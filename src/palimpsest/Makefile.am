
NULL =

bin_PROGRAMS = palimpsest

palimpsest_SOURCES = 					\
				main.c			\
	gdu.h						\
	gduapplication.h	gduapplication.c	\
	gdutypes.h					\
	$(NULL)

palimpsest_CPPFLAGS = 					\
	-I$(top_srcdir)/src/				\
	-I$(top_builddir)/src/				\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"		\
	-DG_LOG_DOMAIN=\"Palimpsest\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(NULL)

palimpsest_CFLAGS = 					\
	$(GLIB2_CFLAGS)					\
	$(UDISKS2_CFLAGS)				\
	$(GTK3_CFLAGS)					\
	$(WARN_CFLAGS)					\
	$(NULL)

palimpsest_LDADD = 					\
	$(GLIB2_LIBS)					\
	$(UDISKS2_LIBS)					\
	$(GTK3_LIBS)					\
	$(NULL)

clean-local :
	rm -f *~
