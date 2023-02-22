/*
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <gio/gio.h>
#include <libnotify/notify.h>

#include "gdusdmonitor.h"

static void
name_acquired_handler (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
  GduSdMonitor **monitor = user_data;

  g_warn_if_fail (*monitor == NULL);
  g_clear_object (monitor);
  *monitor = gdu_sd_monitor_new ();
}

static void
name_lost_handler (GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  GduSdMonitor **monitor = user_data;

  g_clear_object (monitor);
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop = NULL;
  guint name_owner_id = 0;
  GduSdMonitor *monitor = NULL;

  notify_init ("org.gnome.SettingsDaemon.DiskUtilityNotify");

  loop = g_main_loop_new (NULL, FALSE);

  /* The reason for claiming a unique name is so it's easier to test
   * code changes - it helps ensure that only one instance of
   * GduSdMonitor is running at any one time. See also
   * gdusdplugin.c:impl_activate().
   */
  name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                  "org.gnome.Disks.NotificationMonitor",
                                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                  G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                  NULL, /* bus_acquired_handler */
                                  name_acquired_handler,
                                  name_lost_handler,
                                  &monitor,
                                  NULL); /* GDestroyNotify */

  g_main_loop_run (loop);

  g_bus_unown_name (name_owner_id);
  g_main_loop_unref (loop);
  g_object_unref (monitor);

  return 0;
}
