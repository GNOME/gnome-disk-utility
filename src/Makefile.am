
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

gnome_disk_utility_SOURCES = 								\
						gdu-main.c				\
	gdu-util.h				gdu-util.c				\
	gdu-ui-util.h				gdu-ui-util.c				\
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
	gdu-tree.h				gdu-tree.c				\
	gdu-pool.c				gdu-pool.h				\
	gdu-smart-data.c			gdu-smart-data.h			\
	gdu-smart-data-attribute.c		gdu-smart-data-attribute.h		\
	gdu-device.c				gdu-device.h				\
	gdu-drive.c				gdu-drive.h				\
	gdu-activatable-drive.c			gdu-activatable-drive.h			\
	gdu-volume.c				gdu-volume.h				\
	gdu-presentable.c			gdu-presentable.h			\
	gdu-volume-hole.c			gdu-volume-hole.h			\
	gdu-known-filesystem.c			gdu-known-filesystem.h			\
	gdu-error.c				gdu-error.h				\
	gdu-process.c				gdu-process.h				\
						gdu-private.h				\
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

EXTRA_DIST = gdu-marshal.list

clean-local :
	rm -f *~
