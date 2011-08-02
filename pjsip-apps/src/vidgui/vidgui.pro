win32 {
  DEFINES += PJ_WIN32=1
  LIBS += ../../../lib/pjproject.lib Iphlpapi.lib  dsound.lib \
  	  dxguid.lib netapi32.lib mswsock.lib ws2_32.lib odbc32.lib \
  	  odbccp32.lib ole32.lib user32.lib gdi32.lib advapi32.lib 
} else {
  LIBS += $$system(make -f pj-pkgconfig.mak ldflags)
  QMAKE_CXXFLAGS += $$system(make -f pj-pkgconfig.mak cflags)
}


TEMPLATE = app
CONFIG += thread debug
TARGET = 
DEPENDPATH += .

# Input
HEADERS += vidgui.h vidwin.h
SOURCES += vidgui.cpp vidwin.cpp 

