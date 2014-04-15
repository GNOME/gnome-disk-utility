NULL =

SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files =				\
	org.gnome.DiskUtility.desktop.in 			\
	gnome-disk-image-mounter.desktop.in 	\
	gnome-disk-image-writer.desktop.in	\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_DATA = org.gnome.DiskUtility.service

org.gnome.DiskUtility.service: Makefile
	$(AM_V_GEN) (echo '[D-BUS Service]'; \
		     echo 'Name=org.gnome.DiskUtility'; \
		     echo 'Exec=${bindir}/gnome-disks --gapplication-service') > $@.tmp && \
		     mv $@.tmp $@

# GSettings schemas
gsettings_SCHEMAS = 						\
	org.gnome.Disks.gschema.xml 				\
	org.gnome.settings-daemon.plugins.gdu-sd.gschema.xml	\
	$(NULL)

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

EXTRA_DIST = 			\
	$(desktop_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(gsettings_SCHEMAS)	\
	$(desktop_DATA)		\
	$(dbusservice_DATA)	\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
