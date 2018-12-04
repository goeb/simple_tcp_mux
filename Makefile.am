# Manage configure options
AM_CPPFLAGS=-Wall
AM_CFLAGS=-Wall
AM_CXXFLAGS=-Wall
AM_LDFLAGS=

# Main program
bin_PROGRAMS = simple_tcp_mux
simple_tcp_mux_SOURCES = \
                         src/argv.cpp \
                         src/logging.cpp \
                         src/simple_tcp_mux.cpp \
                         src/subprocess.cpp


simple_tcp_mux_CFLAGS = -I @srcdir@/src @OPENSSL_CFLAGS@
simple_tcp_mux_CPPFLAGS = -I @srcdir@/src @OPENSSL_CFLAGS@
simple_tcp_mux_LDADD = @OPENSSL_LIBS@

