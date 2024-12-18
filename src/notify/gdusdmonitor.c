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
#include <libnotify/notify.h>

#include <udisks/udisks.h>

#include "gdusdmonitor.h"

struct GduSdMonitorClass;
typedef struct GduSdMonitorClass GduSdMonitorClass;

struct GduSdMonitorClass {
  GObjectClass parent_class;
};

struct GduSdMonitor {
  GObject parent_instance;

  UDisksClient *client;

  /* ATA SMART problems */
  GList *ata_smart_problems;
  NotifyNotification *ata_smart_notification;
};

G_DEFINE_TYPE (GduSdMonitor, gdu_sd_monitor, G_TYPE_OBJECT);

static void on_client_changed (UDisksClient *client,
                               gpointer      user_data);

static void update (GduSdMonitor *monitor);

static void
udisks_client_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  GduSdMonitor *monitor = GDU_SD_MONITOR (user_data);
  GError *error = NULL;

  monitor->client = udisks_client_new_finish (res, &error);
  if (monitor->client == NULL)
    {
      g_warning ("Error initializing udisks client: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  else
    {
      g_signal_connect (monitor->client,
                        "changed",
                        G_CALLBACK (on_client_changed),
                        monitor);
      update (monitor);
    }
  g_object_unref (monitor);
}

static void
gdu_sd_monitor_init (GduSdMonitor *monitor)
{
  udisks_client_new (NULL, /* GCancellable* */
                     udisks_client_cb,
                     g_object_ref (monitor));
}

static void
gdu_sd_monitor_finalize (GObject *object)
{
  GduSdMonitor *monitor = GDU_SD_MONITOR (object);

  if (monitor->client != NULL)
    {
      g_signal_handlers_disconnect_by_func (monitor->client, on_client_changed, monitor);
      g_clear_object (&monitor->client);
    }

  g_list_free_full (monitor->ata_smart_problems, g_object_unref);
  g_clear_object (&monitor->ata_smart_notification);

  G_OBJECT_CLASS (gdu_sd_monitor_parent_class)->finalize (object);
}

static void
gdu_sd_monitor_class_init (GduSdMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gdu_sd_monitor_finalize;
}

GduSdMonitor *
gdu_sd_monitor_new (void)
{
  return GDU_SD_MONITOR (g_object_new (GDU_TYPE_SD_MONITOR, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
diff_sorted_lists (GList         *list1,
                   GList         *list2,
                   GCompareFunc   compare,
                   GList        **added,
                   GList        **removed)
{
  int order;

  *added = *removed = NULL;

  while (list1 != NULL &&
         list2 != NULL)
    {
      order = (*compare) (list1->data, list2->data);
      if (order < 0)
        {
          *removed = g_list_prepend (*removed, list1->data);
          list1 = list1->next;
        }
      else if (order > 0)
        {
          *added = g_list_prepend (*added, list2->data);
          list2 = list2->next;
        }
      else
        { /* same item */
          list1 = list1->next;
          list2 = list2->next;
        }
    }

  while (list1 != NULL)
    {
      *removed = g_list_prepend (*removed, list1->data);
      list1 = list1->next;
    }
  while (list2 != NULL)
    {
      *added = g_list_prepend (*added, list2->data);
      list2 = list2->next;
    }
}

static gint
ptr_compare (gconstpointer a, gconstpointer b)
{
  if (a > b)
    return 1;
  else if (a < b)
    return -1;
  else
    return 0;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*CheckProblemFunc) (GduSdMonitor   *monitor,
                                      UDisksObject   *object);

static void
update_problems (GduSdMonitor      *monitor,
                 GList            **problem_list,
                 CheckProblemFunc   check_func)
{
  GList *want = NULL;
  GList *added = NULL;
  GList *removed = NULL;
  GList *objects;
  GList *l;

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (monitor->client));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      if (check_func (monitor, object))
        want = g_list_prepend (want, object);
    }

  want = g_list_sort (want, ptr_compare);
  *problem_list = g_list_sort (*problem_list, ptr_compare);
  diff_sorted_lists (*problem_list,
                     want,
                     ptr_compare,
                     &added,
                     &removed);

  for (l = removed; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      *problem_list = g_list_remove (*problem_list, object);
      g_object_unref (object);
    }

  for (l = added; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      *problem_list = g_list_prepend (*problem_list, g_object_ref (object));
    }

  g_list_free (removed);
  g_list_free (added);
  g_list_free (want);
  g_list_free_full (objects, g_object_unref);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_examine_action_clicked (NotifyNotification  *notification,
                           const gchar         *action,
                           gpointer             user_data)
{
  GduSdMonitor *monitor = GDU_SD_MONITOR (user_data);
  const gchar *device_file = NULL;
  gchar *command_line = NULL;
  GAppInfo *app_info = NULL;
  GError *error = NULL;

  if (g_strcmp0 (action, "examine-smart") == 0)
    {
      if (monitor->ata_smart_problems != NULL)
        {
          UDisksObject *object = UDISKS_OBJECT (monitor->ata_smart_problems->data);
          if (object != NULL)
            {
              UDisksDrive *drive = udisks_object_peek_drive (object);
              if (drive != NULL)
                {
                  UDisksBlock *block = udisks_client_get_block_for_drive (monitor->client,
                                                                          drive,
                                                                          TRUE); /* get_physical */
                  if (block != NULL)
                    {
                      device_file = udisks_block_get_device (block);
                      g_object_ref (block);
                    }
                }
            }
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  if (device_file != NULL)
    command_line = g_strdup_printf ("gnome-disks --block-device %s", device_file);
  else
    command_line = g_strdup_printf ("gnome-disks");


  app_info = g_app_info_create_from_commandline (command_line,
                                                 NULL, /* application name */
                                                 G_APP_INFO_CREATE_SUPPORTS_STARTUP_NOTIFICATION,
                                                 NULL);
  if (!g_app_info_launch (app_info, NULL, NULL, &error))
    {
      g_warning ("Error launching gnome-disks: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_clear_object (&app_info);
  g_free (command_line);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_notification (GduSdMonitor        *monitor,
                     GList               *problems,
                     NotifyNotification **notification,
                     const gchar         *title,
                     const gchar         *text,
                     const gchar         *icon_name,
                     const gchar         *action,
                     const gchar         *action_label)
{
  if (g_list_length (problems) > 0)
    {
      /* it could be the notification has already been presented, in that
       * case, don't show another one
       */
      if (*notification == NULL)
        {
          *notification = notify_notification_new (title, text, icon_name);
          notify_notification_set_urgency (*notification, NOTIFY_URGENCY_CRITICAL);
          notify_notification_set_timeout (*notification, NOTIFY_EXPIRES_NEVER);
          notify_notification_set_hint_string (*notification, "desktop-entry", "gnome-disks");
          notify_notification_set_app_name (*notification, _("Disks"));
          notify_notification_add_action (*notification,
                                          action,
                                          action_label,
                                          (NotifyActionCallback) on_examine_action_clicked,
                                          monitor,
                                          NULL);
          notify_notification_show (*notification, NULL);
        }
    }
  else
    {
      if (*notification != NULL)
        {
          notify_notification_close (*notification, NULL);
          g_clear_object (notification);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */


static gboolean
check_for_ata_smart_problem (GduSdMonitor  *monitor,
                             UDisksObject  *object)
{
  gboolean ret = FALSE;
  UDisksDriveAta *ata = NULL;

  ata = udisks_object_peek_drive_ata (object);
  if (ata == NULL)
    goto out;

  /* For now we only check if the SMART status is set to FAIL
   *
   * - could add other heuristics (attributes failing, many bad sectors, temperature, etc.)
   * - could check if user wants to ignore the failure
   */
  if (!udisks_drive_ata_get_smart_failing (ata))
    goto out;

  ret = TRUE;

 out:
  return ret;
}

static void
update (GduSdMonitor *monitor)
{
  update_problems (monitor, &monitor->ata_smart_problems, check_for_ata_smart_problem);
  update_notification (monitor,
                       monitor->ata_smart_problems,
                       &monitor->ata_smart_notification,
                       /* Translators: This is used as the title of the SMART failure notification */
                       C_("notify-smart", "Hard Disk Problems Detected"),
                       /* Translators: This is used as the text of the SMART failure notification */
                       C_("notify-smart", "A hard disk is likely to fail soon."),
                       "org.gnome.DiskUtility",
                       "examine-smart",
                       /* Translators: Text for button in SMART failure notification */
                       C_("notify-smart", "Examine"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_client_changed (UDisksClient *client,
                   gpointer      user_data)
{
  GduSdMonitor *monitor = GDU_SD_MONITOR (user_data);
  update (monitor);
}
