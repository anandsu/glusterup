xlator_LTLIBRARIES = proto-locks.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

proto_locks_la_LDFLAGS = -module -avoid-version

proto_locks_la_SOURCES = proto-locks.c
proto_locks_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la\
                            $(top_builddir)/api/src/libgfapi.la\
                            $(RLLIBS) $(top_builddir)/rpc/xdr/src/libgfxdr.la \
                            $(top_builddir)/rpc/rpc-lib/src/libgfrpc.la

noinst_HEADERS = proto-locks.h proto-locks-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
               -I$(top_srcdir)/api/src \
               -I$(top_srcdir)/rpc/rpc-lib/src \
               -I$(top_srcdir)/rpc/xdr/src \
	       -DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
