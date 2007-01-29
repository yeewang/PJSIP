/* $Id */
/* 
 * Copyright (C) 2003-2005 Benny Prijono <benny@prijono.org>
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
#ifndef __PJ_STUN_MSG_H__
#define __PJ_STUN_MSG_H__

/**
 * @file stun_msg.h
 * @brief STUN message components.
 */

#include <pjlib-util/types.h>
#include <pj/sock.h>

/**
 * @defgroup PJLIB_UTIL_STUN Simple Traversal Underneath NAT (STUN)
 * @ingroup PJLIB_UTIL
 */

PJ_BEGIN_DECL


/* **************************************************************************/
/**
 * @defgroup PJLIB_UTIL_STUN_MSG STUN Message Representation and Parsing
 * @brief Low-level representation and parsing of STUN messages.
 * @ingroup PJLIB_UTIL_STUN
 * @{
 */


/**
 * The default initial STUN round-trip time estimation (the RTO value
 * in RFC 3489-bis), in miliseconds. 
 * This value is used to control the STUN request 
 * retransmit time. The initial value of retransmission interval 
 * would be set to this value, and will be doubled after each
 * retransmission.
 */
#ifndef PJ_STUN_RTO_VALUE
#   define PJ_STUN_RTO_VALUE		    100
#endif


/**
 * The STUN transaction timeout value, in miliseconds.
 * After the last retransmission is sent and if no response is received 
 * after this time, the STUN transaction will be considered to have failed.
 *
 * The default value is 1600 miliseconds (as per RFC 3489-bis).
 */
#ifndef PJ_STUN_TIMEOUT_VALUE
#   define PJ_STUN_TIMEOUT_VALUE	    1600
#endif


/**
 * Maximum number of STUN retransmission count.
 *
 * Default: 7 (as per RFC 3489-bis)
 */
#ifndef PJ_STUN_MAX_RETRANSMIT_COUNT
#   define PJ_STUN_MAX_RETRANSMIT_COUNT	    7
#endif


/**
 * Maximum size of STUN message.
 */
#ifndef PJ_STUN_MAX_PKT_LEN
#   define PJ_STUN_MAX_PKT_LEN		    512
#endif


/**
 * Default STUN port as defined by RFC 3489.
 */
#define PJ_STUN_PORT			    3478

/**
 * STUN magic cookie.
 */
#define PJ_STUN_MAGIC			    0x2112A442


/**
 * STUN method constants.
 */
enum pj_stun_method_e
{
    /**
     * STUN Binding method as defined by RFC 3489-bis.
     */
    PJ_STUN_BINDING_METHOD		    = 1,

    /**
     * STUN Shared Secret method as defined by RFC 3489-bis.
     */
    PJ_STUN_SHARED_SECRET_METHOD	    = 2,

    /**
     * STUN/TURN Allocate method as defined by draft-ietf-behave-turn
     */
    PJ_STUN_ALLOCATE_METHOD		    = 3,

    /**
     * STUN/TURN Send Indication as defined by draft-ietf-behave-turn
     */
    PJ_STUN_SEND_INDICATION_METHOD	    = 4,

    /**
     * STUN/TURN Data Indication as defined by draft-ietf-behave-turn
     */
    PJ_STUN_DATA_INDICATION_METHOD	    = 5,

    /**
     * STUN/TURN Set Active Destination as defined by draft-ietf-behave-turn
     */
    PJ_STUN_SET_ACTIVE_DESTINATION_METHOD   = 6,

    /**
     * STUN/TURN Connect method as defined by draft-ietf-behave-turn
     */
    PJ_STUN_CONNECT_METHOD		    = 7,

    /**
     * STUN/TURN Connect Status indication method.
     */
    PJ_STUN_CONNECT_STATUS_METHOD	    = 8
};


/**
 * Retrieve the STUN method from the message-type field of the STUN
 * message.
 */
#define PJ_STUN_GET_METHOD(msg_type)	((msg_type) & 0x000F)


/**
 * STUN message classes constants.
 */
enum pj_stun_msg_class_e
{
    /**
     * This specifies that the message type is a STUN request message.
     */
    PJ_STUN_REQUEST_CLASS	    = 0,

    /**
     * This specifies that the message type is a STUN indication message.
     */
    PJ_STUN_INDICATION_CLASS	    = 1,

    /**
     * This specifies that the message type is a STUN successful response.
     */
    PJ_STUN_SUCCESS_CLASS	    = 2,

    /**
     * This specifies that the message type is a STUN error response.
     */
    PJ_STUN_ERROR_CLASS		    = 3
};


/**
 * Determine if the message type is a request.
 */
