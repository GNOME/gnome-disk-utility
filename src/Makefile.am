
SUBDIRS = gdu gdu-gtk

bin_PROGRAMS = gnome-disk-utility

gnome_disk_utility_SOURCES = 								\
						gdu-main.c				\
	gdu-time-label.h			gdu-time-label.c			\
	gdu-shell.h				gdu-shell.c				\
	gdu-section.h				gdu-section.c				\
	gdu-section-health.h			gdu-section-health.c			\
	gdu-section-create-partition-table.h	gdu-section-create-partition-table.c	\
	gdu-section-partition.h			gdu-section-partition.c			\
	gdu-section-unallocated.h		gdu-section-unallocated.c		\
	gdu-section-unrecognized.h		gdu-section-unrecognized.c		\
	gdu-section-filesystem.h		gdu-section-filesystem.c		\
	gdu-section-swapspace.h			gdu-section-swapspace.c			\
	gdu-section-encrypted.h			gdu-section-encrypted.c			\
	gdu-section-activatable-drive.h		gdu-section-activatable-drive.c		\
	gdu-section-no-media.h			gdu-section-no-media.c			\
	gdu-section-job.h			gdu-section-job.c			\
	gdu-tree.h				gdu-tree.c

gnome_disk_utility_CPPFLAGS = 				\
	-I$(top_srcdir)/src/				\
	-I$(top_builddir)/src/				\
	-DG_LOG_DOMAIN=\"GnomeDiskUtility\"		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

gnome_disk_utility_CFLAGS = 				\
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

gnome_disk_utility_LDFLAGS = 				\
	$(AM_LDFLAGS)

gnome_disk_utility_LDADD = 				\
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
