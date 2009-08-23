
NULL =

bin_PROGRAMS = palimpsest

palimpsest_SOURCES = 									\
	bling-color.h				bling-color.c				\
	bling-spinner.h				bling-spinner.c				\
						gdu-main.c				\
	gdu-shell.h				gdu-shell.c				\
	gdu-section.h				gdu-section.c				\
	gdu-section-create-partition-table.h	gdu-section-create-partition-table.c	\
	gdu-section-partition.h			gdu-section-partition.c			\
	gdu-section-unallocated.h		gdu-section-unallocated.c		\
	gdu-section-unrecognized.h		gdu-section-unrecognized.c		\
	gdu-section-filesystem.h		gdu-section-filesystem.c		\
	gdu-section-swapspace.h			gdu-section-swapspace.c			\
	gdu-section-encrypted.h			gdu-section-encrypted.c			\
	gdu-section-linux-md-drive.h		gdu-section-linux-md-drive.c		\
	gdu-section-no-media.h			gdu-section-no-media.c			\
	$(NULL)

palimpsest_CPPFLAGS = 					\
	-I$(top_srcdir)/src/				\
	-I$(top_builddir)/src/				\
	-DG_LOG_DOMAIN=\"Palimpsest\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	-DGDU_API_IS_SUBJECT_TO_CHANGE			\
	-DGDU_GTK_API_IS_SUBJECT_TO_CHANGE		\
	$(AM_CPPFLAGS)

palimpsest_CFLAGS = 					\
	$(GLIB2_CFLAGS)					\
	$(GOBJECT2_CFLAGS)				\
	$(GIO2_CFLAGS)					\
	$(GIO_UNIX2_CFLAGS)				\
	$(DBUS_GLIB_CFLAGS)				\
	$(GNOME_KEYRING_CFLAGS)				\
	$(GTK2_CFLAGS)					\
	$(UNIQUE_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)

palimpsest_LDFLAGS = 					\
	$(AM_LDFLAGS)

palimpsest_LDADD = 					\
	$(GLIB2_LIBS)					\
	$(GIO2_LIBS)					\
	$(GIO_UNIX2_LIBS)				\
	$(DBUS_GLIB_LIBS)				\
	$(GNOME_KEYRING_LIBS)				\
	$(GTK2_LIBS)					\
	$(UNIQUE_LIBS)					\
	$(INTLLIBS)					\
	$(top_builddir)/src/gdu/libgdu.la		\
	$(top_builddir)/src/gdu-gtk/libgdu-gtk.la

clean-local :
	rm -f *~