#define PJ_STUN_IS_REQUEST(msg_type)	(((msg_type) & 0x0F00) == 0x0000)


/**
 * Determine if the message type is a response.
 */
#define PJ_STUN_IS_RESPONSE(msg_type)	(((msg_type) & 0x0F00) == 0x0100)


/**
 * Determine if the message type is an error response.
 */
#define PJ_STUN_IS_ERROR_RESPONSE(msg_type) (((msg_type) & 0x0FF0) == 0x0110)


#if 0
/**
 * Determine if the message type is an indication message.
 */
#define PJ_STUN_IS_INDICATION(msg_type)	(((msg_type) & 0x0FF0) == 0x0010)
#endif


/**
 * This enumeration describes STUN message types.
 */
typedef enum pj_stun_msg_type
{
    /**
     * STUN BINDING request.
     */
    PJ_STUN_BINDING_REQUEST		    = 0x0001,

    /**
     * Successful response to STUN BINDING-REQUEST.
     */
    PJ_STUN_BINDING_RESPONSE		    = 0x0101,

    /**
     * Error response to STUN BINDING-REQUEST.
     */
    PJ_STUN_BINDING_ERROR_RESPONSE	    = 0x0111,

    /**
     * STUN SHARED-SECRET reqeust.
     */
    PJ_STUN_SHARED_SECRET_REQUEST	    = 0x0002,

    /**
     * Successful response to STUN SHARED-SECRET reqeust.
     */
    PJ_STUN_SHARED_SECRET_RESPONSE	    = 0x0102,

    /**
     * Error response to STUN SHARED-SECRET reqeust.
     */
    PJ_STUN_SHARED_SECRET_ERROR_RESPONSE    = 0x0112,

    /**
     * STUN/TURN Allocate Request
     */
    PJ_STUN_ALLOCATE_REQUEST		    = 0x0003,

    /**
     * Successful response to STUN/TURN Allocate Request
     */
    PJ_STUN_ALLOCATE_RESPONSE		    = 0x0103,

    /**
     * Failure response to STUN/TURN Allocate Request
     */
    PJ_STUN_ALLOCATE_ERROR_RESPONSE	    = 0x0113,

    /**
     * STUN/TURN Send Indication
     */
    PJ_STUN_SEND_INDICATION		    = 0x0004,

    /**
     * STUN/TURN Data Indication
     */
    PJ_STUN_DATA_INDICATION		    = 0x0115,

    /**
     * STUN/TURN Set Active Destination Request
     */
    PJ_STUN_SET_ACTIVE_DESTINATION_REQUEST  = 0x0006,

    /** 
     * STUN/TURN Set Active Destination Response
     */
    PJ_STUN_SET_ACTIVE_DESTINATION_RESPONSE = 0x0106,

    /**
     * STUN/TURN Set Active Destination Error Response
     */
    PJ_STUN_SET_ACTIVE_DESTINATION_ERROR_RESPONSE = 0x0116,

    /**
     * STUN/TURN Connect Request
     */
    PJ_STUN_CONNECT_REQUEST		    = 0x0007,

    /**
     * STUN/TURN Connect Response
     */
    PJ_STUN_CONNECT_RESPONSE		    = 0x0107,

    /**
     * STUN/TURN Connect Error Response
     */
    PJ_STUN_CONNECT_ERROR_RESPONSE	    = 0x0117,

    /**
     * STUN/TURN Connect Status Indication
     */
    PJ_STUN_CONNECT_STATUS_INDICATION	    = 0x0118


} pj_stun_msg_type;



/**
 * This enumeration describes STUN attribute types.
 */
