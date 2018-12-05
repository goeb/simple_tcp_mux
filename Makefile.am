# Manage configure options
AM_CPPFLAGS=-Wall
AM_CFLAGS=-Wall
AM_CXXFLAGS=-Wall
AM_LDFLAGS=

# Main program
bin_PROGRAMS = simux
simux_SOURCES = \
                src/argv.cpp \
                src/logging.cpp \
                src/simple_tcp_mux.cpp \
                src/subprocess.cpp


simux_CFLAGS = -I @srcdir@/src @OPENSSL_CFLAGS@
simux_CPPFLAGS = -I @srcdir@/src @OPENSSL_CFLAGS@
simux_LDADD = @OPENSSL_LIBS@

