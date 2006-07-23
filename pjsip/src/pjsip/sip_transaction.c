/* $Id$ */
/* 
 * Copyright (C) 2003-2006 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjsip/sip_transaction.h>
#include <pjsip/sip_util.h>
#include <pjsip/sip_module.h>
#include <pjsip/sip_endpoint.h>
#include <pjsip/sip_errno.h>
#include <pjsip/sip_event.h>
#include <pj/hash.h>
#include <pj/pool.h>
#include <pj/os.h>
#include <pj/string.h>
#include <pj/assert.h>
#include <pj/guid.h>
#include <pj/log.h>

#define THIS_FILE   "sip_transaction.c"

#if 0
#define TSX_TRACE_(expr)    PJ_LOG(3,expr)
#else
#define TSX_TRACE_(expr)
#endif

/* When this macro is set, transaction will keep the hashed value
 * so that future lookup (to unregister transaction) does not need
 * to recalculate the hash again. It should gains a little bit of
 * performance, so generally we'd want this.
 */
#define PRECALC_HASH


/* Defined in sip_util_statefull.c */
extern pjsip_module mod_stateful_util;


/*****************************************************************************
 **
 ** Declarations and static variable definitions section.
 **
 *****************************************************************************
 **/
/* Prototypes. */
static pj_status_t mod_tsx_layer_load(pjsip_endpoint *endpt);
static pj_status_t mod_tsx_layer_start(void);
static pj_status_t mod_tsx_layer_stop(void);
static pj_status_t mod_tsx_layer_unload(void);
static pj_bool_t   mod_tsx_layer_on_rx_request(pjsip_rx_data *rdata);
static pj_bool_t   mod_tsx_layer_on_rx_response(pjsip_rx_data *rdata);

/* Transaction layer module definition. */
static struct mod_tsx_layer
{
    struct pjsip_module  mod;
    pj_pool_t		*pool;
    pjsip_endpoint	*endpt;
    pj_mutex_t		*mutex;
    pj_hash_table_t	*htable;
} mod_tsx_layer = 
{   {
	NULL, NULL,			/* List's prev and next.    */
	{ "mod-tsx-layer", 13 },	/* Module name.		    */
	-1,				/* Module ID		    */
	PJSIP_MOD_PRIORITY_TSX_LAYER,	/* Priority.		    */
	mod_tsx_layer_load,		/* load().		    */
	mod_tsx_layer_start,		/* start()		    */
	mod_tsx_layer_stop,		/* stop()		    */
	mod_tsx_layer_unload,		/* unload()		    */
	mod_tsx_layer_on_rx_request,	/* on_rx_request()	    */
	mod_tsx_layer_on_rx_response,	/* on_rx_response()	    */
	NULL
    }
};

/* Thread Local Storage ID for transaction lock */
static long pjsip_tsx_lock_tls_id;

/* Transaction state names */
static const char *state_str[] = 
{
    "Null",
    "Calling",
    "Trying",
    "Proceeding",
    "Completed",
    "Confirmed",
    "Terminated",
    "Destroyed",
};

/* Role names */
static const char *role_name[] = 
{
    "UAC",
    "UAS"
};

/* Transport flag. */
enum
{
    TSX_HAS_PENDING_TRANSPORT	= 1,
    TSX_HAS_PENDING_RESCHED	= 2,
    TSX_HAS_PENDING_SEND	= 4,
    TSX_HAS_PENDING_DESTROY	= 8,
    TSX_HAS_RESOLVED_SERVER	= 16,
};

/* Transaction lock. */
typedef struct tsx_lock_data {
    struct tsx_lock_data *prev;
    pjsip_transaction    *tsx;
    int			  is_alive;
} tsx_lock_data;


/* Timer timeout value constants */
static const pj_time_val t1_timer_val = { PJSIP_T1_TIMEOUT/1000, 
                                          PJSIP_T1_TIMEOUT%1000 };
static const pj_time_val t4_timer_val = { PJSIP_T4_TIMEOUT/1000, 
                                          PJSIP_T4_TIMEOUT%1000 };
static const pj_time_val td_timer_val = { PJSIP_TD_TIMEOUT/1000, 
                                          PJSIP_TD_TIMEOUT%1000 };
static const pj_time_val timeout_timer_val = { (64*PJSIP_T1_TIMEOUT)/1000,
					       (64*PJSIP_T1_TIMEOUT)%1000 };

/* Internal timer IDs */
enum Transaction_Timer_Id
{
    TSX_TIMER_RETRANSMISSION,
    TSX_TIMER_TIMEOUT,
};


/* Prototypes. */
static void	   lock_tsx(pjsip_transaction *tsx, struct tsx_lock_data *lck);
static pj_status_t unlock_tsx( pjsip_transaction *tsx, 
                               struct tsx_lock_data *lck);
static pj_status_t tsx_on_state_null(		pjsip_transaction *tsx, 
				                pjsip_event *event);
static pj_status_t tsx_on_state_calling(	pjsip_transaction *tsx, 
				                pjsip_event *event);
static pj_status_t tsx_on_state_trying(		pjsip_transaction *tsx, 
				                pjsip_event *event);
static pj_status_t tsx_on_state_proceeding_uas( pjsip_transaction *tsx, 
					        pjsip_event *event);
static pj_status_t tsx_on_state_proceeding_uac( pjsip_transaction *tsx,
					        pjsip_event *event);
static pj_status_t tsx_on_state_completed_uas(	pjsip_transaction *tsx, 
					        pjsip_event *event);
static pj_status_t tsx_on_state_completed_uac(	pjsip_transaction *tsx,
					        pjsip_event *event);
static pj_status_t tsx_on_state_confirmed(	pjsip_transaction *tsx, 
					        pjsip_event *event);
static pj_status_t tsx_on_state_terminated(	pjsip_transaction *tsx, 
					        pjsip_event *event);
static pj_status_t tsx_on_state_destroyed(	pjsip_transaction *tsx, 
					        pjsip_event *event);
static void        tsx_timer_callback( pj_timer_heap_t *theap, 
			               pj_timer_entry *entry);
static pj_status_t tsx_create( pjsip_module *tsx_user,
			       pjsip_transaction **p_tsx);
static void	   tsx_destroy( pjsip_transaction *tsx );
static void	   tsx_resched_retransmission( pjsip_transaction *tsx );
static pj_status_t tsx_retransmit( pjsip_transaction *tsx, int resched);
static int         tsx_send_msg( pjsip_transaction *tsx, 
                                 pjsip_tx_data *tdata);


/* State handlers for UAC, indexed by state */
static int  (*tsx_state_handler_uac[PJSIP_TSX_STATE_MAX])(pjsip_transaction *,
							  pjsip_event *) = 
{
    &tsx_on_state_null,
    &tsx_on_state_calling,
    NULL,
    &tsx_on_state_proceeding_uac,
    &tsx_on_state_completed_uac,
    &tsx_on_state_confirmed,
    &tsx_on_state_terminated,
    &tsx_on_state_destroyed,
};

/* State handlers for UAS */
static int  (*tsx_state_handler_uas[PJSIP_TSX_STATE_MAX])(pjsip_transaction *, 
							  pjsip_event *) = 
{
    &tsx_on_state_null,
    NULL,
    &tsx_on_state_trying,
    &tsx_on_state_proceeding_uas,
    &tsx_on_state_completed_uas,
    &tsx_on_state_confirmed,
    &tsx_on_state_terminated,
    &tsx_on_state_destroyed,
};

/*****************************************************************************
 **
 ** Utilities
 **
 *****************************************************************************
 */
/*
 * Get transaction state name.
 */
PJ_DEF(const char *) pjsip_tsx_state_str(pjsip_tsx_state_e state)
{
    return state_str[state];
}

/*
 * Get the role name.
 */
PJ_DEF(const char *) pjsip_role_name(pjsip_role_e role)
{
    return role_name[role];
}


/*
 * Create transaction key for RFC2543 compliant messages, which don't have
 * unique branch parameter in the top most Via header.
 *
 * INVITE requests matches a transaction if the following attributes
 * match the original request:
 *	- Request-URI
 *	- To tag
 *	- From tag
 *	- Call-ID
 *	- CSeq
 *	- top Via header
 *
 * CANCEL matching is done similarly as INVITE, except:
 *	- CSeq method will differ
 *	- To tag is not matched.
 *
 * ACK matching is done similarly, except that:
 *	- method of the CSeq will differ,
 *	- To tag is matched to the response sent by the server transaction.
 *
 * The transaction key is constructed from the common components of above
 * components. Additional comparison is needed to fully match a transaction.
 */
static pj_status_t create_tsx_key_2543( pj_pool_t *pool,
			                pj_str_t *str,
			                pjsip_role_e role,
			                const pjsip_method *method,
			                const pjsip_rx_data *rdata )
{
#define SEPARATOR   '$'
    char *key, *p, *end;
    int len;
    pj_size_t len_required;
    pjsip_uri *req_uri;
    pj_str_t *host;

    PJ_ASSERT_RETURN(pool && str && method && rdata, PJ_EINVAL);
    PJ_ASSERT_RETURN(rdata->msg_info.msg, PJ_EINVAL);
    PJ_ASSERT_RETURN(rdata->msg_info.via, PJSIP_EMISSINGHDR);
    PJ_ASSERT_RETURN(rdata->msg_info.cseq, PJSIP_EMISSINGHDR);
    PJ_ASSERT_RETURN(rdata->msg_info.from, PJSIP_EMISSINGHDR);

    host = &rdata->msg_info.via->sent_by.host;
    req_uri = (pjsip_uri*)rdata->msg_info.msg->line.req.uri;

    /* Calculate length required. */
    len_required = 9 +			    /* CSeq number */
		   rdata->msg_info.from->tag.slen +   /* From tag. */
		   rdata->msg_info.cid->id.slen +    /* Call-ID */
		   host->slen +		    /* Via host. */
		   9 +			    /* Via port. */
		   16;			    /* Separator+Allowance. */
    key = p = pj_pool_alloc(pool, len_required);
    end = p + len_required;

    /* Add role. */
    *p++ = (char)(role==PJSIP_ROLE_UAC ? 'c' : 's');
    *p++ = SEPARATOR;

    /* Add method, except when method is INVITE or ACK. */
    if (method->id != PJSIP_INVITE_METHOD && method->id != PJSIP_ACK_METHOD) {
	pj_memcpy(p, method->name.ptr, method->name.slen);
	p += method->name.slen;
	*p++ = '$';
    }

    /* Add CSeq (only the number). */
    len = pj_utoa(rdata->msg_info.cseq->cseq, p);
    p += len;
    *p++ = SEPARATOR;

    /* Add From tag. */
    len = rdata->msg_info.from->tag.slen;
    pj_memcpy( p, rdata->msg_info.from->tag.ptr, len);
    p += len;
    *p++ = SEPARATOR;

    /* Add Call-ID. */
    len = rdata->msg_info.cid->id.slen;
    pj_memcpy( p, rdata->msg_info.cid->id.ptr, len );
    p += len;
    *p++ = SEPARATOR;

    /* Add top Via header. 
     * We don't really care whether the port contains the real port (because
     * it can be omited if default port is used). Anyway this function is 
     * only used to match request retransmission, and we expect that the 
     * request retransmissions will contain the same port.
     */
    pj_memcpy(p, host->ptr, host->slen);
    p += host->slen;
    *p++ = ':';

    len = pj_utoa(rdata->msg_info.via->sent_by.port, p);
    p += len;
    *p++ = SEPARATOR;
    
    *p++ = '\0';

    /* Done. */
    str->ptr = key;
    str->slen = p-key;

    return PJ_SUCCESS;
}