typedef enum pj_stun_attr_type
{
    PJ_STUN_ATTR_MAPPED_ADDR	    = 0x0001,/**< MAPPED-ADDRESS.	    */
    PJ_STUN_ATTR_RESPONSE_ADDR	    = 0x0002,/**< RESPONSE-ADDRESS (deprcatd)*/
    PJ_STUN_ATTR_CHANGE_REQUEST	    = 0x0003,/**< CHANGE-REQUEST (deprecated)*/
    PJ_STUN_ATTR_SOURCE_ADDR	    = 0x0004,/**< SOURCE-ADDRESS (deprecated)*/
    PJ_STUN_ATTR_CHANGED_ADDR	    = 0x0005,/**< CHANGED-ADDRESS (deprecatd)*/
    PJ_STUN_ATTR_USERNAME	    = 0x0006,/**< USERNAME attribute.	    */
    PJ_STUN_ATTR_PASSWORD	    = 0x0007,/**< PASSWORD attribute.	    */
    PJ_STUN_ATTR_MESSAGE_INTEGRITY  = 0x0008,/**< MESSAGE-INTEGRITY.	    */
    PJ_STUN_ATTR_ERROR_CODE	    = 0x0009,/**< ERROR-CODE.		    */
    PJ_STUN_ATTR_UNKNOWN_ATTRIBUTES = 0x000A,/**< UNKNOWN-ATTRIBUTES.	    */
    PJ_STUN_ATTR_REFLECTED_FROM	    = 0x000B,/**< REFLECTED-FROM (deprecatd)*/
    PJ_STUN_ATTR_LIFETIME	    = 0x000D,/**< LIFETIME attribute.	    */
    PJ_STUN_ATTR_BANDWIDTH	    = 0x0010,/**< BANDWIDTH attribute	    */
    PJ_STUN_ATTR_REMOTE_ADDRESS	    = 0x0012,/**< REMOTE-ADDRESS attribute  */
    PJ_STUN_ATTR_DATA		    = 0x0013,/**< DATA attribute.	    */
    PJ_STUN_ATTR_REALM		    = 0x0014,/**< REALM attribute.	    */
    PJ_STUN_ATTR_NONCE		    = 0x0015,/**< NONCE attribute.	    */
    PJ_STUN_ATTR_RELAY_ADDRESS	    = 0x0016,/**< RELAY-ADDRESS attribute.  */
    PJ_STUN_ATTR_REQUESTED_PORT_PROPS=0x0018,/**< REQUESTED-PORT-PROPS	    */
    PJ_STUN_ATTR_REQUESTED_TRANSPORT= 0x0019,/**< REQUESTED-TRANSPORT	    */
    PJ_STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020,/**< XOR-MAPPED-ADDRESS	    */
    PJ_STUN_ATTR_TIMER_VAL	    = 0x0021,/**< TIMER-VAL attribute.	    */
    PJ_STUN_ATTR_REQUESTED_IP	    = 0x0022,/**< REQUESTED-IP attribute    */
    PJ_STUN_ATTR_FINGERPRINT	    = 0x8021,/**< FINGERPRINT attribute.    */
    PJ_STUN_ATTR_SERVER		    = 0x8022,/**< SERVER attribute.	    */
    PJ_STUN_ATTR_ALTERNATE_SERVER   = 0x8023,/**< ALTERNATE-SERVER.	    */
    PJ_STUN_ATTR_REFRESH_INTERVAL   = 0x8024 /**< REFRESH-INTERVAL.	    */
} pj_stun_attr_type;


/**
 * STUN error codes.
 */
typedef enum pj_stun_status
{
    PJ_STUN_STATUS_TRY_ALTERNATE	    = 300,  /**< Try Alternate	    */
    PJ_STUN_STATUS_BAD_REQUEST		    = 400,  /**< Bad Request	    */
    PJ_STUN_STATUS_UNAUTHORIZED		    = 401,  /**< Unauthorized	    */
    PJ_STUN_STATUS_UNKNOWN_ATTRIBUTE	    = 420,  /**< Unknown Attribute  */
    PJ_STUN_STATUS_STALE_CREDENTIALS	    = 430,  /**< Stale Credentials  */
    PJ_STUN_STATUS_INTEGRITY_CHECK_FAILURE  = 431,  /**< Integrity Chk Fail */
    PJ_STUN_STATUS_MISSING_USERNAME	    = 432,  /**< Missing Username   */
    PJ_STUN_STATUS_USE_TLS		    = 433,  /**< Use TLS	    */
    PJ_STUN_STATUS_MISSING_REALM	    = 434,  /**< Missing Realm	    */
    PJ_STUN_STATUS_MISSING_NONCE	    = 435,  /**< Missing Nonce	    */
    PJ_STUN_STATUS_UNKNOWN_USERNAME	    = 436,  /**< Unknown Username   */
    PJ_STUN_STATUS_NO_BINDING		    = 437,  /**< No Binding.	    */
    PJ_STUN_STATUS_STALE_NONCE		    = 438,  /**< Stale Nonce	    */
    PJ_STUN_STATUS_TRANSITIONING	    = 439,  /**< Transitioning.	    */
    PJ_STUN_STATUS_WRONG_USERNAME	    = 441,  /**< Wrong Username.    */
    PJ_STUN_STATUS_UNSUPP_TRANSPORT_PROTO   = 442,  /**< Unsupported Transport Protocol */
    PJ_STUN_STATUS_INVALID_IP_ADDR	    = 443,  /**< Invalid IP Address */
    PJ_STUN_STATUS_INVALID_PORT		    = 444,  /**< Invalid Port	    */
    PJ_STUN_STATUS_OPER_TCP_ONLY	    = 445,  /**< Operation for TCP Only */
    PJ_STUN_STATUS_CONNECTION_FAILURE	    = 446,  /**< Connection Failure */
    PJ_STUN_STATUS_CONNECTION_TIMEOUT	    = 447,  /**< Connection Timeout */
    PJ_STUN_STATUS_SERVER_ERROR		    = 500,  /**< Server Error	    */
    PJ_STUN_STATUS_GLOBAL_FAILURE	    = 600   /**< Global Failure	    */
} pj_stun_status;


