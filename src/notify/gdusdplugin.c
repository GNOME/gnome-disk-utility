/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gio/gio.h>

#include "gdusdplugin.h"
#include "gdusdmonitor.h"

#include <gnome-settings-daemon/gnome-settings-plugin.h>

struct GduSdPluginClass;
typedef struct GduSdPluginClass GduSdPluginClass;

struct GduSdPlugin
{
  GnomeSettingsPlugin parent_instance;
  GduSdMonitor *monitor;
  guint name_owner_id;
};

struct GduSdPluginClass
{
  GnomeSettingsPluginClass parent_class;
};

GNOME_SETTINGS_PLUGIN_REGISTER (GduSdPlugin, gdu_sd_plugin)

static void
name_acquired_handler (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
  GduSdPlugin *plugin = GDU_SD_PLUGIN (user_data);
  g_warn_if_fail (plugin->monitor == NULL);
  g_clear_object (&plugin->monitor);
  plugin->monitor = gdu_sd_monitor_new ();
}

static void
name_lost_handler (GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  GduSdPlugin *plugin = GDU_SD_PLUGIN (user_data);
  g_clear_object (&plugin->monitor);
}

static void
gdu_sd_plugin_init (GduSdPlugin *plugin)
{
}

static void
gdu_sd_plugin_finalize (GObject *object)
{
  GduSdPlugin *plugin = GDU_SD_PLUGIN (object);

  if (plugin->name_owner_id == 0)
    {
      g_bus_unown_name (plugin->name_owner_id);
      plugin->name_owner_id = 0;
      g_clear_object (&plugin->monitor);
    }
  g_clear_object (&plugin->monitor);

  G_OBJECT_CLASS (gdu_sd_plugin_parent_class)->finalize (object);
}

static void
impl_activate (GnomeSettingsPlugin *_plugin)
{
  GduSdPlugin *plugin = GDU_SD_PLUGIN (_plugin);
  /* The reason for claiming a unique name is so it's easier to test
   * code changes - it helps ensure that only one instance of
   * GduSdMonitor is running at any one time. See also testplugin.c.
   */
  if (plugin->name_owner_id == 0)
    {
      plugin->name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                              "org.gnome.Disks.NotificationMonitor",
                                              G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                              G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                              NULL, /* bus_acquired_handler */
                                              name_acquired_handler,
                                              name_lost_handler,
                                              plugin,
                                              NULL); /* GDestroyNotify */
    }

}

static void
impl_deactivate (GnomeSettingsPlugin *_plugin)
{
  GduSdPlugin *plugin = GDU_SD_PLUGIN (_plugin);
  if (plugin->name_owner_id == 0)
    {
      g_bus_unown_name (plugin->name_owner_id);
      plugin->name_owner_id = 0;
      g_clear_object (&plugin->monitor);
    }
  g_clear_object (&plugin->monitor);
}

static void
gdu_sd_plugin_class_init (GduSdPluginClass *klass)
{
  GObjectClass           *object_class = G_OBJECT_CLASS (klass);
  GnomeSettingsPluginClass *plugin_class = GNOME_SETTINGS_PLUGIN_CLASS (klass);

  object_class->finalize = gdu_sd_plugin_finalize;
  plugin_class->activate = impl_activate;
  plugin_class->deactivate = impl_deactivate;
}
