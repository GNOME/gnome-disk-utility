NULL =

SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files =				\
	org.gnome.DiskUtility.desktop.in	\
	gnome-disk-image-mounter.desktop.in	\
	gnome-disk-image-writer.desktop.in	\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

%.desktop: %.desktop.in
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_in_files = org.gnome.DiskUtility.service.in
dbusservice_DATA = $(dbusservice_in_files:.service.in=.service)

%.service: %.service.in
	$(AM_V_GEN)$(SED) -e "s|[@]bindir[@]|$(bindir)|" $< > $@

# GSettings schemas
gsettings_SCHEMAS = 						\
	org.gnome.Disks.gschema.xml 				\
	$(NULL)

@GSETTINGS_RULES@

appstream_in_files = org.gnome.DiskUtility.appdata.xml.in
appstream_XML = $(appstream_in_files:.xml.in=.xml)

$(appstream_XML): $(appstream_in_files)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

@APPSTREAM_XML_RULES@


DISTCHECK_CONFIGURE_FLAGS == --enable-appdata-check

EXTRA_DIST = 			\
	$(appstream_in_files)	\
	$(desktop_in_files)	\
	$(dbusservice_in_files) \
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