/**
 * This structure describes STUN message header. A STUN message has the 
 * following format:
 *
 * \verbatim

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |0 0|     STUN Message Type     |         Message Length        |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         Magic Cookie                          |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                                Transaction ID
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                                                                       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   \endverbatim
 */
typedef struct pj_stun_msg_hdr
{
    /**
     * STUN message type, which the first two bits must be zeroes.
     */
    pj_uint16_t		type;

    /**
     * The message length is the size, in bytes, of the message not
     * including the 20 byte STUN header.
     */
    pj_uint16_t		length;

    /**
     * The magic cookie is a fixed value, 0x2112A442 (PJ_STUN_MAGIC constant).
     * In the previous version of this specification [15] this field was part 
     * of the transaction ID.
     */
    pj_uint32_t		magic;

    /**
     * The transaction ID is a 96 bit identifier.  STUN transactions are
     * identified by their unique 96-bit transaction ID.  For request/
     * response transactions, the transaction ID is chosen by the STUN
     * client and MUST be unique for each new STUN transaction generated by
     * that STUN client.  The transaction ID MUST be uniformly and randomly
     * distributed between 0 and 2**96 - 1. 
     */
    pj_uint8_t		tsx_id[12];

} pj_stun_msg_hdr;


/**
 * This structre describes STUN attribute header. Each attribute is
 * TLV encoded, with a 16 bit type, 16 bit length, and variable value.
 * Each STUN attribute ends on a 32 bit boundary:
 *
 * \verbatim

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |         Type                  |            Length             |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                             Value                 ....        |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   \endverbatim
 */
typedef struct pj_stun_attr_hdr
{
    /**
     * STUN attribute type.
     */
    pj_uint16_t		type;

    /**
     * The Length refers to the length of the actual useful content of the
     * Value portion of the attribute, measured in bytes. The value
     * in the Length field refers to the length of the Value part of the
     * attribute prior to padding - i.e., the useful content.
     */
    pj_uint16_t		length;

} pj_stun_attr_hdr;


/**
 * This structure describes STUN generic IP address attribute, used for
 * example to represent STUN MAPPED-ADDRESS attribute.
 *
 * The generic IP address attribute indicates the transport address.
 * It consists of an eight bit address family, and a sixteen bit port,
 * followed by a fixed length value representing the IP address.  If the
 * address family is IPv4, the address is 32 bits, in network byte
 * order.  If the address family is IPv6, the address is 128 bits in
 * network byte order.
 *
 * The format of the generic IP address attribute is:
 *
 * \verbatim

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |x x x x x x x x|    Family     |           Port                |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   Address  (variable)
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   \endverbatim
 */
typedef struct pj_stun_generic_ip_addr_attr
{
    /**
     * Standard STUN attribute header.
     */
    pj_stun_attr_hdr	hdr;

    /**
     * The first 8 bits of the attribute are ignored for the purposes
     * of aligning parameters on natural 32 bit boundaries.
     */
    pj_uint8_t		ignored;

    /**
     * The address family can take on the following values:
     * 
     * 0x01:IPv4
     * 0x02:IPv6
     */
    pj_uint8_t		family;

    /**
     * Port number.
     */
    pj_uint16_t		port;

    /**
     * The address.
     */
    union {

	/** IPv4 address. */
	pj_uint32_t	ipv4;

	/** IPv6 address. */
	char		ipv6[1];

    } addr;

} pj_stun_generic_ip_addr_attr;


/**
 * This structure represents generic STUN string attributes, such as STUN
 * USERNAME, PASSWORD, SERVER, REALM, and NONCE attributes. Note that for REALM and
 * NONCE attributes, the text MUST be quoted with.
 */
typedef struct pj_stun_generic_string_attr
{
    /**
     * Standard STUN attribute header.
     */
    pj_stun_attr_hdr	hdr;

    /**
     * The string value.
     */
    pj_str_t		value;

} pj_stun_generic_string_attr;


/**
 * This structure represents a generic STUN attributes with 32bit (unsigned)
 * integer value, such as STUN FINGERPRINT and REFRESH-INTERVAL attributes.
 */
typedef struct pj_stun_generic_uint_attr
{
    /**
     * Standard STUN attribute header.
     */
    pj_stun_attr_hdr	hdr;
    
    /**
     * The 32bit value.
     */
    pj_uint32_t		value;

} pj_stun_generic_uint_attr;


