
BUILT_SOURCES =                                         \
        devkit-disks-daemon-glue.h                      \
        devkit-disks-device-glue.h

devkit-disks-daemon-glue.h: /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-client --output=devkit-disks-daemon-glue.h /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.xml

devkit-disks-device-glue.h: /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.Device.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-client --output=devkit-disks-device-glue.h /usr/share/dbus-1/interfaces/org.freedesktop.DeviceKit.Disks.Device.xml

bin_PROGRAMS = gnome-disk-utility

gnome_disk_utility_SOURCES = 						\
	gdu-main.h			gdu-main.c			\
	gdu-menus.h			gdu-menus.c			\
	gdu-tree.h			gdu-tree.c			\
	gdu-pool.c			gdu-pool.h			\
	gdu-device.c			gdu-device.h			\
	gdu-drive.c			gdu-drive.h			\
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
