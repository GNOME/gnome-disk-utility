
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

lib_LTLIBRARIES=libgdu.la

libgduincludedir=$(includedir)/gnome-disk-utility/gdu

libgduinclude_HEADERS =              			\
	gdu.h						\
	gdu-types.h					\
	gdu-callbacks.h					\
	gdu-device.h					\
	gdu-drive.h					\
	gdu-linux-md-drive.h				\
	gdu-error.h					\
	gdu-known-filesystem.h				\
	gdu-pool.h					\
	gdu-presentable.h				\
	gdu-process.h					\
	gdu-smart-data-attribute.h			\
	gdu-smart-data.h				\
	gdu-util.h					\
	gdu-volume.h					\
	gdu-volume-hole.h

libgdu_la_SOURCES =                                					\
						gdu.h					\
						gdu-types.h				\
						gdu-callbacks.h				\
	gdu-util.h				gdu-util.c				\
	gdu-pool.c				gdu-pool.h				\
	gdu-smart-data.c			gdu-smart-data.h			\
	gdu-smart-data-attribute.c		gdu-smart-data-attribute.h		\
	gdu-device.c				gdu-device.h				\
	gdu-drive.c				gdu-drive.h				\
	gdu-linux-md-drive.c			gdu-linux-md-drive.h			\
	gdu-volume.c				gdu-volume.h				\
	gdu-presentable.c			gdu-presentable.h			\
	gdu-volume-hole.c			gdu-volume-hole.h			\
	gdu-known-filesystem.c			gdu-known-filesystem.h			\
	gdu-error.c				gdu-error.h				\
	gdu-process.c				gdu-process.h				\
						gdu-private.h				\
	$(BUILT_SOURCES)

libgdu_la_CPPFLAGS = 					\
	-I$(top_srcdir)/src				\
	-I$(top_builddir)/src				\
	-DG_LOG_DOMAIN=\"libgdu\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)					\
	-DGDU_API_IS_SUBJECT_TO_CHANGE			\
	-DGDU_COMPILATION

libgdu_la_CFLAGS = 					\
	$(GLIB2_CFLAGS)					\
	$(GOBJECT2_CFLAGS)				\
	$(GIO2_CFLAGS)					\
	$(GIO_UNIX2_CFLAGS)				\
	$(DBUS_GLIB_CFLAGS)				\
	$(POLKIT_DBUS_CFLAGS)				\
	$(GNOME_KEYRING_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)

libgdu_la_LIBADD = 					\
	$(GLIB2_LIBS)					\
	$(GIO2_LIBS)					\
	$(GIO_UNIX2_LIBS)				\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)				\
	$(GNOME_KEYRING_LIBS)				\
	$(INTLLIBS)

libgdu_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
		    -export-dynamic -no-undefined -export-symbols-regex '(^gdu_.*)'

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gdu.pc

CLEANFILES = $(BUILT_SOURCES) $(pkgconfig_DATA)

EXTRA_DIST = gdu-marshal.list

clean-local :
	rm -f *~ $(BUILT_SOURCES)