/**
 * This structure represents generic STUN attributes to hold a raw binary
 * data.
 */
typedef struct pj_stun_binary_attr
{
    /**
     * Standard STUN attribute header.
     */
    pj_stun_attr_hdr	hdr;
    
    /**
     * Length of the data.
     */
    unsigned		length;

    /**
     * The raw data.
     */
    char	       *data;

} pj_stun_binary_attr;


/**
 * This structure describes STUN MESSAGE-INTEGRITY attribute.
 * The MESSAGE-INTEGRITY attribute contains an HMAC-SHA1 [10] of the
 * STUN message.  The MESSAGE-INTEGRITY attribute can be present in any
 * STUN message type.  Since it uses the SHA1 hash, the HMAC will be 20
 * bytes.
 */
typedef struct pj_stun_msg_integrity_attr
{
    /**
     * Standard STUN attribute header.
     */
    pj_stun_attr_hdr	hdr;

    /**
     * The 20 bytes hmac value.
     */
    pj_uint8_t		hmac[20];

} pj_stun_msg_integrity_attr;


/**
 * This structure describes STUN FINGERPRINT attribute. The FINGERPRINT 
 * attribute can be present in all STUN messages.  It is computed as 
 * the CRC-32 of the STUN message up to (but excluding) the FINGERPRINT 
 * attribute itself, xor-d with the 32 bit value 0x5354554e
 */
typedef struct pj_stun_generic_uint_attr pj_stun_fingerprint_attr;


/**
 * This structure represents STUN ERROR-CODE attribute. The ERROR-CODE 
 * attribute is present in the Binding Error Response and Shared Secret 
 * Error Response.  It is a numeric value in the range of 100 to 699 
 * plus a textual reason phrase encoded in UTF-8
 *
 * \verbatim

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   0                     |Class|     Number    |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |      Reason Phrase (variable)                                ..
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 \endverbatim
 */
typedef struct pj_stun_error_code_attr
{
    /**
     * Standard STUN attribute header.
     */
    pj_stun_attr_hdr	hdr;

    /**
     * The value must be zero.
     */
    pj_uint16_t		zero;

    /**
     * Error class (1-6).
     */
    pj_uint8_t		err_class;

    /**
     * Error number is the error number modulo 100.
     */
    pj_uint8_t		number;

    /**
     * The reason phrase.
     */
    pj_str_t		reason;

} pj_stun_error_code_attr;


/**
 * This describes STUN REALM attribute.
 * The REALM attribute is present in requests and responses.  It
 * contains text which meets the grammar for "realm" as described in RFC
 * 3261 [11], and will thus contain a quoted string (including the
 * quotes).
 */
typedef struct pj_stun_generic_string_attr pj_stun_realm_attr;


/**
 * This describes STUN NONCE attribute. 
 * The NONCE attribute is present in requests and in error responses.
 * It contains a sequence of qdtext or quoted-pair, which are defined in
 * RFC 3261 [11].  See RFC 2617 [7] for guidance on selection of nonce
 * values in a server.
 */
typedef struct pj_stun_generic_string_attr pj_stun_nonce_attr;


/**
 * This describes STUN UNKNOWN-ATTRIBUTES attribute.
 * The UNKNOWN-ATTRIBUTES attribute is present only in an error response
 * when the response code in the ERROR-CODE attribute is 420.
 * The attribute contains a list of 16 bit values, each of which
 * represents an attribute type that was not understood by the server.
 * If the number of unknown attributes is an odd number, one of the
 * attributes MUST be repeated in the list, so that the total length of
 * the list is a multiple of 4 bytes.
 */
typedef struct pj_stun_unknown_attr
{
    /**
     * Standard STUN attribute header.
     */
    pj_stun_attr_hdr	hdr;

    /**
     * Number of unknown attributes in the array.
     */
    unsigned		attr_count;

    /**
     * Array of unknown attribute IDs.
     */
    pj_uint16_t	        attrs[PJ_STUN_MAX_ATTR];

} pj_stun_unknown_attr;


/**
 * This structure describes STUN MAPPED-ADDRESS attribute.
 * The MAPPED-ADDRESS attribute indicates the mapped transport address.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_mapped_addr_attr;


/**
 * This describes STUN XOR-MAPPED-ADDRESS attribute (which has the same
 * format as STUN MAPPED-ADDRESS attribute).
 * The XOR-MAPPED-ADDRESS attribute is present in responses.  It
 * provides the same information that would present in the MAPPED-
 * ADDRESS attribute but because the NAT's public IP address is
 * obfuscated through the XOR function, STUN messages are able to pass
 * through NATs which would otherwise interfere with STUN.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_xor_mapped_addr_attr;


/**
 * This describes STUN SERVER attribute.
 * The server attribute contains a textual description of the software
 * being used by the server, including manufacturer and version number.
 * The attribute has no impact on operation of the protocol, and serves
 * only as a tool for diagnostic and debugging purposes.  The value of
 * SERVER is variable length.
 */