/*
 * Create transaction key for RFC3161 compliant system.
 */
static pj_status_t create_tsx_key_3261( pj_pool_t *pool,
		                        pj_str_t *key,
		                        pjsip_role_e role,
		                        const pjsip_method *method,
		                        const pj_str_t *branch)
{
    char *p;

    PJ_ASSERT_RETURN(pool && key && method && branch, PJ_EINVAL);

    p = key->ptr = pj_pool_alloc(pool, branch->slen + method->name.slen + 4 );
    
    /* Add role. */
    *p++ = (char)(role==PJSIP_ROLE_UAC ? 'c' : 's');
    *p++ = SEPARATOR;

    /* Add method, except when method is INVITE or ACK. */
    if (method->id != PJSIP_INVITE_METHOD && method->id != PJSIP_ACK_METHOD) {
	pj_memcpy(p, method->name.ptr, method->name.slen);
	p += method->name.slen;
	*p++ = '$';
    }

    /* Add branch ID. */
    pj_memcpy(p, branch->ptr, branch->slen);
    p += branch->slen;

    /* Set length */
    key->slen = p - key->ptr;

    return PJ_SUCCESS;
}

/*
 * Create key from the incoming data, to be used to search the transaction
 * in the transaction hash table.
 */
PJ_DEF(pj_status_t) pjsip_tsx_create_key( pj_pool_t *pool, pj_str_t *key, 
				          pjsip_role_e role, 
				          const pjsip_method *method, 
				          const pjsip_rx_data *rdata)
{
    pj_str_t rfc3261_branch = {PJSIP_RFC3261_BRANCH_ID, 
                               PJSIP_RFC3261_BRANCH_LEN};


    /* Get the branch parameter in the top-most Via.
     * If branch parameter is started with "z9hG4bK", then the message was
     * generated by agent compliant with RFC3261. Otherwise, it will be
     * handled as RFC2543.
     */
    const pj_str_t *branch = &rdata->msg_info.via->branch_param;

    if (pj_strncmp(branch,&rfc3261_branch,PJSIP_RFC3261_BRANCH_LEN)==0) {

	/* Create transaction key. */
	return create_tsx_key_3261(pool, key, role, method, branch);

    } else {
	/* Create the key for the message. This key will be matched up 
         * with the transaction key. For RFC2563 transactions, the 
         * transaction key was created by the same function, so it will 
         * match the message.
	 */
	return create_tsx_key_2543( pool, key, role, method, rdata );
    }
}

/*****************************************************************************
 **
 ** Transaction layer module
 **
 *****************************************************************************
 **/
/*
 * Create transaction layer module and registers it to the endpoint.
 */
PJ_DEF(pj_status_t) pjsip_tsx_layer_init_module(pjsip_endpoint *endpt)
{
    pj_pool_t *pool;
    pj_status_t status;


    PJ_ASSERT_RETURN(mod_tsx_layer.endpt==NULL, PJ_EINVALIDOP);

    /* Initialize TLS ID for transaction lock. */
    status = pj_thread_local_alloc(&pjsip_tsx_lock_tls_id);
    if (status != PJ_SUCCESS)
	return status;

    pj_thread_local_set(pjsip_tsx_lock_tls_id, NULL);

    /*
     * Initialize transaction layer structure.
     */

    /* Create pool for the module. */
    pool = pjsip_endpt_create_pool(endpt, "tsxlayer", 
				   PJSIP_POOL_TSX_LAYER_LEN,
				   PJSIP_POOL_TSX_LAYER_INC );
    if (!pool)
	return PJ_ENOMEM;

    
    /* Initialize some attributes. */
    mod_tsx_layer.pool = pool;
    mod_tsx_layer.endpt = endpt;


    /* Create hash table. */
    mod_tsx_layer.htable = pj_hash_create( pool, PJSIP_MAX_TSX_COUNT );
    if (!mod_tsx_layer.htable) {
	pjsip_endpt_release_pool(endpt, pool);
	return PJ_ENOMEM;
    }

    /* Create mutex. */
    status = pj_mutex_create_recursive(pool, "tsxlayer", &mod_tsx_layer.mutex);
    if (status != PJ_SUCCESS) {
	pjsip_endpt_release_pool(endpt, pool);
	return status;
    }

    /*
     * Register transaction layer module to endpoint.
     */
    status = pjsip_endpt_register_module( endpt, &mod_tsx_layer.mod );
    if (status != PJ_SUCCESS) {
	pj_mutex_destroy(mod_tsx_layer.mutex);
	pjsip_endpt_release_pool(endpt, pool);
	return status;
    }

    /* Register mod_stateful_util module (sip_util_statefull.c) */
    status = pjsip_endpt_register_module(endpt, &mod_stateful_util);
    if (status != PJ_SUCCESS) {
	return status;
    }

    return PJ_SUCCESS;
}


/*
 * Get the instance of transaction layer module.
 */
PJ_DEF(pjsip_module*) pjsip_tsx_layer_instance(void)
{
    return &mod_tsx_layer.mod;
}


/*
 * Unregister and destroy transaction layer module.
 */
PJ_DEF(pj_status_t) pjsip_tsx_layer_destroy(void)
{
    /* Are we registered? */
    PJ_ASSERT_RETURN(mod_tsx_layer.endpt!=NULL, PJ_EINVALIDOP);

    /* Unregister from endpoint. 
     * Clean-ups will be done in the unload() module callback.
     */
    return pjsip_endpt_unregister_module( mod_tsx_layer.endpt, 
					  &mod_tsx_layer.mod);
}


/*
 * Register the transaction to the hash table.
 */
static pj_status_t mod_tsx_layer_register_tsx( pjsip_transaction *tsx)
{
    pj_assert(tsx->transaction_key.slen != 0);

    /* Lock hash table mutex. */
    pj_mutex_lock(mod_tsx_layer.mutex);

    /* Check if no transaction with the same key exists. 
     * Do not use PJ_ASSERT_RETURN since it evaluates the expression
     * twice!
     */
    pj_assert(pj_hash_get( mod_tsx_layer.htable, 
			   &tsx->transaction_key.ptr,
			   tsx->transaction_key.slen, 
			   NULL) == NULL);

    TSX_TRACE_((THIS_FILE, 
		"Transaction %p registered with hkey=0x%p and key=%.*s",
		tsx, tsx->hashed_key, tsx->transaction_key.slen,
		tsx->transaction_key.ptr));

    /* Register the transaction to the hash table. */
#ifdef PRECALC_HASH
    pj_hash_set( tsx->pool, mod_tsx_layer.htable, tsx->transaction_key.ptr,
    		 tsx->transaction_key.slen, tsx->hashed_key, tsx);
#else
    pj_hash_set( tsx->pool, mod_tsx_layer.htable, tsx->transaction_key.ptr,
    		 tsx->transaction_key.slen, 0, tsx);
#endif

    /* Unlock mutex. */
    pj_mutex_unlock(mod_tsx_layer.mutex);

    return PJ_SUCCESS;
}


/*
 * Unregister the transaction from the hash table.
 */
static void mod_tsx_layer_unregister_tsx( pjsip_transaction *tsx)
{
    pj_assert(tsx->transaction_key.slen != 0);
    //pj_assert(tsx->state != PJSIP_TSX_STATE_NULL);

    /* Lock hash table mutex. */
    pj_mutex_lock(mod_tsx_layer.mutex);

    /* Register the transaction to the hash table. */
#ifdef PRECALC_HASH
    pj_hash_set( NULL, mod_tsx_layer.htable, tsx->transaction_key.ptr,
    		 tsx->transaction_key.slen, tsx->hashed_key, NULL);
#else
    pj_hash_set( NULL, mod_tsx_layer.htable, tsx->transaction_key.ptr,
    		 tsx->transaction_key.slen, 0, NULL);
#endif

    TSX_TRACE_((THIS_FILE, 
		"Transaction %p unregistered, hkey=0x%p and key=%.*s",
		tsx, tsx->hashed_key, tsx->transaction_key.slen,
		tsx->transaction_key.ptr));

    /* Unlock mutex. */
    pj_mutex_unlock(mod_tsx_layer.mutex);
}


/*
 * Find a transaction.
 */
PJ_DEF(pjsip_transaction*) pjsip_tsx_layer_find_tsx( const pj_str_t *key,
						     pj_bool_t lock )
{
    pjsip_transaction *tsx;
    pj_uint32_t hval = 0;

    pj_mutex_lock(mod_tsx_layer.mutex);
    tsx = pj_hash_get( mod_tsx_layer.htable, key->ptr, key->slen, &hval );
    pj_mutex_unlock(mod_tsx_layer.mutex);

    TSX_TRACE_((THIS_FILE, 
		"Finding tsx with hkey=0x%p and key=%.*s: found %p",
		hval, key->slen, key->ptr, tsx));

    /* Race condition!
     * Transaction may gets deleted before we have chance to lock it.
     */
    PJ_TODO(FIX_RACE_CONDITION_HERE);
    if (tsx && lock)
	pj_mutex_lock(tsx->mutex);

    return tsx;
}


/* This module callback is called when module is being loaded by
 * endpoint. It does nothing for this module.
 */
static pj_status_t mod_tsx_layer_load(pjsip_endpoint *endpt)
{
    PJ_UNUSED_ARG(endpt);
    return PJ_SUCCESS;
}


/* This module callback is called when module is being started by
 * endpoint. It does nothing for this module.
 */
static pj_status_t mod_tsx_layer_start(void)
{
    return PJ_SUCCESS;
}


/* This module callback is called when module is being stopped by
 * endpoint. 
 */
static pj_status_t mod_tsx_layer_stop(void)
{
    pj_hash_iterator_t it_buf, *it;

    PJ_LOG(4,(THIS_FILE, "Stopping transaction layer module"));

    pj_mutex_lock(mod_tsx_layer.mutex);

    /* Destroy all transactions. */
    it = pj_hash_first(mod_tsx_layer.htable, &it_buf);
    while (it) {
	pjsip_transaction *tsx = pj_hash_this(mod_tsx_layer.htable, it);
	pj_hash_iterator_t *next = pj_hash_next(mod_tsx_layer.htable, it);
	if (tsx)
	    tsx_destroy(tsx);
	it = next;
    }

    pj_mutex_unlock(mod_tsx_layer.mutex);
    return PJ_SUCCESS;
}


/* This module callback is called when module is being unloaded by
 * endpoint.
 */
static pj_status_t mod_tsx_layer_unload(void)
{
    /* Only self destroy when there's no transaction in the table.
     * Transaction may refuse to destroy when it has pending
     * transmission. If we destroy the module now, application will
     * crash when the pending transaction finally got error response
     * from transport and when it tries to unregister itself.
     */
    if (pj_hash_count(mod_tsx_layer.htable) != 0)
	return PJ_EBUSY;

    /* Destroy mutex. */
    pj_mutex_destroy(mod_tsx_layer.mutex);

    /* Release pool. */
    pjsip_endpt_release_pool(mod_tsx_layer.endpt, mod_tsx_layer.pool);

    /* Mark as unregistered. */
    mod_tsx_layer.endpt = NULL;

    PJ_LOG(4,(THIS_FILE, "Transaction layer module destroyed"));

    return PJ_SUCCESS;
}


