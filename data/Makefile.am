SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files = palimpsest.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

distuninstallcheck_listfiles = find . -type f -print | grep -v scrollkeeper

EXTRA_DIST = 			\
	$(desktop_in_files)	

CLEANFILES = \
	$(desktop_DATA)

clean-local :
	rm -f *~