typedef struct pj_stun_generic_string_attr pj_stun_server_attr;


/**
 * This describes STUN ALTERNATE-SERVER attribute.
 * The alternate server represents an alternate transport address for a
 * different STUN server to try.  It is encoded in the same way as
 * MAPPED-ADDRESS.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_alt_server_attr;


/**
 * This describes STUN REFRESH-INTERVAL attribute.
 * The REFRESH-INTERVAL indicates the number of milliseconds that the
 * server suggests the client should use between refreshes of the NAT
 * bindings between the client and server.
 */
typedef struct pj_stun_generic_uint_attr pj_stun_refresh_interval_attr;


/**
 * This structure describes STUN RESPONSE-ADDRESS attribute.
 * The RESPONSE-ADDRESS attribute indicates where the response to a
 * Binding Request should be sent.  Its syntax is identical to MAPPED-
 * ADDRESS.
 *
 * Note that the usage of this attribute has been deprecated by the 
 * RFC 3489-bis standard.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_response_addr_attr;


/**
 * This structure describes STUN CHANGED-ADDRESS attribute.
 * The CHANGED-ADDRESS attribute indicates the IP address and port where
 * responses would have been sent from if the "change IP" and "change
 * port" flags had been set in the CHANGE-REQUEST attribute of the
 * Binding Request.  The attribute is always present in a Binding
 * Response, independent of the value of the flags.  Its syntax is
 * identical to MAPPED-ADDRESS.
 *
 * Note that the usage of this attribute has been deprecated by the 
 * RFC 3489-bis standard.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_changed_addr_attr;


/**
 * This structure describes STUN CHANGE-REQUEST attribute.
 * The CHANGE-REQUEST attribute is used by the client to request that
 * the server use a different address and/or port when sending the
 * response. 
 *
 * Bit 29 of the value is the "change IP" flag.  If true, it requests 
 * the server to send the Binding Response with a different IP address 
 * than the one the Binding Request was received on.
 *
 * Bit 30 of the value is the "change port" flag.  If true, it requests 
 * the server to send the Binding Response with a different port than 
 * the one the Binding Request was received on.
 *
 * Note that the usage of this attribute has been deprecated by the 
 * RFC 3489-bis standard.
 */
typedef struct pj_stun_generic_uint_attr pj_stun_change_request_attr;

/**
 * This structure describes STUN SOURCE-ADDRESS attribute.
 * The SOURCE-ADDRESS attribute is present in Binding Responses.  It
 * indicates the source IP address and port that the server is sending
 * the response from.  Its syntax is identical to that of MAPPED-
 * ADDRESS.
 *
 * Note that the usage of this attribute has been deprecated by the 
 * RFC 3489-bis standard.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_src_addr_attr;


/**
 * This describes the STUN REFLECTED-FROM attribute.
 * The REFLECTED-FROM attribute is present only in Binding Responses,
 * when the Binding Request contained a RESPONSE-ADDRESS attribute.  The
 * attribute contains the identity (in terms of IP address) of the
 * source where the request came from.  Its purpose is to provide
 * traceability, so that a STUN server cannot be used as a reflector for
 * denial-of-service attacks.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_reflected_from_attr;


/**
 * This describes STUN USERNAME attribute.
 * The USERNAME attribute is used for message integrity.  It identifies
 * the shared secret used in the message integrity check.  Consequently,
 * the USERNAME MUST be included in any request that contains the
 * MESSAGE-INTEGRITY attribute.
 */
typedef struct pj_stun_generic_string_attr pj_stun_username_attr;


/**
 * This describes STUN PASSWORD attribute.
 * If the message type is Shared Secret Response it MUST include the
 * PASSWORD attribute.
 */
typedef struct pj_stun_generic_string_attr pj_stun_password_attr;


/**
 * This describes STUN LIFETIME attribute.
 * The lifetime attribute represents the duration for which the server
 * will maintain an allocation in the absence of data traffic either
 * from or to the client.  It is a 32 bit value representing the number
 * of seconds remaining until expiration.
 */
typedef struct pj_stun_generic_uint_attr pj_stun_lifetime_attr;


/**
 * This describes STUN BANDWIDTH attribute.
 * The bandwidth attribute represents the peak bandwidth, measured in
 * kbits per second, that the client expects to use on the binding.  The
 * value represents the sum in the receive and send directions.
 */