/* This module callback is called when endpoint has received an
 * incoming request message.
 */
static pj_bool_t mod_tsx_layer_on_rx_request(pjsip_rx_data *rdata)
{
    pj_str_t key;
    pj_uint32_t hval = 0;
    pjsip_transaction *tsx;

    pjsip_tsx_create_key(rdata->tp_info.pool, &key, PJSIP_ROLE_UAS,
			 &rdata->msg_info.cseq->method, rdata);

    /* Find transaction. */
    pj_mutex_lock( mod_tsx_layer.mutex );

    tsx = pj_hash_get( mod_tsx_layer.htable, key.ptr, key.slen, &hval );


    TSX_TRACE_((THIS_FILE, 
		"Finding tsx for request, hkey=0x%p and key=%.*s, found %p",
		hval, key.slen, key.ptr, tsx));


    if (tsx == NULL || tsx->state == PJSIP_TSX_STATE_TERMINATED) {
	/* Transaction not found.
	 * Reject the request so that endpoint passes the request to
	 * upper layer modules.
	 */
	pj_mutex_unlock( mod_tsx_layer.mutex);
	return PJ_FALSE;
    }

    /* Unlock hash table. */
    pj_mutex_unlock( mod_tsx_layer.mutex );

    /* Race condition!
     * Transaction may gets deleted before we have chance to lock it
     * in pjsip_tsx_recv_msg().
     */
    PJ_TODO(FIX_RACE_CONDITION_HERE);

    /* Pass the message to the transaction. */
    pjsip_tsx_recv_msg(tsx, rdata );

    return PJ_TRUE;
}


/* This module callback is called when endpoint has received an
 * incoming response message.
 */
static pj_bool_t mod_tsx_layer_on_rx_response(pjsip_rx_data *rdata)
{
    pj_str_t key;
    pj_uint32_t hval = 0;
    pjsip_transaction *tsx;

    pjsip_tsx_create_key(rdata->tp_info.pool, &key, PJSIP_ROLE_UAC,
			 &rdata->msg_info.cseq->method, rdata);

    /* Find transaction. */
    pj_mutex_lock( mod_tsx_layer.mutex );

    tsx = pj_hash_get( mod_tsx_layer.htable, key.ptr, key.slen, &hval );


    TSX_TRACE_((THIS_FILE, 
		"Finding tsx for response, hkey=0x%p and key=%.*s, found %p",
		hval, key.slen, key.ptr, tsx));


    if (tsx == NULL || tsx->state == PJSIP_TSX_STATE_TERMINATED) {
	/* Transaction not found.
	 * Reject the request so that endpoint passes the request to
	 * upper layer modules.
	 */
	pj_mutex_unlock( mod_tsx_layer.mutex);
	return PJ_FALSE;
    }

    /* Unlock hash table. */
    pj_mutex_unlock( mod_tsx_layer.mutex );

    /* Race condition!
     * Transaction may gets deleted before we have chance to lock it
     * in pjsip_tsx_recv_msg().
     */
    PJ_TODO(FIX_RACE_CONDITION_HERE);

    /* Pass the message to the transaction. */
    pjsip_tsx_recv_msg(tsx, rdata );

    return PJ_TRUE;
}


/*
 * Get transaction instance in the rdata.
 */
PJ_DEF(pjsip_transaction*) pjsip_rdata_get_tsx( pjsip_rx_data *rdata )
{
    return rdata->endpt_info.mod_data[mod_tsx_layer.mod.id];
}


/*
 * Dump transaction layer.
 */
PJ_DEF(void) pjsip_tsx_layer_dump(pj_bool_t detail)
{
#if PJ_LOG_MAX_LEVEL >= 3
    pj_hash_iterator_t itbuf, *it;

    /* Lock mutex. */
    pj_mutex_lock(mod_tsx_layer.mutex);

    PJ_LOG(3, (THIS_FILE, "Dumping transaction table:"));
    PJ_LOG(3, (THIS_FILE, " Total %d transactions", 
			  pj_hash_count(mod_tsx_layer.htable)));

    if (detail) {
	it = pj_hash_first(mod_tsx_layer.htable, &itbuf);
	if (it == NULL) {
	    PJ_LOG(3, (THIS_FILE, " - none - "));
	} else {
	    while (it != NULL) {
		pjsip_transaction *tsx = pj_hash_this(mod_tsx_layer.htable,it);

		PJ_LOG(3, (THIS_FILE, " %s %s|%d|%s",
			   tsx->obj_name,
			   (tsx->last_tx? 
				pjsip_tx_data_get_info(tsx->last_tx): 
				"none"),
			   tsx->status_code,
			   pjsip_tsx_state_str(tsx->state)));

		it = pj_hash_next(mod_tsx_layer.htable, it);
	    }
	}
    }

    /* Unlock mutex. */
    pj_mutex_unlock(mod_tsx_layer.mutex);
#endif
}

/*****************************************************************************
 **
 ** Transaction
 **
 *****************************************************************************
 **/
/*
 * Lock transaction and set the value of Thread Local Storage.
 */
static void lock_tsx(pjsip_transaction *tsx, struct tsx_lock_data *lck)
{
    struct tsx_lock_data *prev_data;

    pj_mutex_lock(tsx->mutex);
    prev_data = (struct tsx_lock_data *) 
                    pj_thread_local_get(pjsip_tsx_lock_tls_id);
    lck->prev = prev_data;
    lck->tsx = tsx;
    lck->is_alive = 1;
    pj_thread_local_set(pjsip_tsx_lock_tls_id, lck);
}


/*
 * Unlock transaction.
 * This will selectively unlock the mutex ONLY IF the transaction has not been 
 * destroyed. The function knows whether the transaction has been destroyed
 * because when transaction is destroyed the is_alive flag for the transaction
 * will be set to zero.
 */
static pj_status_t unlock_tsx( pjsip_transaction *tsx, 
                               struct tsx_lock_data *lck)
{
    pj_assert( (void*)pj_thread_local_get(pjsip_tsx_lock_tls_id) == lck);
    pj_assert( lck->tsx == tsx );
    pj_thread_local_set(pjsip_tsx_lock_tls_id, lck->prev);
    if (lck->is_alive)
	pj_mutex_unlock(tsx->mutex);

    return lck->is_alive ? PJ_SUCCESS : PJSIP_ETSXDESTROYED;
}


/* Create and initialize basic transaction structure.
 * This function is called by both UAC and UAS creation.
 */
static pj_status_t tsx_create( pjsip_module *tsx_user,
			       pjsip_transaction **p_tsx)
{
    pj_pool_t *pool;
    pjsip_transaction *tsx;
    pj_status_t status;

    pool = pjsip_endpt_create_pool( mod_tsx_layer.endpt, "tsx", 
				    PJSIP_POOL_TSX_LEN, PJSIP_POOL_TSX_INC );
    if (!pool)
	return PJ_ENOMEM;

    tsx = pj_pool_zalloc(pool, sizeof(pjsip_transaction));
    tsx->pool = pool;
    tsx->tsx_user = tsx_user;
    tsx->endpt = mod_tsx_layer.endpt;

    pj_ansi_sprintf(tsx->obj_name, "tsx%p", tsx);

    tsx->handle_200resp = 1;
    tsx->retransmit_timer.id = TSX_TIMER_RETRANSMISSION;
    tsx->retransmit_timer._timer_id = -1;
    tsx->retransmit_timer.user_data = tsx;
    tsx->retransmit_timer.cb = &tsx_timer_callback;
    tsx->timeout_timer.id = TSX_TIMER_TIMEOUT;
    tsx->timeout_timer._timer_id = -1;
    tsx->timeout_timer.user_data = tsx;
    tsx->timeout_timer.cb = &tsx_timer_callback;
    
    status = pj_mutex_create_recursive(pool, "tsx%p", &tsx->mutex);
    if (status != PJ_SUCCESS) {
	pjsip_endpt_release_pool(mod_tsx_layer.endpt, pool);
	return status;
    }

    *p_tsx = tsx;
    return PJ_SUCCESS;
}


/* Destroy transaction. */
static void tsx_destroy( pjsip_transaction *tsx )
{
    struct tsx_lock_data *lck;

    /* Decrement transport reference counter. */
    if (tsx->transport) {
	pjsip_transport_dec_ref( tsx->transport );
	tsx->transport = NULL;
    }
    /* Free last transmitted message. */
    if (tsx->last_tx) {
	pjsip_tx_data_dec_ref( tsx->last_tx );
	tsx->last_tx = NULL;
    }
    /* Cancel timeout timer. */
    if (tsx->timeout_timer._timer_id != -1) {
	pjsip_endpt_cancel_timer(tsx->endpt, &tsx->timeout_timer);
	tsx->timeout_timer._timer_id = -1;
    }
    /* Cancel retransmission timer. */
    if (tsx->retransmit_timer._timer_id != -1) {
	pjsip_endpt_cancel_timer(tsx->endpt, &tsx->retransmit_timer);
	tsx->retransmit_timer._timer_id = -1;
    }

    /* Clear some pending flags. */
    tsx->transport_flag &= ~(TSX_HAS_PENDING_RESCHED | TSX_HAS_PENDING_SEND);

    /* Refuse to destroy transaction if it has pending resolving. */
    if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
	tsx->transport_flag |= TSX_HAS_PENDING_DESTROY;
	tsx->tsx_user = NULL;
	PJ_LOG(4,(tsx->obj_name, "Will destroy later because transport is "
				 "in progress"));
	return;
    }

    /* Clear TLS, so that mutex will not be unlocked */
    lck = pj_thread_local_get(pjsip_tsx_lock_tls_id);
    while (lck) {
	if (lck->tsx == tsx) {
	    lck->is_alive = 0;
	}
	lck = lck->prev;
    }

    pj_mutex_destroy(tsx->mutex);

    PJ_LOG(5,(tsx->obj_name, "Transaction destroyed!"));

    pjsip_endpt_release_pool(tsx->endpt, tsx->pool);
}


/*
 * Callback when timer expires.
 */
static void tsx_timer_callback( pj_timer_heap_t *theap, pj_timer_entry *entry)
{
    pjsip_event event;
    pjsip_transaction *tsx = entry->user_data;
    struct tsx_lock_data lck;

    PJ_UNUSED_ARG(theap);

    PJ_LOG(5,(tsx->obj_name, "%s timer event", 
	     (entry->id==TSX_TIMER_RETRANSMISSION ? "Retransmit":"Timeout")));


    if (entry->id == TSX_TIMER_RETRANSMISSION) {
        PJSIP_EVENT_INIT_TIMER(event, &tsx->retransmit_timer);
    } else {
        PJSIP_EVENT_INIT_TIMER(event, &tsx->timeout_timer);
    }

    /* Dispatch event to transaction. */
    lock_tsx(tsx, &lck);
    (*tsx->state_handler)(tsx, &event);
    unlock_tsx(tsx, &lck);
}


/*
 * Set transaction state, and inform TU about the transaction state change.
 */
