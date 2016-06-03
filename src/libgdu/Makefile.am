
NULL =
BUILT_SOURCES =

noinst_LTLIBRARIES = libgdu.la

libgduenumtypes.h: libgduenums.h libgduenumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template libgduenumtypes.h.template libgduenums.h ) >	\
	   libgduenumtypes.h.tmp && mv libgduenumtypes.h.tmp libgduenumtypes.h			\
	$(NULL)

libgduenumtypes.c: libgduenums.h libgduenumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template libgduenumtypes.c.template libgduenums.h ) > 	\
	   libgduenumtypes.c.tmp && mv libgduenumtypes.c.tmp libgduenumtypes.c			\
	$(NULL)

enum_built_sources =								\
	libgduenumtypes.h			libgduenumtypes.c		\
	$(NULL)

BUILT_SOURCES += $(enum_built_sources)

libgdu_la_SOURCES = 							\
	libgdu.h							\
	libgduenums.h							\
	libgdutypes.h							\
	gduutils.h			gduutils.c			\
	$(enum_built_sources)						\
	$(NULL)

libgdu_la_CPPFLAGS = 					\
	-I$(top_srcdir)/src/				\
	-I$(top_srcdir)/src/libgdu			\
	-I$(top_builddir)/src/				\
	-I$(top_srcdir)/src/libgdu			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"		\
	-DG_LOG_DOMAIN=\"libgdu\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(NULL)

libgdu_la_CFLAGS = 					\
	$(GLIB2_CFLAGS)					\
	$(UDISKS2_CFLAGS)				\
	$(GTK3_CFLAGS)					\
	$(LIBSECRET1_CFLAGS)				\
	$(LIBSYSTEMD_CFLAGS)				\
	$(PWQUALITY_CFLAGS)				\
	$(WARN_CFLAGS)					\
	-lm						\
	$(NULL)

libgdu_la_LDFLAGS =					\
	$(WARN_LDFLAGS)					\
	$(NULL)

libgdu_la_LIBADD = 					\
	$(GLIB2_LIBS)					\
	$(UDISKS2_LIBS)					\
	$(GTK3_LIBS)					\
	$(LIBSECRET1_LIBS)				\
	$(LIBSYSTEMD_LIBS)				\
	$(PWQUALITY_LIBS)				\
	$(NULL)

EXTRA_DIST = 						\
	libgduenumtypes.h.template			\
	libgduenumtypes.c.template			\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
