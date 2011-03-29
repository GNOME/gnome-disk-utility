NULL =

INCLUDES =						\
	-DDATADIR=\"$(datadir)\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-I$(top_srcdir)/src				\
	-I$(top_builddir)/src				\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)					\
	-DGDU_API_IS_SUBJECT_TO_CHANGE			\
	-DGDU_GTK_API_IS_SUBJECT_TO_CHANGE		\
	$(NULL)

CORE_CFLAGS = 						\
	$(GLIB2_CFLAGS)					\
	$(GOBJECT2_CFLAGS)				\
	$(GIO2_CFLAGS)					\
	$(GIO_UNIX2_CFLAGS)				\
	$(GTHREAD2_CFLAGS)				\
	$(GTK2_CFLAGS)					\
	$(NULL)

CORE_LIBADD = 						\
	$(GLIB2_LIBS)					\
	$(GOBJECT2_LIBS)				\
	$(GIO2_LIBS)					\
	$(GIO_UNIX2_LIBS)				\
	$(GTHREAD2_LIBS)				\
	$(GTK2_LIBS)					\
	$(INTLLIBS)					\
	$(top_builddir)/src/gdu/libgdu.la		\
	$(top_builddir)/src/gdu-gtk/libgdu-gtk.la	\
	$(NULL)


nautilus_extensiondir = $(libdir)/nautilus/extensions-3.0
nautilus_extension_LTLIBRARIES = libnautilus-gdu.la

libnautilus_gdu_la_SOURCES =			\
	nautilus-module.c			\
	nautilus-gdu.c				\
	nautilus-gdu.h				\
	$(NULL)
libnautilus_gdu_la_CPPFLAGS = $(CORE_CFLAGS) $(NAUTILUS_CFLAGS) -DG_LOG_DOMAIN=\"Nautilus-GDU\"
libnautilus_gdu_la_LDFLAGS = $(AM_LDFLAGS) -export_dynamic -module -avoid-version -no-undefined
libnautilus_gdu_la_LIBADD  = $(CORE_LIBADD) $(NAUTILUS_LIBS)
