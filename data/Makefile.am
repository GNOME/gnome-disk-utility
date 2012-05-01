NULL =

SUBDIRS = ui icons

desktopdir = $(datadir)/applications
desktop_in_files = palimpsest.desktop.in gnome-disk-image-mounter.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = 			\
	$(desktop_in_files)	\
	$(NULL)

CLEANFILES = \
	$(desktop_DATA)		\
	$(NULL)

clean-local :
	rm -f *~
