xlator_LTLIBRARIES = upcalls.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

upcalls_la_LDFLAGS = -module -avoid-version

upcalls_la_SOURCES = upcalls.c

upcalls_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = upcalls.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

CLEANFILES = 
