include $(top_srcdir)/build/vars.build.mk
plugin = tagfilter

geanyplugins_LTLIBRARIES = tagfilter.la


tagfilter_la_SOURCES  = tagfilter-plugin.c
tagfilter_la_CPPFLAGS = $(AM_CPPFLAGS) \
                           -DG_LOG_DOMAIN=\"TagFilter\"
tagfilter_la_CFLAGS   = $(AM_CFLAGS) \
                           $(TAGFILTER_CFLAGS)
tagfilter_la_LIBADD   = $(COMMONLIBS) \
                           $(TAGFILTER_CFLAGS)


include $(top_srcdir)/build/cppcheck.mk
