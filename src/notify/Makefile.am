
NULL =

if USE_GSD_PLUGIN

gdu_sd_plugin_name = gdu-sd

gdu_sd_plugindir=$(gsd_plugindir)

gdu_sd_plugin_LTLIBRARIES = 					\
	libgdu-sd.la						\
	$(NULL)

libgdu_sd_la_SOURCES = 						\
	gdusdmanager.h		gdusdmanager.c			\
	gdusdmonitor.h		gdusdmonitor.c			\
	$(NULL)

libgdu_sd_la_CPPFLAGS = 					\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" 	\
	$(AM_CPPFLAGS)

libgdu_sd_la_CFLAGS = 						\
	$(GSD_PLUGIN_CFLAGS)					\
	$(GLIB2_CFLAGS)						\
	$(GTK3_CFLAGS)						\
	$(UDISKS2_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)					\
	$(NULL)

libgdu_sd_la_LDFLAGS = 						\
	-export-dynamic 					\
	-export-symbols-regex '^register_gnome_settings_plugin' \
	-module 						\
	-avoid-version 						\
	-no-undefined 						\
	$(NULL)

libgdu_sd_la_LIBADD  = 						\
	$(GSD_PLUGIN_LIBS)					\
	$(GLIB2_LIBS)						\
	$(GTK3_LIBS)						\
	$(UDISKS2_LIBS)						\
	$(LIBNOTIFY_LIBS)					\
	$(NULL)

endif # USE_GSD_PLUGIN

# ----------------------------------------------------------------------

noinst_PROGRAMS = test-plugin

test_plugin_SOURCES = 						\
	testplugin.c						\
	gdusdmonitor.h		gdusdmonitor.c			\
	$(NULL)

test_plugin_CPPFLAGS =						\
	$(NULL)

test_plugin_CFLAGS =						\
	$(GLIB2_CFLAGS)						\
	$(GTK3_CFLAGS)						\
	$(UDISKS2_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)					\
	$(NULL)

test_plugin_LDADD =						\
	$(GLIB2_LIBS)						\
	$(GTK3_LIBS)						\
	$(UDISKS2_LIBS)						\
	$(LIBNOTIFY_LIBS)					\
	$(NULL)

# ----------------------------------------------------------------------

gdu_sd_plugin_in_files = 					\
	gdu-sd-plugin.gnome-settings-plugin.in			\
	$(NULL)

if USE_GSD_PLUGIN

gdu_sd_plugin_DATA = $(gdu_sd_plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

endif # USE_GSD_PLUGIN

%.gnome-settings-plugin: %.gnome-settings-plugin.in
	LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@

EXTRA_DIST = 				\
	$(gdu_sd_plugin_in_files)	\
	$(NULL)

CLEANFILES = 				\
	$(gdu_sd_plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =			\
	$(gdu_sd_plugin_DATA)		\
	$(NULL)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
