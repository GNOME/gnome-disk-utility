
NULL =

noinst_PROGRAMS = grid

grid_SOURCES = 										\
	grid.c										\
	gdu-grid-types.h								\
	gdu-grid-element.h			gdu-grid-element.c			\
	gdu-grid-hbox.h				gdu-grid-hbox.c				\
	gdu-grid-view.h				gdu-grid-view.c				\
	gdu-grid-details.h			gdu-grid-details.c			\
	$(NULL)

grid_CPPFLAGS = 					\
	-I$(top_srcdir)/src/				\
	-I$(top_builddir)/src/				\
	-DG_LOG_DOMAIN=\"TestGrid\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	-DGDU_API_IS_SUBJECT_TO_CHANGE			\
	-DGDU_GTK_API_IS_SUBJECT_TO_CHANGE		\
	$(AM_CPPFLAGS)

grid_CFLAGS = 						\
	$(GLIB2_CFLAGS)					\
	$(GOBJECT2_CFLAGS)				\
	$(GIO2_CFLAGS)					\
	$(GIO_UNIX2_CFLAGS)				\
	$(DBUS_GLIB_CFLAGS)				\
	$(POLKIT_DBUS_CFLAGS)				\
	$(POLKIT_GNOME_CFLAGS)				\
	$(GNOME_KEYRING_CFLAGS)				\
	$(GTK2_CFLAGS)					\
	$(LIBSEXY_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)

grid_LDFLAGS = 						\
	$(AM_LDFLAGS)

grid_LDADD = 						\
	$(GLIB2_LIBS)					\
	$(GIO2_LIBS)					\
	$(GIO_UNIX2_LIBS)				\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)				\
	$(POLKIT_GNOME_LIBS)				\
	$(GNOME_KEYRING_LIBS)				\
	$(GTK2_LIBS)					\
	$(LIBSEXY_LIBS)					\
	$(INTLLIBS)					\
	$(top_builddir)/src/gdu/libgdu.la		\
	$(top_builddir)/src/gdu-gtk/libgdu-gtk.la

clean-local :
	rm -f *~
