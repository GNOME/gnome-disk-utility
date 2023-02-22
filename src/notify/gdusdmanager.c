/*
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gio/gio.h>

#include "gdusdmanager.h"
#include "gdusdmonitor.h"

#include <gnome-settings-daemon/gnome-settings-plugin.h>

struct GduSdManagerClass;
typedef struct GduSdManagerClass GduSdManagerClass;

struct GduSdManager
{
  GObject parent_instance;
  GduSdMonitor *monitor;
  guint name_owner_id;
};

struct GduSdManagerClass
{
  GObjectClass parent_class;
};

static gboolean gdu_sd_manager_start (GduSdManager *, GError **);
static void     gdu_sd_manager_stop  (GduSdManager *);

static GduSdManager *gdu_sd_manager_new   (void);

G_DEFINE_TYPE (GduSdManager, gdu_sd_manager, G_TYPE_OBJECT)
GNOME_SETTINGS_PLUGIN_REGISTER (GduSd, gdu_sd)

static void
name_acquired_handler (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
  GduSdManager *manager = GDU_SD_MANAGER (user_data);
  g_warn_if_fail (manager->monitor == NULL);
  g_clear_object (&manager->monitor);
  manager->monitor = gdu_sd_monitor_new ();
}

static void
name_lost_handler (GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  GduSdManager *manager = GDU_SD_MANAGER (user_data);
  g_clear_object (&manager->monitor);
}

static void
gdu_sd_manager_init (GduSdManager *manager)
{
}

static void
gdu_sd_manager_finalize (GObject *object)
{
  GduSdManager *manager = GDU_SD_MANAGER (object);

  gdu_sd_manager_stop (manager);

  G_OBJECT_CLASS (gdu_sd_manager_parent_class)->finalize (object);
}

static gboolean
gdu_sd_manager_start (GduSdManager  *manager,
                      GError       **error)
{
  /* The reason for claiming a unique name is so it's easier to test
   * code changes - it helps ensure that only one instance of
   * GduSdMonitor is running at any one time. See also testplugin.c.
   */
  if (manager->name_owner_id == 0)
    {
      manager->name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                               "org.gnome.Disks.NotificationMonitor",
                                               G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                               G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                               NULL, /* bus_acquired_handler */
                                               name_acquired_handler,
                                               name_lost_handler,
                                               manager,
                                               NULL); /* GDestroyNotify */
    }

  return TRUE;
}

static void
gdu_sd_manager_stop (GduSdManager *manager)
{
  if (manager->name_owner_id == 0)
    {
      g_bus_unown_name (manager->name_owner_id);
      manager->name_owner_id = 0;
    }

  g_clear_object (&manager->monitor);
}

static void
gdu_sd_manager_class_init (GduSdManagerClass *klass)
{
  GObjectClass           *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gdu_sd_manager_finalize;
}

static GduSdManager *
gdu_sd_manager_new (void)
{
  return g_object_new (GDU_TYPE_SD_MANAGER, NULL);
}