static void tsx_set_state( pjsip_transaction *tsx,
			   pjsip_tsx_state_e state,
			   pjsip_event_id_e event_src_type,
                           void *event_src )
{
    pjsip_tsx_state_e prev_state = tsx->state;

    PJ_LOG(5, (tsx->obj_name, "State changed from %s to %s, event=%s",
	       state_str[tsx->state], state_str[state], 
               pjsip_event_str(event_src_type)));

    /* Change state. */
    tsx->state = state;

    /* Update the state handlers. */
    if (tsx->role == PJSIP_ROLE_UAC) {
	tsx->state_handler = tsx_state_handler_uac[state];
    } else {
	tsx->state_handler = tsx_state_handler_uas[state];
    }

    /* Before informing TU about state changed, inform TU about
     * rx event.
     */
    if (event_src_type==PJSIP_EVENT_RX_MSG && tsx->tsx_user) {
	pjsip_rx_data *rdata = event_src;

	pj_assert(rdata != NULL);

	if (rdata->msg_info.msg->type == PJSIP_RESPONSE_MSG &&
		   tsx->tsx_user->on_rx_response)
	{
	    (*tsx->tsx_user->on_rx_response)(rdata);
	}

    }

    /* Inform TU about state changed. */
    if (tsx->tsx_user && tsx->tsx_user->on_tsx_state) {
	pjsip_event e;
	PJSIP_EVENT_INIT_TSX_STATE(e, tsx, event_src_type, event_src,
				   prev_state);
	(*tsx->tsx_user->on_tsx_state)(tsx, &e);
    }
    

    /* When the transaction is terminated, release transport, and free the
     * saved last transmitted message.
     */
    if (state == PJSIP_TSX_STATE_TERMINATED) {
	pj_time_val timeout = {0, 0};

	/* Reschedule timeout timer to destroy this transaction. */
	if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
	    tsx->transport_flag |= TSX_HAS_PENDING_DESTROY;
	} else {
	    /* Cancel timeout timer. */
	    if (tsx->timeout_timer._timer_id != -1) {
		pjsip_endpt_cancel_timer(tsx->endpt, &tsx->timeout_timer);
		tsx->timeout_timer._timer_id = -1;
	    }

	    pjsip_endpt_schedule_timer( tsx->endpt, &tsx->timeout_timer, 
					&timeout);
	}


    } else if (state == PJSIP_TSX_STATE_DESTROYED) {

	/* Unregister transaction. */
	mod_tsx_layer_unregister_tsx(tsx);

	/* Destroy transaction. */
	tsx_destroy(tsx);
    }
}


/*
 * Create, initialize, and register UAC transaction.
 */
PJ_DEF(pj_status_t) pjsip_tsx_create_uac( pjsip_module *tsx_user,
					  pjsip_tx_data *tdata,
					  pjsip_transaction **p_tsx)
{
    pjsip_transaction *tsx;
    pjsip_msg *msg;
    pjsip_cseq_hdr *cseq;
    pjsip_via_hdr *via;
    pjsip_host_info dst_info;
    struct tsx_lock_data lck;
    pj_status_t status;

    /* Validate arguments. */
    PJ_ASSERT_RETURN(tdata && tdata->msg && p_tsx, PJ_EINVAL);
    PJ_ASSERT_RETURN(tdata->msg->type == PJSIP_REQUEST_MSG,
		     PJSIP_ENOTREQUESTMSG);

    /* Method MUST NOT be ACK! */
    PJ_ASSERT_RETURN(tdata->msg->line.req.method.id != PJSIP_ACK_METHOD,
		     PJ_EINVALIDOP);

    /* Keep shortcut */
    msg = tdata->msg;

    /* Make sure CSeq header is present. */
    cseq = pjsip_msg_find_hdr(msg, PJSIP_H_CSEQ, NULL);
    if (!cseq) {
	pj_assert(!"CSeq header not present in outgoing message!");
	return PJSIP_EMISSINGHDR;
    }


    /* Create transaction instance. */
    status = tsx_create( tsx_user, &tsx);
    if (status != PJ_SUCCESS)
	return status;


    /* Lock transaction. */
    lock_tsx(tsx, &lck);

    /* Role is UAC. */
    tsx->role = PJSIP_ROLE_UAC;

    /* Save method. */
    pjsip_method_copy( tsx->pool, &tsx->method, &msg->line.req.method);

    /* Save CSeq. */
    tsx->cseq = cseq->cseq;

    /* Generate Via header if it doesn't exist. */
    via = pjsip_msg_find_hdr(msg, PJSIP_H_VIA, NULL);
    if (via == NULL) {
	via = pjsip_via_hdr_create(tdata->pool);
	pjsip_msg_insert_first_hdr(msg, (pjsip_hdr*) via);
    }

    /* Generate branch parameter if it doesn't exist. */
    if (via->branch_param.slen == 0) {
	pj_str_t tmp;
	via->branch_param.ptr = pj_pool_alloc(tsx->pool, PJSIP_MAX_BRANCH_LEN);
	via->branch_param.slen = PJSIP_MAX_BRANCH_LEN;
	pj_memcpy(via->branch_param.ptr, PJSIP_RFC3261_BRANCH_ID, 
		  PJSIP_RFC3261_BRANCH_LEN);
	tmp.ptr = via->branch_param.ptr + PJSIP_RFC3261_BRANCH_LEN + 2;
	*(tmp.ptr-2) = 80; *(tmp.ptr-1) = 106;
	pj_generate_unique_string( &tmp );

        /* Save branch parameter. */
        tsx->branch = via->branch_param;

    } else {
        /* Copy branch parameter. */
        pj_strdup(tsx->pool, &tsx->branch, &via->branch_param);
    }

   /* Generate transaction key. */
    create_tsx_key_3261( tsx->pool, &tsx->transaction_key,
			 PJSIP_ROLE_UAC, &tsx->method, 
			 &via->branch_param);

    /* Calculate hashed key value. */
#ifdef PRECALC_HASH
    tsx->hashed_key = pj_hash_calc(0, tsx->transaction_key.ptr,
				   tsx->transaction_key.slen);
#endif

    PJ_LOG(6, (tsx->obj_name, "tsx_key=%.*s", tsx->transaction_key.slen,
	       tsx->transaction_key.ptr));

    /* Begin with State_Null.
     * Manually set-up the state becase we don't want to call the callback.
     */
    tsx->state = PJSIP_TSX_STATE_NULL;
    tsx->state_handler = &tsx_on_state_null;

    /* Save the message. */
    tsx->last_tx = tdata;
    pjsip_tx_data_add_ref(tsx->last_tx);

    /* Determine whether reliable transport should be used initially.
     * This will be updated whenever transport has changed.
     */
    status = pjsip_get_request_addr(tdata, &dst_info);
    if (status != PJ_SUCCESS) {
	tsx_destroy(tsx);
	return status;
    }
    tsx->is_reliable = (dst_info.flag & PJSIP_TRANSPORT_RELIABLE);

    /* Register transaction to hash table. */
    status = mod_tsx_layer_register_tsx(tsx);
    if (status != PJ_SUCCESS) {
	pj_assert(!"Bug in branch_param generator (i.e. not unique)");
	tsx_destroy(tsx);
	return status;
    }


    /* Unlock transaction and return. */
    unlock_tsx(tsx, &lck);

    PJ_LOG(5,(tsx->obj_name, "Transaction created for %s",
	      pjsip_tx_data_get_info(tdata)));

    *p_tsx = tsx;
    return PJ_SUCCESS;
}


/*
 * Create, initialize, and register UAS transaction.
 */
PJ_DEF(pj_status_t) pjsip_tsx_create_uas( pjsip_module *tsx_user,
					  pjsip_rx_data *rdata,
					  pjsip_transaction **p_tsx)
{
    pjsip_transaction *tsx;
    pjsip_msg *msg;
    pj_str_t *branch;
    pjsip_cseq_hdr *cseq;
    pj_status_t status;
    struct tsx_lock_data lck;

    /* Validate arguments. */
    PJ_ASSERT_RETURN(rdata && rdata->msg_info.msg && p_tsx, PJ_EINVAL);

    /* Keep shortcut to message */
    msg = rdata->msg_info.msg;
    
    /* Make sure this is a request message. */
    PJ_ASSERT_RETURN(msg->type == PJSIP_REQUEST_MSG, PJSIP_ENOTREQUESTMSG);

    /* Make sure method is not ACK */
    PJ_ASSERT_RETURN(msg->line.req.method.id != PJSIP_ACK_METHOD,
		     PJ_EINVALIDOP);

    /* Make sure CSeq header is present. */
    cseq = rdata->msg_info.cseq;
    if (!cseq)
	return PJSIP_EMISSINGHDR;

    /* Make sure Via header is present. */
    if (rdata->msg_info.via == NULL)
	return PJSIP_EMISSINGHDR;

    /* Check that method in CSeq header match request method.
     * Reference: PROTOS #1922
     */
    if (pjsip_method_cmp(&msg->line.req.method, 
			 &rdata->msg_info.cseq->method) != 0)
    {
	PJ_LOG(4,(THIS_FILE, "Error: CSeq header contains different "
			     "method than the request line"));
	return PJSIP_EINVALIDHDR;
    }

    /* 
     * Create transaction instance. 
     */
    status = tsx_create( tsx_user, &tsx);
    if (status != PJ_SUCCESS)
	return status;


    /* Lock transaction. */
    lock_tsx(tsx, &lck);

    /* Role is UAS */
    tsx->role = PJSIP_ROLE_UAS;

    /* Save method. */
    pjsip_method_copy( tsx->pool, &tsx->method, &msg->line.req.method);

    /* Save CSeq */
    tsx->cseq = cseq->cseq;

    /* Get transaction key either from branch for RFC3261 message, or
     * create transaction key.
     */
    status = pjsip_tsx_create_key(tsx->pool, &tsx->transaction_key, 
                                  PJSIP_ROLE_UAS, &tsx->method, rdata);
    if (status != PJ_SUCCESS) {
        tsx_destroy(tsx);
        return status;
    }

    /* Calculate hashed key value. */
#ifdef PRECALC_HASH
    tsx->hashed_key = pj_hash_calc(0, tsx->transaction_key.ptr,
				   tsx->transaction_key.slen);
#endif

    /* Duplicate branch parameter for transaction. */
    branch = &rdata->msg_info.via->branch_param;
    pj_strdup(tsx->pool, &tsx->branch, branch);

    PJ_LOG(6, (tsx->obj_name, "tsx_key=%.*s", tsx->transaction_key.slen,
	       tsx->transaction_key.ptr));


    /* Begin with state NULL.
     * Manually set-up the state becase we don't want to call the callback.
     */
    tsx->state = PJSIP_TSX_STATE_NULL; 
    tsx->state_handler = &tsx_on_state_null;

    /* Get response address. */
    status = pjsip_get_response_addr( tsx->pool, rdata, &tsx->res_addr );
    if (status != PJ_SUCCESS) {
	tsx_destroy(tsx);
	return status;
    }

    /* If it's decided that we should use current transport, keep the
     * transport.
     */
    if (tsx->res_addr.transport) {
	tsx->transport = tsx->res_addr.transport;
	pjsip_transport_add_ref(tsx->transport);
	pj_memcpy(&tsx->addr, &tsx->res_addr.addr, tsx->res_addr.addr_len);
	tsx->addr_len = tsx->res_addr.addr_len;
	tsx->is_reliable = PJSIP_TRANSPORT_IS_RELIABLE(tsx->transport);
    }


    /* Register the transaction. */
    status = mod_tsx_layer_register_tsx(tsx);
    if (status != PJ_SUCCESS) {
	tsx_destroy(tsx);
	return status;
    }

    /* Put this transaction in rdata's mod_data. */
    rdata->endpt_info.mod_data[mod_tsx_layer.mod.id] = tsx;

    /* Unlock transaction and return. */
    unlock_tsx(tsx, &lck);


    PJ_LOG(5,(tsx->obj_name, "Transaction created for %s",
	      pjsip_rx_data_get_info(rdata)));


    *p_tsx = tsx;
    return PJ_SUCCESS;
}


