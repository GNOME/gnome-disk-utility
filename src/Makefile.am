bin_PROGRAMS = gnome-disk-utility

gnome_disk_utility_SOURCES = 						\
	gdu-main.h			gdu-main.c			\
	gdu-menus.h			gdu-menus.c			\
	gdu-device-tree.h		gdu-device-tree.c

gnome_disk_utility_CPPFLAGS = 			\
	-I$(top_srcdir)					\
	-DG_LOG_DOMAIN=\"GnomeDeviceManager\"		\
	-DDATADIR=\""$(pkgdatadir)"\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

gnome_disk_utility_CFLAGS = 				\
	   $(GNOME_DEVICE_MANAGER_CFLAGS)		\
	   $(WARN_CFLAGS)				\
	   $(AM_CFLAGS)

gnome_disk_utility_LDFLAGS = 				\
	$(AM_LDFLAGS)

gnome_disk_utility_LDADD = 						\
	$(GNOME_DEVICE_MANAGER_LIBS)					\
	$(INTLLIBS)

clean-local :
	rm -f *~
