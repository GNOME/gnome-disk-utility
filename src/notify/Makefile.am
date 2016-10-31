
NULL =

if USE_GSD_PLUGIN

gdu_sd_plugin_name = gdu-sd

# ----------------------------------------------------------------------

libexec_PROGRAMS = gsd-disk-utility-notify

gsd_disk_utility_notify_SOURCES =				\
	main.c							\
	gdusdmonitor.h		gdusdmonitor.c			\
	$(NULL)

gsd_disk_utility_notify_CPPFLAGS =				\
	$(NULL)

gsd_disk_utility_notify_CFLAGS =				\
	$(GLIB2_CFLAGS)						\
	$(GTK3_CFLAGS)						\
	$(UDISKS2_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)					\
	$(NULL)

gsd_disk_utility_notify_LDADD =					\
	$(GLIB2_LIBS)						\
	$(GTK3_LIBS)						\
	$(UDISKS2_LIBS)						\
	$(LIBNOTIFY_LIBS)					\
	$(NULL)

# ----------------------------------------------------------------------

autostart_desktopdir = $(sysconfdir)/xdg/autostart
autostart_desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

endif # USE_GSD_PLUGIN

desktop_in_files = org.gnome.SettingsDaemon.DiskUtilityNotify.desktop.in
%.desktop: %.desktop.in
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(desktop_in_files)

CLEANFILES =						\
	$(autostart_desktop_DATA)			\
	$(NULL)

DISTCLEANFILES =			\
	$(autostart_desktop_DATA)			\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