/*
 * Set transaction status code and reason.
 */
static void tsx_set_status_code(pjsip_transaction *tsx,
			   	int code, const pj_str_t *reason)
{
    tsx->status_code = code;
    if (reason)
	pj_strdup(tsx->pool, &tsx->status_text, reason);
    else
	tsx->status_text = *pjsip_get_status_text(code);
}


/*
 * Forcely terminate transaction.
 */
PJ_DEF(pj_status_t) pjsip_tsx_terminate( pjsip_transaction *tsx, int code )
{
    struct tsx_lock_data lck;

    PJ_LOG(5,(tsx->obj_name, "Request to terminate transaction"));

    PJ_ASSERT_RETURN(tsx != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(code >= 200, PJ_EINVAL);

    if (tsx->state == PJSIP_TSX_STATE_TERMINATED)
	return PJ_SUCCESS;

    lock_tsx(tsx, &lck);
    tsx_set_status_code(tsx, code, NULL);
    tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, PJSIP_EVENT_USER, NULL);
    unlock_tsx(tsx, &lck);

    return PJ_SUCCESS;
}


/*
 * This function is called by TU to send a message.
 */
PJ_DEF(pj_status_t) pjsip_tsx_send_msg( pjsip_transaction *tsx, 
				        pjsip_tx_data *tdata )
{
    pjsip_event event;
    struct tsx_lock_data lck;
    pj_status_t status;

    if (tdata == NULL)
	tdata = tsx->last_tx;

    PJ_ASSERT_RETURN(tdata != NULL, PJ_EINVALIDOP);

    PJ_LOG(5,(tsx->obj_name, "Sending %s in state %s",
                             pjsip_tx_data_get_info(tdata),
			     state_str[tsx->state]));

    PJSIP_EVENT_INIT_TX_MSG(event, tdata);

    /* Dispatch to transaction. */
    lock_tsx(tsx, &lck);
    status = (*tsx->state_handler)(tsx, &event);
    unlock_tsx(tsx, &lck);

    /* Will always decrement tdata reference counter
     * (consistent with other send functions.
     */
    if (status == PJ_SUCCESS) {
	pjsip_tx_data_dec_ref(tdata);
    }

    return status;
}


/*
 * This function is called by endpoint when incoming message for the 
 * transaction is received.
 */
PJ_DEF(void) pjsip_tsx_recv_msg( pjsip_transaction *tsx, 
				 pjsip_rx_data *rdata)
{
    pjsip_event event;
    struct tsx_lock_data lck;
    pj_status_t status;

    PJ_LOG(5,(tsx->obj_name, "Incoming %s in state %s", 
	      pjsip_rx_data_get_info(rdata), state_str[tsx->state]));

    /* Put the transaction in the rdata's mod_data. */
    rdata->endpt_info.mod_data[mod_tsx_layer.mod.id] = tsx;

    /* Init event. */
    PJSIP_EVENT_INIT_RX_MSG(event, rdata);

    /* Dispatch to transaction. */
    lock_tsx(tsx, &lck);
    status = (*tsx->state_handler)(tsx, &event);
    unlock_tsx(tsx, &lck);
}


/* Callback called by send message framework */
static void send_msg_callback( pjsip_send_state *send_state,
			       pj_ssize_t sent, pj_bool_t *cont )
{
    pjsip_transaction *tsx = send_state->token;
    struct tsx_lock_data lck;

    lock_tsx(tsx, &lck);

    if (sent > 0) {
	/* Successfully sent! */
	pj_assert(send_state->cur_transport != NULL);

	if (tsx->transport != send_state->cur_transport) {
	    /* Update transport. */
	    if (tsx->transport) {
		pjsip_transport_dec_ref(tsx->transport);
		tsx->transport = NULL;
	    }
	    tsx->transport = send_state->cur_transport;
	    pjsip_transport_add_ref(tsx->transport);

	    /* Update remote address. */
	    tsx->addr_len = send_state->addr.entry[send_state->cur_addr].addr_len;
	    pj_memcpy(&tsx->addr, 
		      &send_state->addr.entry[send_state->cur_addr].addr,
		      tsx->addr_len);

	    /* Update is_reliable flag. */
	    tsx->is_reliable = PJSIP_TRANSPORT_IS_RELIABLE(tsx->transport);
	}

	/* Clear pending transport flag. */
	tsx->transport_flag &= ~(TSX_HAS_PENDING_TRANSPORT);

	/* Mark that we have resolved the addresses. */
	tsx->transport_flag |= TSX_HAS_RESOLVED_SERVER;

	/* Pending destroy? */
	if (tsx->transport_flag & TSX_HAS_PENDING_DESTROY) {
	    tsx_set_state( tsx, PJSIP_TSX_STATE_DESTROYED, 
			   PJSIP_EVENT_UNKNOWN, NULL );
	    unlock_tsx(tsx, &lck);
	    return;
	}

	/* Need to transmit a message? */
	if (tsx->transport_flag & TSX_HAS_PENDING_SEND) {
	    tsx->transport_flag &= ~(TSX_HAS_PENDING_SEND);
	    tsx_send_msg(tsx, tsx->last_tx);
	}

	/* Need to reschedule retransmission? */
	if (tsx->transport_flag & TSX_HAS_PENDING_RESCHED) {
	    tsx->transport_flag &= ~(TSX_HAS_PENDING_RESCHED);

	    /* Only update when transport turns out to be unreliable. */
	    if (!tsx->is_reliable) {
		tsx_resched_retransmission(tsx);
	    }
	}

    } else {
	/* Failed to send! */
	pj_assert(sent != 0);

	/* If transaction is using the same transport as the failed one, 
	 * release the transport.
	 */
	if (send_state->cur_transport==tsx->transport &&
	    tsx->transport != NULL)
	{
	    pjsip_transport_dec_ref(tsx->transport);
	    tsx->transport = NULL;
	}

	if (!*cont) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    pj_str_t err;

	    tsx->transport_err = -sent;

	    err =pj_strerror(-sent, errmsg, sizeof(errmsg));

	    PJ_LOG(2,(tsx->obj_name, 
		      "Failed to send %s! err=%d (%s)",
		      pjsip_tx_data_get_info(send_state->tdata), -sent,
		      errmsg));

	    /* Clear pending transport flag. */
	    tsx->transport_flag &= ~(TSX_HAS_PENDING_TRANSPORT);

	    /* Mark that we have resolved the addresses. */
	    tsx->transport_flag |= TSX_HAS_RESOLVED_SERVER;

	    /* Terminate transaction, if it's not already terminated. */
	    tsx_set_status_code(tsx, PJSIP_SC_TSX_TRANSPORT_ERROR, &err);
	    if (tsx->state != PJSIP_TSX_STATE_TERMINATED &&
		tsx->state != PJSIP_TSX_STATE_DESTROYED)
	    {
		tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
			       PJSIP_EVENT_TRANSPORT_ERROR, send_state->tdata);
	    }

	} else {
	    char errmsg[PJ_ERR_MSG_SIZE];

	    PJ_LOG(2,(tsx->obj_name, 
		      "Temporary failure in sending %s, "
		      "will try next server. Err=%d (%s)",
		      pjsip_tx_data_get_info(send_state->tdata), -sent,
		      pj_strerror(-sent, errmsg, sizeof(errmsg)).ptr));
	}
    }

    unlock_tsx(tsx, &lck);
}


/* Transport callback. */
static void transport_callback(void *token, pjsip_tx_data *tdata,
			       pj_ssize_t sent)
{
    if (sent < 0) {
	pjsip_transaction *tsx = token;
	struct tsx_lock_data lck;
	char errmsg[PJ_ERR_MSG_SIZE];
	pj_str_t err;

	tsx->transport_err = -sent;

	err = pj_strerror(-sent, errmsg, sizeof(errmsg));

	PJ_LOG(2,(tsx->obj_name, "Transport failed to send %s! Err=%d (%s)",
		  pjsip_tx_data_get_info(tdata), -sent, errmsg));

	lock_tsx(tsx, &lck);

	/* Dereference transport. */
	pjsip_transport_dec_ref(tsx->transport);
	tsx->transport = NULL;

	/* Terminate transaction. */
	tsx_set_status_code(tsx, PJSIP_SC_TSX_TRANSPORT_ERROR, &err);
	tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
		       PJSIP_EVENT_TRANSPORT_ERROR, tdata );

	unlock_tsx(tsx, &lck);
   }
}

/*
 * Send message to the transport.
 */