typedef struct pj_stun_generic_uint_attr pj_stun_bandwidth_attr;


/**
 * This describes the STUN REMOTE-ADDRESS attribute.
 * The REMOTE-ADDRESS specifies the address and port of the peer as seen
 * from the STUN relay server.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_remote_addr_attr;


/**
 * This describes the STUN DATA attribute.
 * The DATA attribute is present in Send Indications and Data
 * Indications.  It contains raw payload data that is to be sent (in the
 * case of a Send Request) or was received (in the case of a Data
 * Indication)..
 */
typedef struct pj_stun_binary_attr pj_stun_data_attr;


/**
 * This describes the STUN RELAY-ADDRESS attribute.
 * The RELAY-ADDRESS is present in Allocate responses.  It specifies the
 * address and port that the server allocated to the client.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_relay_addr_attr;


/**
 * This describes the STUN REQUESTED-PORT-PROPS attribute.
 * This attribute allows the client to request certain properties for
 * the port that is allocated by the server.  The attribute can be used
 * with any transport protocol that has the notion of a 16 bit port
 * space (including TCP and UDP).  The attribute is 32 bits long.  Its
 * format is:

 \verbatim

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           Reserved = 0                  |B| A |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 \endverbatim  
 */
typedef struct pj_stun_generic_uint_attr pj_stun_requested_port_props_attr;


/**
 * This describes the STUN REQUESTED-TRANSPORT attribute.
 * This attribute is used by the client to request a specific transport
 * protocol for the allocated transport address.  It is a 32 bit
 * unsigned integer.  Its values are: 0x0000 for UDP and 0x0000 for TCP.
 */
typedef struct pj_stun_generic_uint_attr pj_stun_requested_transport_attr;


/**
 * This describes the STUN REQUESTED-IP attribute.
 * The REQUESTED-IP attribute is used by the client to request that a
 * specific IP address be allocated to it.
 */
typedef struct pj_stun_generic_ip_addr_attr pj_stun_requested_ip_attr;


/**
 * This describes the STUN TIMER-VAL attribute.
 * The TIMER-VAL attribute is used only in conjunction with the Set
 * Active Destination response.  It conveys from the server, to the
 * client, the value of the timer used in the server state machine.
 */
typedef struct pj_stun_generic_uint_attr pj_stun_timer_val_attr;


/**
 * This structure describes a parsed STUN message. All integral fields
 * in this structure (including IP addresses) will be in the host
 * byte order.
 */
typedef struct pj_stun_msg
{
    /**
     * STUN message header.
     */
    pj_stun_msg_hdr     hdr;

    /**
     * Number of attributes in the STUN message.
     */
    unsigned		attr_count;

    /**
     * Array of STUN attributes.
     */
    pj_stun_attr_hdr   *attr[PJ_STUN_MAX_ATTR];

} pj_stun_msg;



/**
 * Get STUN standard reason phrase for the specified error code.
 *
 * @param err_code	The STUN error code.
 *
 * @return		The STUN error reason phrase.
 */
PJ_DECL(pj_str_t) pj_stun_get_err_reason(int err_code);


/**
 * Create a blank STUN message.
 *
 * @param pool		Pool to create the STUN message.
 * @param msg_type	The 14bit message type.
 * @param tsx_id	Optional transaction ID, or NULL to let the
 *			function generates a random transaction ID.
 * @param p_msg		Pointer to receive the message.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pj_stun_msg_create(pj_pool_t *pool,
					unsigned msg_type,
					const pj_uint8_t tsx_id[12],
					pj_stun_msg **p_msg);

/**
 * Check that the PDU is potentially a valid STUN message. This function
 * is useful when application needs to multiplex STUN packets with other
 * application traffic. When this function returns PJ_SUCCESS, there is a
 * big chance that the packet is a STUN packet.
 *
 * Note that we cannot be sure that the PDU is a really valid STUN message 
 * until we actually parse the PDU.
 *
 * @param pdu		The packet buffer.
 * @param pdu_len	The length of the packet buffer.
 * @param options	Options.
 *
 * @return		PJ_SUCCESS if the PDU is a potentially valid STUN
 *			message.
 */
PJ_DECL(pj_status_t) pj_stun_msg_check(const void *pdu, unsigned pdu_len,
				       unsigned options);


