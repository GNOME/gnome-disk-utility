NULL =

SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files =				\
	org.gnome.DiskUtility.desktop.in	\
	gnome-disk-image-mounter.desktop.in	\
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

appstream_in_files = org.gnome.DiskUtility.appdata.xml.in
appstream_XML = $(appstream_in_files:.xml.in=.xml)

@APPSTREAM_XML_RULES@
@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_RULE@
@GSETTINGS_RULES@

DISTCHECK_CONFIGURE_FLAGS == --enable-appdata-check

EXTRA_DIST = 			\
	$(appstream_in_files)	\
	$(desktop_in_files)	\
	$(gsettings_SCHEMAS)	\
	$(NULL)

CLEANFILES = 			\
	$(appstream_XML)	\
	$(desktop_DATA)		\
	$(dbusservice_DATA)	\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
