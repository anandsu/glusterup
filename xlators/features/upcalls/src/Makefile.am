xlator_LTLIBRARIES = upcalls.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

upcalls_la_LDFLAGS = -module -avoid-version

upcalls_la_SOURCES = upcalls.c upcalls-internal.c

upcalls_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
        $(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
        $(top_builddir)/rpc/xdr/src/libgfxdr.la

noinst_HEADERS = upcalls.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
        -I$(top_srcdir)/rpc/rpc-lib/src \
        -I$(top_srcdir)/rpc/xdr/src

AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

CLEANFILES = 
