
lib_LTLIBRARIES=libgdu-gtk.la

libgdu_gtkincludedir=$(includedir)/gnome-disk-utility/gdu-gtk

libgdu_gtkinclude_HEADERS =              		\
	gdu-gtk.h

libgdu_gtk_la_SOURCES =                                	\
	gdu-gtk.h		gdu-gtk.c

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
	$(POLKIT_DBUS_CFLAGS)				\
	$(GNOME_KEYRING_CFLAGS)				\
	$(GTK2_CFLAGS)					\
	$(POLKIT_GNOME_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)

libgdu_gtk_la_LIBADD = 					\
	$(GLIB2_LIBS)					\
	$(GIO2_LIBS)					\
	$(GIO_UNIX2_LIBS)				\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)				\
	$(GNOME_KEYRING_LIBS)				\
	$(GTK2_LIBS)					\
	$(POLKIT_GNOME_LIBS)				\
	$(INTLLIBS)

libgdu_gtk_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
		        -export-dynamic -no-undefined -export-symbols-regex '(^gdu_.*)'

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gdu-gtk.pc

CLEANFILES = $(BUILT_SOURCES) $(pkgconfig_DATA)

clean-local :
	rm -f *~ $(BUILT_SOURCES)