static pj_status_t tsx_send_msg( pjsip_transaction *tsx, 
                                 pjsip_tx_data *tdata)
{
    pj_status_t status = PJ_SUCCESS;

    PJ_ASSERT_RETURN(tsx && tdata, PJ_EINVAL);

    /* Send later if transport is still pending. */
    if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
	tsx->transport_flag |= TSX_HAS_PENDING_SEND;
	return PJ_SUCCESS;
    }

    /* If we have the transport, send the message using that transport.
     * Otherwise perform full transport resolution.
     */
    if (tsx->transport) {
	status = pjsip_transport_send( tsx->transport, tdata, &tsx->addr,
				       tsx->addr_len, tsx, 
				       &transport_callback);
	if (status == PJ_EPENDING)
	    status = PJ_SUCCESS;

	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];

	    PJ_LOG(2,(tsx->obj_name, 
		      "Error sending %s: Err=%d (%s)",
		      pjsip_tx_data_get_info(tdata), status, 
		      pj_strerror(status, errmsg, sizeof(errmsg)).ptr));

	    /* On error, release transport to force using full transport
	     * resolution procedure.
	     */
	    if (tsx->transport) {
		pjsip_transport_dec_ref(tsx->transport);
		tsx->transport = NULL;
	    }
	    tsx->addr_len = 0;
	    tsx->res_addr.transport = NULL;
	    tsx->res_addr.addr_len = 0;
	} else {
	    return PJ_SUCCESS;
	}
    }

    /* We are here because we don't have transport, or we failed to send
     * the message using existing transport. If we haven't resolved the
     * server before, then begin the long process of resolving the server
     * and send the message with possibly new server.
     */
    pj_assert(status != PJ_SUCCESS || tsx->transport == NULL);

    /* If we have resolved the server, we treat the error as permanent error.
     * Terminate transaction with transport error failure.
     */
    if (tsx->transport_flag & TSX_HAS_RESOLVED_SERVER) {
	
	char errmsg[PJ_ERR_MSG_SIZE];
	pj_str_t err;

	if (status == PJ_SUCCESS) {
	    pj_assert(!"Unexpected status!");
	    status = PJ_EUNKNOWN;
	}

	/* We have resolved the server!.
	 * Treat this as permanent transport error.
	 */
	err = pj_strerror(status, errmsg, sizeof(errmsg));

	PJ_LOG(2,(tsx->obj_name, 
		  "Transport error, terminating transaction. "
		  "Err=%d (%s)",
		  status, errmsg));

	tsx_set_status_code(tsx, PJSIP_SC_TSX_TRANSPORT_ERROR, &err);
	tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
		       PJSIP_EVENT_TRANSPORT_ERROR, NULL );

	return status;
    }

    /* Must add reference counter because the send request functions
     * decrement the reference counter.
     */
    pjsip_tx_data_add_ref(tdata);

    /* Begin resolving destination etc to send the message. */
    if (tdata->msg->type == PJSIP_REQUEST_MSG) {

	tsx->transport_flag |= TSX_HAS_PENDING_TRANSPORT;
	status = pjsip_endpt_send_request_stateless(tsx->endpt, tdata, tsx,
						    &send_msg_callback);
	if (status == PJ_EPENDING)
	    status = PJ_SUCCESS;
	if (status != PJ_SUCCESS)
	    pjsip_tx_data_dec_ref(tdata);
	
	/* Check if transaction is terminated. */
	if (status==PJ_SUCCESS && tsx->state == PJSIP_TSX_STATE_TERMINATED)
	    status = tsx->transport_err;

    } else {

	tsx->transport_flag |= TSX_HAS_PENDING_TRANSPORT;
	status = pjsip_endpt_send_response( tsx->endpt, &tsx->res_addr, 
					    tdata, tsx, 
					    &send_msg_callback);
	if (status == PJ_EPENDING)
	    status = PJ_SUCCESS;
	if (status != PJ_SUCCESS)
	    pjsip_tx_data_dec_ref(tdata);

	/* Check if transaction is terminated. */
	if (status==PJ_SUCCESS && tsx->state == PJSIP_TSX_STATE_TERMINATED)
	    status = tsx->transport_err;

    }


    return status;
}


/*
 * Retransmit last message sent.
 */
static void tsx_resched_retransmission( pjsip_transaction *tsx )
{
    pj_time_val timeout;
    int msec_time;

    pj_assert((tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) == 0);

    msec_time = (1 << (tsx->retransmit_count)) * PJSIP_T1_TIMEOUT;

    if (tsx->role == PJSIP_ROLE_UAC) {
	/* Retransmission for non-INVITE transaction caps-off at T2 */
	if (msec_time>PJSIP_T2_TIMEOUT && tsx->method.id!=PJSIP_INVITE_METHOD)
	    msec_time = PJSIP_T2_TIMEOUT;
    } else {
	/* Retransmission of INVITE final response also caps-off at T2 */
	pj_assert(tsx->status_code >= 200);
	if (msec_time>PJSIP_T2_TIMEOUT)
	    msec_time = PJSIP_T2_TIMEOUT;
    }

    timeout.sec = msec_time / 1000;
    timeout.msec = msec_time % 1000;
    pjsip_endpt_schedule_timer( tsx->endpt, &tsx->retransmit_timer, 
				&timeout);
}

/*
 * Retransmit last message sent.
 */
static pj_status_t tsx_retransmit( pjsip_transaction *tsx, int resched)
{
    pj_status_t status;

    PJ_ASSERT_RETURN(tsx->last_tx!=NULL, PJ_EBUG);

    PJ_LOG(5,(tsx->obj_name, "Retransmiting %s, count=%d, restart?=%d", 
	      pjsip_tx_data_get_info(tsx->last_tx), 
	      tsx->retransmit_count, resched));

    ++tsx->retransmit_count;

    /* Restart timer T1 first before sending the message to ensure that
     * retransmission timer is not engaged when loop transport is used.
     */
    if (resched) {
	pj_assert(tsx->state != PJSIP_TSX_STATE_CONFIRMED);
	if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
	    tsx->transport_flag |= TSX_HAS_PENDING_RESCHED;
	} else {
	    tsx_resched_retransmission(tsx);
	}
    }

    status = tsx_send_msg( tsx, tsx->last_tx);
    if (status != PJ_SUCCESS) {
	return status;
    }

    return PJ_SUCCESS;
}


/*
 * Handler for events in state Null.
 */
static pj_status_t tsx_on_state_null( pjsip_transaction *tsx, 
                                      pjsip_event *event )
{
    pj_status_t status;

    pj_assert(tsx->state == PJSIP_TSX_STATE_NULL);

    if (tsx->role == PJSIP_ROLE_UAS) {

	/* Set state to Trying. */
	pj_assert(event->type == PJSIP_EVENT_RX_MSG &&
		  event->body.rx_msg.rdata->msg_info.msg->type == 
		    PJSIP_REQUEST_MSG);
	tsx_set_state( tsx, PJSIP_TSX_STATE_TRYING, PJSIP_EVENT_RX_MSG,
		       event->body.rx_msg.rdata);

    } else {
	pjsip_tx_data *tdata;

	/* Must be transmit event. 
	 * You may got this assertion when using loop transport with delay 
	 * set to zero. That would cause on_rx_response() callback to be 
	 * called before tsx_send_msg() has completed.
	 */
	PJ_ASSERT_RETURN(event->type == PJSIP_EVENT_TX_MSG, PJ_EBUG);

	/* Get the txdata */
	tdata = event->body.tx_msg.tdata;

	/* Save the message for retransmission. */
	if (tsx->last_tx && tsx->last_tx != tdata) {
	    pjsip_tx_data_dec_ref(tsx->last_tx);
	    tsx->last_tx = NULL;
	}
	if (tsx->last_tx != tdata) {
	    tsx->last_tx = tdata;
	    pjsip_tx_data_add_ref(tdata);
	}

	/* Send the message. */
        status = tsx_send_msg( tsx, tdata);
	if (status != PJ_SUCCESS) {
	    return status;
	}

	/* Start Timer B (or called timer F for non-INVITE) for transaction 
	 * timeout.
	 */
	pjsip_endpt_schedule_timer( tsx->endpt, &tsx->timeout_timer, 
                                    &timeout_timer_val);

	/* Start Timer A (or timer E) for retransmission only if unreliable 
	 * transport is being used.
	 */
	if (!tsx->is_reliable)  {
	    tsx->retransmit_count = 0;
	    if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
		tsx->transport_flag |= TSX_HAS_PENDING_RESCHED;
	    } else {
		pjsip_endpt_schedule_timer(tsx->endpt, &tsx->retransmit_timer,
					   &t1_timer_val);
	    }
	}

	/* Move state. */
	tsx_set_state( tsx, PJSIP_TSX_STATE_CALLING, 
                       PJSIP_EVENT_TX_MSG, tdata);
    }

    return PJ_SUCCESS;
}


/*
 * State Calling is for UAC after it sends request but before any responses
 * is received.
 */
static pj_status_t tsx_on_state_calling( pjsip_transaction *tsx, 
				         pjsip_event *event )
{
    pj_assert(tsx->state == PJSIP_TSX_STATE_CALLING);
    pj_assert(tsx->role == PJSIP_ROLE_UAC);

    if (event->type == PJSIP_EVENT_TIMER && 
	event->body.timer.entry == &tsx->retransmit_timer) 
    {
        pj_status_t status;

	/* Retransmit the request. */
        status = tsx_retransmit( tsx, 1 );
	if (status != PJ_SUCCESS) {
	    return status;
	}

    } else if (event->type == PJSIP_EVENT_TIMER && 
	       event->body.timer.entry == &tsx->timeout_timer) 
    {

	/* Cancel retransmission timer. */
	if (tsx->retransmit_timer._timer_id != -1) {
	    pjsip_endpt_cancel_timer(tsx->endpt, &tsx->retransmit_timer);
	    tsx->retransmit_timer._timer_id = -1;
	}
	tsx->transport_flag &= ~(TSX_HAS_PENDING_RESCHED);

	/* Set status code */
	tsx_set_status_code(tsx, PJSIP_SC_TSX_TIMEOUT, NULL);

	/* Inform TU. */
	tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                       PJSIP_EVENT_TIMER, &tsx->timeout_timer);

	/* Transaction is destroyed */
	//return PJSIP_ETSXDESTROYED;

    } else if (event->type == PJSIP_EVENT_RX_MSG) {
	pjsip_msg *msg;
	//int code;

	/* Get message instance */
	msg = event->body.rx_msg.rdata->msg_info.msg;

	/* Better be a response message. */
	if (msg->type != PJSIP_RESPONSE_MSG)
	    return PJSIP_ENOTRESPONSEMSG;

	/* Cancel retransmission timer A. */
	if (tsx->retransmit_timer._timer_id != -1) {
	    pjsip_endpt_cancel_timer(tsx->endpt, &tsx->retransmit_timer);
	    tsx->retransmit_timer._timer_id = -1;
	}
	tsx->transport_flag &= ~(TSX_HAS_PENDING_RESCHED);


	/* Cancel timer B (transaction timeout) */
	pjsip_endpt_cancel_timer(tsx->endpt, &tsx->timeout_timer);

	/* Discard retransmission message if it is not INVITE.
	 * The INVITE tdata is needed in case we have to generate ACK for
	 * the final response.
	 */
	/* Keep last_tx for authorization. */
	//blp: always keep last_tx until transaction is destroyed
	//code = msg->line.status.code;
	//if (tsx->method.id != PJSIP_INVITE_METHOD && code!=401 && code!=407) {
	//    pjsip_tx_data_dec_ref(tsx->last_tx);
	//    tsx->last_tx = NULL;
	//}

	/* Processing is similar to state Proceeding. */
	tsx_on_state_proceeding_uac( tsx, event);

    } else {
	pj_assert(!"Unexpected event");
        return PJ_EBUG;
    }

    return PJ_SUCCESS;
}


/*
 * State Trying is for UAS after it received request but before any responses
 * is sent.
 * Note: this is different than RFC3261, which can use Trying state for
 *	 non-INVITE client transaction (bug in RFC?).
 */
static pj_status_t tsx_on_state_trying( pjsip_transaction *tsx, 
                                        pjsip_event *event)
{
    pj_status_t status;

    pj_assert(tsx->state == PJSIP_TSX_STATE_TRYING);

    /* This state is only for UAS */
    pj_assert(tsx->role == PJSIP_ROLE_UAS);

    /* Better be transmission of response message.
     * If we've got request retransmission, this means that the TU hasn't
     * transmitted any responses within 500 ms, which is not allowed. If
     * this happens, just ignore the event (we couldn't retransmit last
     * response because we haven't sent any!).
     */
    if (event->type != PJSIP_EVENT_TX_MSG) {
	return PJ_SUCCESS;
    }

    /* The rest of the processing of the event is exactly the same as in
     * "Proceeding" state.
     */
    status = tsx_on_state_proceeding_uas( tsx, event);

    /* Inform the TU of the state transision if state is still State_Trying */
    if (status==PJ_SUCCESS && tsx->state == PJSIP_TSX_STATE_TRYING) {

	tsx_set_state( tsx, PJSIP_TSX_STATE_PROCEEDING, 
                       PJSIP_EVENT_TX_MSG, event->body.tx_msg.tdata);

    }

    return status;
}


