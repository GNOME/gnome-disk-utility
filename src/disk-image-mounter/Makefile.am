
NULL =

bin_PROGRAMS = gnome-disk-image-mounter

gnome_disk_image_mounter_SOURCES = 			\
					main.c		\
	$(NULL)

gnome_disk_image_mounter_CPPFLAGS = 			\
	-I$(top_srcdir)/src/				\
	-I$(top_srcdir)/src/libgdu			\
	-I$(top_builddir)/src/				\
	-I$(top_builddir)/src/libgdu			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"		\
	-DG_LOG_DOMAIN=\"gnome-disk-image-mounter\"	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(NULL)

gnome_disk_image_mounter_CFLAGS = 			\
	$(GLIB2_CFLAGS)					\
	$(UDISKS2_CFLAGS)				\
	$(GTK3_CFLAGS)					\
	$(WARN_CFLAGS)					\
	$(NULL)

gnome_disk_image_mounter_LDADD = 			\
	$(GLIB2_LIBS)					\
	$(UDISKS2_LIBS)					\
	$(GTK3_LIBS)					\
        $(top_builddir)/src/libgdu/libgdu.la        	\
	$(NULL)

gnome_disk_image_mounter_LDFLAGS =			\
	$(WARN_LDFLAGS)					\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
