AM_CPPFLAGS = -I$(top_srcdir)/src -Wall -g3 -O0
AM_CFLAGS = -I$(top_srcdir)/src -Wall -g3 -O0
AM_LDFLAGS = -pthread

AUTOMAKE_OPTIONS = subdir-objects

AUTOMAKE_ARGS = --add-missing

if MEMCMD
bin_PROGRAMS = memsend memwatch
memsend_SOURCES = src/memsend.c
memsend_LDADD = libmemshare.la
memwatch_SOURCES = src/memwatch.c
memwatch_LDADD = libmemshare.la
endif

if MEMTEST
#basetest_SOURCES = src/test/testmain.c
#basetest_LDADD = libmemshare.la
#reply_SOURCES = src/test/reply.c
#reply_LDADD = libmemshare.la
replyer_SOURCES = src/test/replyer.c
replyer_LDADD = libmemshare.la
#listen_SOURCES = src/test/listen.c
#listen_LDADD = libmemshare.la
tlogtest_SOURCES = src/examples/tlogmain.c
tlogtest_LDADD = libtlog.la
monitor_SOURCES = src/test/monitor.c
monitor_LDADD = libmemshare.la

noinst_PROGRAMS = tlogtest monitor replyer
endif

lib_LTLIBRARIES = libmemshare.la
libmemshare_la_SOURCES = src/memshare.c \
			src/memshare.h \
			src/queue.c \
			src/queue.h \
			src/memshare_api.h
include_HEADERS = src/memshare_api.h

if TLOG
libtlog_la_SOURCES = src/examples/tlog.c \
			src/examples/tlog_api.h
libtlog_la_LIBADD = libmemshare.la
lib_LTLIBRARIES += libtlog.la
include_HEADERS += src/examples/tlog_api.h
endif