/*
 * Handler for events in Proceeding for UAS
 * This state happens after the TU sends provisional response.
 */
static pj_status_t tsx_on_state_proceeding_uas( pjsip_transaction *tsx,
                                                pjsip_event *event)
{
    pj_assert(tsx->state == PJSIP_TSX_STATE_PROCEEDING || 
	      tsx->state == PJSIP_TSX_STATE_TRYING);

    /* This state is only for UAS. */
    pj_assert(tsx->role == PJSIP_ROLE_UAS);

    /* Receive request retransmission. */
    if (event->type == PJSIP_EVENT_RX_MSG) {

        pj_status_t status;

	/* Must have last response sent. */
	PJ_ASSERT_RETURN(tsx->last_tx != NULL, PJ_EBUG);

	/* Send last response */
	if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
	    tsx->transport_flag |= TSX_HAS_PENDING_SEND;
	} else {
	    status = tsx_send_msg(tsx, tsx->last_tx);
	    if (status != PJ_SUCCESS)
		return status;
	}
	
    } else if (event->type == PJSIP_EVENT_TX_MSG ) {
	pjsip_tx_data *tdata = event->body.tx_msg.tdata;
        pj_status_t status;

	/* The TU sends response message to the request. Save this message so
	 * that we can retransmit the last response in case we receive request
	 * retransmission.
	 */
	pjsip_msg *msg = tdata->msg;

	/* This can only be a response message. */
	PJ_ASSERT_RETURN(msg->type==PJSIP_RESPONSE_MSG, PJSIP_ENOTRESPONSEMSG);

	/* Update last status */
	tsx_set_status_code(tsx, msg->line.status.code, 
			    &msg->line.status.reason);

	/* Discard the saved last response (it will be updated later as
	 * necessary).
	 */
	if (tsx->last_tx && tsx->last_tx != tdata) {
	    pjsip_tx_data_dec_ref( tsx->last_tx );
	    tsx->last_tx = NULL;
	}

	/* Send the message. */
        status = tsx_send_msg(tsx, tdata);
	if (status != PJ_SUCCESS) {
	    return status;
	}

	// Update To tag header for RFC2543 transaction.
	// TODO:

	/* Update transaction state */
	if (PJSIP_IS_STATUS_IN_CLASS(tsx->status_code, 100)) {

	    if (tsx->last_tx != tdata) {
		tsx->last_tx = tdata;
		pjsip_tx_data_add_ref( tdata );
	    }

	    tsx_set_state( tsx, PJSIP_TSX_STATE_PROCEEDING, 
                           PJSIP_EVENT_TX_MSG, tdata );

	} else if (PJSIP_IS_STATUS_IN_CLASS(tsx->status_code, 200)) {

	    if (tsx->method.id == PJSIP_INVITE_METHOD && tsx->handle_200resp==0) {

		/* 2xx class message is not saved, because retransmission 
                 * is handled by TU.
		 */
		tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                               PJSIP_EVENT_TX_MSG, tdata );

		/* Transaction is destroyed. */
		//return PJSIP_ETSXDESTROYED;

	    } else {
		pj_time_val timeout;

		if (tsx->method.id == PJSIP_INVITE_METHOD) {
		    tsx->retransmit_count = 0;
		    if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
			tsx->transport_flag |= TSX_HAS_PENDING_RESCHED;
		    } else {
			pjsip_endpt_schedule_timer( tsx->endpt, 
						    &tsx->retransmit_timer,
						    &t1_timer_val);
		    }
		}

		/* Save last response sent for retransmission when request 
		 * retransmission is received.
		 */
		if (tsx->last_tx != tdata) {
		    tsx->last_tx = tdata;
		    pjsip_tx_data_add_ref(tdata);
		}

		/* Setup timeout timer: */
		
		if (tsx->method.id == PJSIP_INVITE_METHOD) {
		    
		    /* Start Timer H at 64*T1 for INVITE server transaction,
		     * regardless of transport.
		     */
		    timeout = timeout_timer_val;
		    
		} else if (PJSIP_TRANSPORT_IS_RELIABLE(tsx->transport)==0) {
		    
		    /* For non-INVITE, start timer J at 64*T1 for unreliable
		     * transport.
		     */
		    timeout = timeout_timer_val;
		    
		} else {
		    
		    /* Transaction terminates immediately for non-INVITE when
		     * reliable transport is used.
		     */
		    timeout.sec = timeout.msec = 0;
		}

		pjsip_endpt_schedule_timer( tsx->endpt, &tsx->timeout_timer, 
                                            &timeout);

		/* Set state to "Completed" */
		tsx_set_state( tsx, PJSIP_TSX_STATE_COMPLETED, 
                               PJSIP_EVENT_TX_MSG, tdata );
	    }

	} else if (tsx->status_code >= 300) {

	    /* 3xx-6xx class message causes transaction to move to 
             * "Completed" state. 
             */
	    if (tsx->last_tx != tdata) {
		tsx->last_tx = tdata;
		pjsip_tx_data_add_ref( tdata );
	    }

	    /* For INVITE, start timer H for transaction termination 
	     * regardless whether transport is reliable or not.
	     * For non-INVITE, start timer J with the value of 64*T1 for
	     * non-reliable transports, and zero for reliable transports.
	     */
	    if (tsx->method.id == PJSIP_INVITE_METHOD) {
		/* Start timer H for INVITE */
		pjsip_endpt_schedule_timer(tsx->endpt,&tsx->timeout_timer,
					   &timeout_timer_val);
	    } else if (!tsx->is_reliable) {
		/* Start timer J on 64*T1 seconds for non-INVITE */
		pjsip_endpt_schedule_timer(tsx->endpt,&tsx->timeout_timer,
					   &timeout_timer_val);
	    } else {
		/* Start timer J on zero seconds for non-INVITE */
		pj_time_val zero_time = { 0, 0 };
		pjsip_endpt_schedule_timer(tsx->endpt,&tsx->timeout_timer,
					   &zero_time);
	    }

	    /* For INVITE, if unreliable transport is used, retransmission 
	     * timer G will be scheduled (retransmission).
	     */
	    if (!tsx->is_reliable) {
		pjsip_cseq_hdr *cseq = pjsip_msg_find_hdr( msg, PJSIP_H_CSEQ,
                                                           NULL);
		if (cseq->method.id == PJSIP_INVITE_METHOD) {
		    tsx->retransmit_count = 0;
		    if (tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) {
			tsx->transport_flag |= TSX_HAS_PENDING_RESCHED;
		    } else {
			pjsip_endpt_schedule_timer(tsx->endpt, 
						   &tsx->retransmit_timer, 
						   &t1_timer_val);
		    }
		}
	    }

	    /* Inform TU */
	    tsx_set_state( tsx, PJSIP_TSX_STATE_COMPLETED, 
                           PJSIP_EVENT_TX_MSG, tdata );

	} else {
	    pj_assert(0);
	}


    } else if (event->type == PJSIP_EVENT_TIMER && 
	       event->body.timer.entry == &tsx->retransmit_timer) {

	/* Retransmission timer elapsed. */
        pj_status_t status;

	/* Must not be triggered while transport is pending. */
	pj_assert((tsx->transport_flag & TSX_HAS_PENDING_TRANSPORT) == 0);

	/* Must have last response to retransmit. */
	pj_assert(tsx->last_tx != NULL);

	/* Retransmit the last response. */
        status = tsx_retransmit( tsx, 1 );
	if (status != PJ_SUCCESS) {
	    return status;
	}

    } else if (event->type == PJSIP_EVENT_TIMER && 
	       event->body.timer.entry == &tsx->timeout_timer) {

	/* Timeout timer. should not happen? */
	pj_assert(!"Should not happen(?)");

	tsx_set_status_code(tsx, PJSIP_SC_TSX_TIMEOUT, NULL);

	tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                       PJSIP_EVENT_TIMER, &tsx->timeout_timer);

	return PJ_EBUG;

    } else {
	pj_assert(!"Unexpected event");
        return PJ_EBUG;
    }

    return PJ_SUCCESS;
}


/*
 * Handler for events in Proceeding for UAC
 * This state happens after provisional response(s) has been received from
 * UAS.
 */
