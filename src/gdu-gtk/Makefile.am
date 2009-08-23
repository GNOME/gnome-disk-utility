
NULL =

gdu-gtk-enumtypes.h: gdu-gtk-enums.h gdu-gtk-enumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`; \
	 cd $(srcdir) && glib-mkenums --template gdu-gtk-enumtypes.h.template gdu-gtk-enums.h ) > \
	   gdu-gtk-enumtypes.h.tmp && mv gdu-gtk-enumtypes.h.tmp gdu-gtk-enumtypes.h

gdu-gtk-enumtypes.c: gdu-gtk-enums.h gdu-gtk-enumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`; \
	 cd $(srcdir) && glib-mkenums --template gdu-gtk-enumtypes.c.template gdu-gtk-enums.h ) > \
	   gdu-gtk-enumtypes.c.tmp && mv gdu-gtk-enumtypes.c.tmp gdu-gtk-enumtypes.c

BUILT_SOURCES =            	\
	gdu-gtk-enumtypes.c     \
	gdu-gtk-enumtypes.h     \
	$(NULL)

EXTRA_DIST =                         	\
	gdu-gtk-enumtypes.h.template    \
	gdu-gtk-enumtypes.c.template    \
	$(NULL)

lib_LTLIBRARIES=libgdu-gtk.la

libgdu_gtkincludedir=$(includedir)/gnome-disk-utility/gdu-gtk

libgdu_gtkinclude_HEADERS =              				\
	gdu-gtk.h							\
	gdu-gtk-types.h							\
	gdu-gtk-enums.h							\
	gdu-gtk-enumtypes.h						\
	gdu-time-label.h						\
	gdu-pool-tree-view.h						\
	gdu-pool-tree-model.h						\
	gdu-size-widget.h						\
	gdu-create-linux-md-dialog.h					\
	gdu-ata-smart-dialog.h						\
	gdu-spinner.h							\
	$(NULL)

libgdu_gtk_la_SOURCES =                 	               				\
	gdu-gtk.h				gdu-gtk.c				\
	gdu-gtk-enums.h									\
	gdu-gtk-enumtypes.h			gdu-gtk-enumtypes.c			\
	gdu-gtk-types.h									\
	gdu-time-label.h			gdu-time-label.c			\
	gdu-pool-tree-view.h			gdu-pool-tree-view.c			\
	gdu-pool-tree-model.h			gdu-pool-tree-model.c			\
	gdu-size-widget.h			gdu-size-widget.c			\
	gdu-create-linux-md-dialog.h		gdu-create-linux-md-dialog.c		\
	gdu-ata-smart-dialog.h			gdu-ata-smart-dialog.c			\
	gdu-spinner.h				gdu-spinner.c				\
	$(NULL)

libgdu_gtk_la_CPPFLAGS = 				\
	-I$(top_srcdir)/src				\
	-I$(top_builddir)/src				\
	-DG_LOG_DOMAIN=\"libgdu-gtk\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)					\
	-DGDU_API_IS_SUBJECT_TO_CHANGE			\
	-DGDU_GTK_API_IS_SUBJECT_TO_CHANGE		\
	-DGDU_GTK_COMPILATION

libgdu_gtk_la_CFLAGS = 					\
	$(GLIB2_CFLAGS)					\
	$(GOBJECT2_CFLAGS)				\
	$(GIO2_CFLAGS)					\
	$(GIO_UNIX2_CFLAGS)				\
	$(DBUS_GLIB_CFLAGS)				\
	$(GNOME_KEYRING_CFLAGS)				\
	$(GTK2_CFLAGS)					\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)					\
	$(LIBATASMART_CFLAGS)				\
	$(NULL)

libgdu_gtk_la_LIBADD = 					\
	$(GLIB2_LIBS)					\
	$(GIO2_LIBS)					\
	$(GIO_UNIX2_LIBS)				\
	$(DBUS_GLIB_LIBS)				\
	$(GNOME_KEYRING_LIBS)				\
	$(GTK2_LIBS)					\
	$(INTLLIBS)					\
	$(LIBATASMART_LIBS)				\
	$(NULL)

libgdu_gtk_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
		        -export-dynamic -no-undefined -export-symbols-regex '(^gdu_.*)'

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gdu-gtk.pc

CLEANFILES = $(BUILT_SOURCES) $(pkgconfig_DATA)

EXTRADIST =				\
	gdu-gtk-enumtypes.h.template	\
	gdu-gtk-enumtypes.c.template	\
	$(NULL)

clean-local :
	rm -f *~ $(BUILT_SOURCES)

