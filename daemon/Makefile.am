AM_CPPFLAGS = \
    -I@top_srcdir@ $(LIBEVENT_CPPFLAGS) \
    -I$(top_srcdir)/third-party/ \
    -I$(top_srcdir)/third-party/libgrowl/ \
    -DEMBEDDED

AM_CFLAGS = \
    $(OPENSSL_CFLAGS) \
    $(LIBCURL_CFLAGS) \
    $(ZLIB_CFLAGS) \
    $(PTHREAD_CFLAGS)

AM_LDFLAGS = \
    $(ZLIB_LDFLAGS)

dist_man_MANS = \
    transmission-daemon.1 \
    transmission-remote.1

bin_PROGRAMS = \
    transmission-daemon \
    transmission-remote

LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(top_builddir)/third-party/libgrowl/libgrowl.la \
    $(INTLLIBS) \
    $(LIBCURL_LIBS) \
    $(ZLIB_LIBS) \
    $(OPENSSL_LIBS) \
    $(PTHREAD_LIBS) -lm

noinst_HEADERS = \
    watch.h

transmission_daemon_SOURCES = daemon.c watch.c
transmission_remote_SOURCES = remote.c