static pj_status_t tsx_on_state_proceeding_uac(pjsip_transaction *tsx, 
                                               pjsip_event *event)
{

    pj_assert(tsx->state == PJSIP_TSX_STATE_PROCEEDING || 
	      tsx->state == PJSIP_TSX_STATE_CALLING);

    if (event->type != PJSIP_EVENT_TIMER) {
	pjsip_msg *msg;

	/* Must be incoming response, because we should not retransmit
	 * request once response has been received.
	 */
	pj_assert(event->type == PJSIP_EVENT_RX_MSG);
	if (event->type != PJSIP_EVENT_RX_MSG) {
	    return PJ_EINVALIDOP;
	}

	msg = event->body.rx_msg.rdata->msg_info.msg;

	/* Must be a response message. */
	if (msg->type != PJSIP_RESPONSE_MSG) {
	    pj_assert(!"Expecting response message!");
	    return PJSIP_ENOTRESPONSEMSG;
	}

	tsx_set_status_code(tsx, msg->line.status.code, 
			    &msg->line.status.reason);

    } else {
	tsx_set_status_code(tsx, PJSIP_SC_TSX_TIMEOUT, NULL);
    }

    if (PJSIP_IS_STATUS_IN_CLASS(tsx->status_code, 100)) {

	/* Inform the message to TU. */
	tsx_set_state( tsx, PJSIP_TSX_STATE_PROCEEDING, 
                       PJSIP_EVENT_RX_MSG, event->body.rx_msg.rdata );

    } else if (PJSIP_IS_STATUS_IN_CLASS(tsx->status_code,200)) {

	/* Stop timeout timer B/F. */
	pjsip_endpt_cancel_timer( tsx->endpt, &tsx->timeout_timer );

	/* For INVITE, the state moves to Terminated state (because ACK is
	 * handled in TU). For non-INVITE, state moves to Completed.
	 */
	if (tsx->method.id == PJSIP_INVITE_METHOD) {
	    tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                           PJSIP_EVENT_RX_MSG, event->body.rx_msg.rdata );
	    //return PJSIP_ETSXDESTROYED;

	} else {
	    pj_time_val timeout;

	    /* For unreliable transport, start timer D (for INVITE) or 
	     * timer K for non-INVITE. */
	    if (!tsx->is_reliable) {
		if (tsx->method.id == PJSIP_INVITE_METHOD) {
		    timeout = td_timer_val;
		} else {
		    timeout = t4_timer_val;
		}
	    } else {
		timeout.sec = timeout.msec = 0;
	    }
	    pjsip_endpt_schedule_timer( tsx->endpt, &tsx->timeout_timer, 
					&timeout);

	    /* Move state to Completed, inform TU. */
	    tsx_set_state( tsx, PJSIP_TSX_STATE_COMPLETED, 
                           PJSIP_EVENT_RX_MSG, event->body.rx_msg.rdata );
	}

    } else if (tsx->status_code >= 300 && tsx->status_code <= 699) {


#if 0
	/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
	/*
	 * This is the old code; it's broken for authentication.
	 */
	pj_time_val timeout;
        pj_status_t status;

	/* Stop timer B. */
	pjsip_endpt_cancel_timer( tsx->endpt, &tsx->timeout_timer );

	/* Generate and send ACK for INVITE. */
	if (tsx->method.id == PJSIP_INVITE_METHOD) {
	    pjsip_tx_data *ack;

	    status = pjsip_endpt_create_ack( tsx->endpt, tsx->last_tx, 
					     event->body.rx_msg.rdata,
					     &ack);
	    if (status != PJ_SUCCESS)
		return status;

	    if (ack != tsx->last_tx) {
		pjsip_tx_data_dec_ref(tsx->last_tx);
		tsx->last_tx = ack;
	    }

            status = tsx_send_msg( tsx, tsx->last_tx);
	    if (status != PJ_SUCCESS) {
		return status;
	    }
	}

	/* Start Timer D with TD/T4 timer if unreliable transport is used. */
	if (!tsx->is_reliable) {
	    if (tsx->method.id == PJSIP_INVITE_METHOD) {
		timeout = td_timer_val;
	    } else {
		timeout = t4_timer_val;
	    }
	} else {
	    timeout.sec = timeout.msec = 0;
	}
	pjsip_endpt_schedule_timer( tsx->endpt, &tsx->timeout_timer, &timeout);

	/* Inform TU. 
	 * blp: You might be tempted to move this notification before
	 *      sending ACK, but I think you shouldn't. Better set-up
	 *      everything before calling tsx_user's callback to avoid
	 *      mess up.
	 */
	tsx_set_state( tsx, PJSIP_TSX_STATE_COMPLETED, 
                       PJSIP_EVENT_RX_MSG, event->body.rx_msg.rdata );

	/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
#endif

	/* New code, taken from 0.2.9.x branch */
	pj_time_val timeout;
	pjsip_tx_data *ack_tdata = NULL;

	/* Stop timer B. */
	pjsip_endpt_cancel_timer( tsx->endpt, &tsx->timeout_timer );

	/* Generate ACK now (for INVITE) but send it later because
	 * dialog need to use last_tx.
	 */
	if (tsx->method.id == PJSIP_INVITE_METHOD) {
	    pj_status_t status;

	    status = pjsip_endpt_create_ack( tsx->endpt, tsx->last_tx, 
					     event->body.rx_msg.rdata,
					     &ack_tdata);
	    if (status != PJ_SUCCESS)
		return status;
	}

	/* Inform TU. */
	tsx_set_state( tsx, PJSIP_TSX_STATE_COMPLETED, 
		       PJSIP_EVENT_RX_MSG, event->body.rx_msg.rdata);

	/* Generate and send ACK for INVITE. */
	if (tsx->method.id == PJSIP_INVITE_METHOD) {
	    pj_status_t status;

	    status = tsx_send_msg( tsx, ack_tdata);

	    if (ack_tdata != tsx->last_tx) {
		pjsip_tx_data_dec_ref(tsx->last_tx);
		tsx->last_tx = ack_tdata;

		/* This is a bug.
		   tsx_send_msg() does NOT decrement tdata's reference counter,
		   so if we add the reference counter here, tdata will have
		   reference counter 2, causing it to leak.
		pjsip_tx_data_add_ref(ack_tdata);
		*/
	    }

	    if (status != PJ_SUCCESS) {
		return status;
	    }
	}

	/* Start Timer D with TD/T4 timer if unreliable transport is used. */
	/* Note: tsx->transport may be NULL! */
	if ((tsx->transport && PJSIP_TRANSPORT_IS_RELIABLE(tsx->transport)==0)
	    || ((tsx->transport_flag & PJSIP_TRANSPORT_RELIABLE) == 0)) 
	{
	    if (tsx->method.id == PJSIP_INVITE_METHOD) {
		timeout = td_timer_val;
	    } else {
		timeout = t4_timer_val;
	    }
	} else {
	    timeout.sec = timeout.msec = 0;
	}
	pjsip_endpt_schedule_timer( tsx->endpt, &tsx->timeout_timer, &timeout);

    } else {
	// Shouldn't happen because there's no timer for this state.
	pj_assert(!"Unexpected event");
        return PJ_EBUG;
    }

    return PJ_SUCCESS;
}


/*
 * Handler for events in Completed state for UAS
 */
static pj_status_t tsx_on_state_completed_uas( pjsip_transaction *tsx, 
                                               pjsip_event *event)
{
    pj_assert(tsx->state == PJSIP_TSX_STATE_COMPLETED);

    if (event->type == PJSIP_EVENT_RX_MSG) {
	pjsip_msg *msg = event->body.rx_msg.rdata->msg_info.msg;

	/* This must be a request message retransmission. */
	if (msg->type != PJSIP_REQUEST_MSG)
	    return PJSIP_ENOTREQUESTMSG;

	/* On receive request retransmission, retransmit last response. */
	if (msg->line.req.method.id != PJSIP_ACK_METHOD) {
            pj_status_t status;

            status = tsx_retransmit( tsx, 0 );
	    if (status != PJ_SUCCESS) {
		return status;
	    }

	} else {
	    /* Process incoming ACK request. */

	    /* Cease retransmission. */
	    if (tsx->retransmit_timer._timer_id != -1) {
		pjsip_endpt_cancel_timer(tsx->endpt, &tsx->retransmit_timer);
		tsx->retransmit_timer._timer_id = -1;
	    }
	    tsx->transport_flag &= ~(TSX_HAS_PENDING_RESCHED);

	    /* Start timer I in T4 interval (transaction termination). */
	    pjsip_endpt_cancel_timer( tsx->endpt, &tsx->timeout_timer );
	    pjsip_endpt_schedule_timer( tsx->endpt, &tsx->timeout_timer, 
					&t4_timer_val);

	    /* Move state to "Confirmed" */
	    tsx_set_state( tsx, PJSIP_TSX_STATE_CONFIRMED, 
                           PJSIP_EVENT_RX_MSG, event->body.rx_msg.rdata );
	}	

    } else if (event->type == PJSIP_EVENT_TIMER) {

	if (event->body.timer.entry == &tsx->retransmit_timer) {
	    /* Retransmit message. */
            pj_status_t status;

            status = tsx_retransmit( tsx, 1 );
	    if (status != PJ_SUCCESS) {
		return status;
	    }

	} else {
	    if (tsx->method.id == PJSIP_INVITE_METHOD) {

		/* For INVITE, this means that ACK was never received.
		 * Set state to Terminated, and inform TU.
		 */

		tsx_set_status_code(tsx, PJSIP_SC_TSX_TIMEOUT, NULL);

		tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                               PJSIP_EVENT_TIMER, &tsx->timeout_timer );

		//return PJSIP_ETSXDESTROYED;

	    } else {
		/* Transaction terminated, it can now be deleted. */
		tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                               PJSIP_EVENT_TIMER, &tsx->timeout_timer );
		//return PJSIP_ETSXDESTROYED;
	    }
	}

    } else {
	/* Ignore request to transmit. */
	PJ_ASSERT_RETURN(event->type == PJSIP_EVENT_TX_MSG && 
			 event->body.tx_msg.tdata == tsx->last_tx, 
			 PJ_EINVALIDOP);
    }

    return PJ_SUCCESS;
}


/*
 * Handler for events in Completed state for UAC transaction.
 */
static pj_status_t tsx_on_state_completed_uac( pjsip_transaction *tsx,
                                               pjsip_event *event)
{
    pj_assert(tsx->state == PJSIP_TSX_STATE_COMPLETED);

    if (event->type == PJSIP_EVENT_TIMER) {
	/* Must be the timeout timer. */
	pj_assert(event->body.timer.entry == &tsx->timeout_timer);

	/* Move to Terminated state. */
	tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                       PJSIP_EVENT_TIMER, event->body.timer.entry );

	/* Transaction has been destroyed. */
	//return PJSIP_ETSXDESTROYED;

    } else if (event->type == PJSIP_EVENT_RX_MSG) {
	if (tsx->method.id == PJSIP_INVITE_METHOD) {
	    /* On received of final response retransmission, retransmit the ACK.
	     * TU doesn't need to be informed.
	     */
	    pjsip_msg *msg = event->body.rx_msg.rdata->msg_info.msg;
	    pj_assert(msg->type == PJSIP_RESPONSE_MSG);
	    if (msg->type==PJSIP_RESPONSE_MSG &&
		msg->line.status.code >= 200)
	    {
                pj_status_t status;

                status = tsx_retransmit( tsx, 0 );
		if (status != PJ_SUCCESS) {
		    return status;
		}
	    } else {
		/* Very late retransmission of privisional response. */
		pj_assert( msg->type == PJSIP_RESPONSE_MSG );
	    }
	} else {
	    /* Just drop the response. */
	}

    } else {
	pj_assert(!"Unexpected event");
        return PJ_EINVALIDOP;
    }

    return PJ_SUCCESS;
}


/*
 * Handler for events in state Confirmed.
 */
static pj_status_t tsx_on_state_confirmed( pjsip_transaction *tsx,
                                           pjsip_event *event)
{
    pj_assert(tsx->state == PJSIP_TSX_STATE_CONFIRMED);

    /* This state is only for UAS for INVITE. */
    pj_assert(tsx->role == PJSIP_ROLE_UAS);
    pj_assert(tsx->method.id == PJSIP_INVITE_METHOD);

    /* Absorb any ACK received. */
    if (event->type == PJSIP_EVENT_RX_MSG) {

	pjsip_msg *msg = event->body.rx_msg.rdata->msg_info.msg;

	/* Only expecting request message. */
	if (msg->type != PJSIP_REQUEST_MSG)
	    return PJSIP_ENOTREQUESTMSG;

	/* Must be an ACK request or a late INVITE retransmission. */
	pj_assert(msg->line.req.method.id == PJSIP_ACK_METHOD || 
		  msg->line.req.method.id == PJSIP_INVITE_METHOD);

    } else if (event->type == PJSIP_EVENT_TIMER) {
	/* Must be from timeout_timer_. */
	pj_assert(event->body.timer.entry == &tsx->timeout_timer);

	/* Move to Terminated state. */
	tsx_set_state( tsx, PJSIP_TSX_STATE_TERMINATED, 
                       PJSIP_EVENT_TIMER, &tsx->timeout_timer );

	/* Transaction has been destroyed. */
	//return PJSIP_ETSXDESTROYED;

    } else {
	pj_assert(!"Unexpected event");
        return PJ_EBUG;
    }

    return PJ_SUCCESS;
}


/*
 * Handler for events in state Terminated.
 */
static pj_status_t tsx_on_state_terminated( pjsip_transaction *tsx,
                                            pjsip_event *event)
{
    pj_assert(tsx->state == PJSIP_TSX_STATE_TERMINATED);
    pj_assert(event->type == PJSIP_EVENT_TIMER);

    /* Destroy this transaction */
    tsx_set_state(tsx, PJSIP_TSX_STATE_DESTROYED, 
                  event->type, event->body.user.user1 );

    return PJ_SUCCESS;
}


/*
 * Handler for events in state Destroyed.
 * Shouldn't happen!
 */
static pj_status_t tsx_on_state_destroyed(pjsip_transaction *tsx,
                                          pjsip_event *event)
{
    PJ_UNUSED_ARG(tsx);
    PJ_UNUSED_ARG(event);
    pj_assert(!"Not expecting any events!!");
    return PJ_EBUG;
}

