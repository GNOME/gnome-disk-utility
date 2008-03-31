
BUILT_SOURCES =                                         	\
        devkit-disks-daemon-glue.h                      	\
        devkit-disks-device-glue.h				\
	gdu-marshal.h			gdu-marshal.c

gdu-marshal.h: gdu-marshal.list
	glib-genmarshal $< --prefix=gdu_marshal --header > $@

gdu-marshal.c: gdu-marshal.list
	echo "#include \"gdu-marshal.h\"" > $@ && glib-genmarshal $< --prefix=gdu_marshal --body >> $@

devkit-disks-daemon-glue.h: /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-client --output=devkit-disks-daemon-glue.h /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.xml

devkit-disks-device-glue.h: /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.Device.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-client --output=devkit-disks-device-glue.h /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.Device.xml

bin_PROGRAMS = gnome-disk-utility

gnome_disk_utility_SOURCES = 						\
					gdu-main.c			\
	gdu-util.h			gdu-util.c			\
	gdu-shell.h			gdu-shell.c			\
	gdu-page.h			gdu-page.c			\
	gdu-page-drive.h		gdu-page-drive.c		\
	gdu-page-volume.h		gdu-page-volume.c		\
	gdu-page-volume-unallocated.h	gdu-page-volume-unallocated.c	\
	gdu-page-volume-unrecognized.h	gdu-page-volume-unrecognized.c	\
	gdu-page-job.h			gdu-page-job.c			\
	gdu-disk-widget.h		gdu-disk-widget.c		\
	gdu-tree.h			gdu-tree.c			\
	gdu-pool.c			gdu-pool.h			\
	gdu-device.c			gdu-device.h			\
	gdu-drive.c			gdu-drive.h			\
	gdu-activatable-drive.c		gdu-activatable-drive.h		\
	gdu-volume.c			gdu-volume.h			\
	gdu-presentable.c		gdu-presentable.h		\
	gdu-volume-hole.c		gdu-volume-hole.h		\
	$(BUILT_SOURCES)

gnome_disk_utility_CPPFLAGS = 				\
	-I$(top_srcdir)					\
	-DG_LOG_DOMAIN=\"GnomeDiskUtility\"		\
	-DDATADIR=\""$(pkgdatadir)"\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

gnome_disk_utility_CFLAGS = 				\
	   $(GNOME_DISK_UTILITY_CFLAGS)			\
	   $(WARN_CFLAGS)				\
	   $(AM_CFLAGS)

gnome_disk_utility_LDFLAGS = 				\
	$(AM_LDFLAGS)

gnome_disk_utility_LDADD = 				\
	$(GNOME_DISK_UTILITY_LIBS)			\
	$(INTLLIBS)

CLEANFILES = $(BUILT_SOURCES)

clean-local :
	rm -f *~
