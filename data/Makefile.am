NULL =

SUBDIRS = ui icons

# TODO: Move gdu-mimetypes.xml to shared-mime-info, see https://bugs.freedesktop.org/show_bug.cgi?id=64845
mimedir = $(datadir)/mime/packages
mime_DATA = gdu-mimetypes.xml

desktopdir = $(datadir)/applications
desktop_in_files =				\
	gnome-disks.desktop.in 			\
	gnome-disk-image-mounter.desktop.in 	\
	gnome-disk-image-writer.desktop.in	\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

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
	$(mime_DATA)		\
	$(NULL)

install-data-hook:
	update-mime-database $(datadir)/mime

uninstall-hook:
	update-mime-database $(datadir)/mime

clean-local :
	rm -f *~