/**
 * Parse incoming packet into STUN message.
 *
 * @param pool		Pool to allocate the message.
 * @param pdu		The incoming packet to be parsed.
 * @param pdu_len	The length of the incoming packet.
 * @param options	Parsing flags.
 * @param p_msg		Pointer to receive the parsed message.
 * @param p_parsed_len	Optional pointer to receive how many bytes have
 *			been parsed for the STUN message. This is useful
 *			when the packet is received over stream oriented
 *			transport.
 * @param p_err_code	Optional pointer to receive STUN error code when
 *			parsing failed.
 * @param uattr_cnt	Optional pointer to specify the number of elements
 *			in uattr array. On return, this will be filled with
 *			the actual number of attributes set in the uattr.
 * @param uattr		Optional array to receive unknown attribute types.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_msg_decode(pj_pool_t *pool,
				        const pj_uint8_t *pdu,
				        unsigned pdu_len,
				        unsigned options,
				        pj_stun_msg **p_msg,
					unsigned *p_parsed_len,
				        unsigned *p_err_code,
				        unsigned *uattr_cnt,
				        pj_uint16_t uattr[]);

/**
 * Print the message structure to a buffer.
 *
 * @param msg		The message to be printed to a contiguous buffer.
 * @param pkt_buf	The buffer.
 * @param buf_size	Size of the buffer.
 * @param options	Options.
 * @param p_msg_len	Upon return, it will be filed with the size of 
 *			the packet in bytes, or negative value on error.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pj_stun_msg_encode(const pj_stun_msg *msg,
				        pj_uint8_t *pkt_buf,
				        unsigned buf_size,
				        unsigned options,
				        unsigned *p_msg_len);


/**
 * Find STUN attribute in the STUN message, starting from the specified
 * index.
 *
 * @param msg		The STUN message.
 * @param attr_type	The attribute type to be found.
 * @param start_index	The start index of the attribute in the message.
 *			Specify zero to start searching from the first
 *			attribute.
 *
 * @return		The attribute instance, or NULL if it cannot be
 *			found.
 */
PJ_DECL(pj_stun_attr_hdr*) pj_stun_msg_find_attr(const pj_stun_msg *msg,
						 int attr_type,
						 unsigned start_index);


/**
 * Create a generic STUN IP address attribute for IPv4 address. Note that
 * the port and ip_addr parameters are in host byte order.
 *
 * @param pool		The pool to allocate memory from.
 * @param attr_type	Attribute type.
 * @param ip_addr	IP address, in host byte order.
 * @param port		Port number, in host byte order.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_generic_ip_addr_attr_create(pj_pool_t *pool,
				    int attr_type,
				    pj_uint32_t ip_addr,
				    int port,
				    pj_stun_generic_ip_addr_attr **p_attr);


/**
 * Create a STUN generic string attribute.
 *
 * @param pool		The pool to allocate memory from.
 * @param value		The string value to be assigned to the attribute.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_generic_string_attr_create(pj_pool_t *pool,
				   int attr_type,
				   const pj_str_t *value,
				   pj_stun_generic_string_attr **p_attr);


/**
 * Create a STUN generic 32bit value attribute.
 *
 * @param pool		The pool to allocate memory from.
 * @param attr_type	Attribute type, from #pj_stun_attr_type.
 * @param value		The 32bit value to be assigned to the attribute.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_generic_uint_attr_create(pj_pool_t *pool,
				 int attr_type,
				 pj_uint32_t value,
				 pj_stun_generic_uint_attr **p_attr);


/**
 * Create a STUN MESSAGE-INTEGRITY attribute.
 *
 * @param pool		The pool to allocate memory from.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_msg_integrity_attr_create(pj_pool_t *pool,
				  pj_stun_msg_integrity_attr **p_attr);


/**
 * Create a STUN ERROR-CODE attribute.
 *
 * @param pool		The pool to allocate memory from.
 * @param err_code	STUN error code.
 * @param err_reason	Optional STUN error reason. If NULL is given, the
 *			standard error reason will be given.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_error_code_attr_create(pj_pool_t *pool,
			       int err_code,
			       const pj_str_t *err_reason,
			       pj_stun_error_code_attr **p_attr);


/**
 * Create an empty instance of STUN UNKNOWN-ATTRIBUTES attribute.
 *
 * @param pool		The pool to allocate memory from.
 * @param attr_cnt	Number of attributes in the array (can be zero).
 * @param attr		Optional array of attributes.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_unknown_attr_create(pj_pool_t *pool,
			    unsigned attr_cnt,
			    pj_uint16_t attr[],
			    pj_stun_unknown_attr **p_attr);


/**
 * Create a blank binary attribute.
 *
 * @param pool		The pool to allocate memory from.
 * @param attr_type	The attribute type.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t)
pj_stun_binary_attr_create(pj_pool_t *pool,
			   int attr_type,
			   pj_stun_binary_attr **p_attr);


/**
 * @}
 */


PJ_END_DECL


#endif	/* __PJ_STUN_MSG_H__ */

