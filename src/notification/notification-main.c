/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* notification-main.c
 *
 * Copyright (C) 2009 David Zeuthen <davidz@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>

#include "gdu-slow-unmount-dialog.h"

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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
        GduPool *pool;

        GList *devices_being_unmounted;
} NotificationData;

/* ---------------------------------------------------------------------------------------------------- */

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

static gboolean
show_unmount_dialog (gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        GtkWidget *dialog;

        g_debug ("show unmount dialog for %s", gdu_device_get_device_file (device));

        dialog = gdu_slow_unmount_dialog_new (NULL, device);
        gtk_widget_show_all (GTK_WIDGET (dialog));
        gtk_window_present (GTK_WINDOW (dialog));

        /* remove the timeout */
        return FALSE;
}

static void
update_unmount_dialogs (NotificationData *data)
{
        GList *devices;
        GList *currently_being_unmounted;
        GList *l;
        GList *added;
        GList *removed;

        devices = gdu_pool_get_devices (data->pool);

        currently_being_unmounted = NULL;

        for (l = devices; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);

                /* TODO: maybe we shouldn't put up a dialog if other bits of the device
                 *       is busy (e.g. another mounted file system)
                 */

                if (!(gdu_device_job_in_progress (device) &&
                      g_strcmp0 (gdu_device_job_get_id (device), "FilesystemUnmount") == 0 &&
                      gdu_device_job_get_initiated_by_uid (device) == getuid ()))
                        continue;

                currently_being_unmounted = g_list_prepend (currently_being_unmounted, device);
        }

        currently_being_unmounted = g_list_sort (currently_being_unmounted, ptr_compare);
        data->devices_being_unmounted = g_list_sort (data->devices_being_unmounted, ptr_compare);

        added = removed = NULL;
        diff_sorted_lists (data->devices_being_unmounted,
                           currently_being_unmounted,
                           ptr_compare,
                           &added,
                           &removed);

        for (l = removed; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);
                guint countdown_timer_id;

                /* remove countdown timer if applicable */
                countdown_timer_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device),
                                                                          "unmount-countdown-timer-id"));
                if (countdown_timer_id > 0)
                        g_source_remove (countdown_timer_id);

                data->devices_being_unmounted = g_list_remove (data->devices_being_unmounted, device);
                g_object_unref (device);
        }

        for (l = added; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);
                guint countdown_timer_id;

                data->devices_being_unmounted = g_list_prepend (data->devices_being_unmounted, g_object_ref (device));

                /* start a countdown timer */
                countdown_timer_id = g_timeout_add (750,
                                                    show_unmount_dialog,
                                                    device);
                g_object_set_data (G_OBJECT (device),
                                   "unmount-countdown-timer-id",
                                   GUINT_TO_POINTER (countdown_timer_id));
        }

        g_list_foreach (devices, (GFunc) g_object_unref, NULL);
        g_list_free (devices);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_device_added (GduPool   *pool,
                 GduDevice *device,
                 gpointer   user_data)
{
        NotificationData *data = user_data;
        update_unmount_dialogs (data);
}

static void
on_device_removed (GduPool   *pool,
                   GduDevice *device,
                   gpointer   user_data)
{
        NotificationData *data = user_data;
        update_unmount_dialogs (data);
}

static void
on_device_changed (GduPool   *pool,
                   GduDevice *device,
                   gpointer   user_data)
{
        NotificationData *data = user_data;
        update_unmount_dialogs (data);
}

static void
on_device_job_changed (GduPool   *pool,
                       GduDevice *device,
                       gpointer   user_data)
{
        NotificationData *data = user_data;
        update_unmount_dialogs (data);
}

static NotificationData *
notification_data_new (void)
{
        NotificationData *data;
        data = g_new0 (NotificationData, 1);
        data->pool = gdu_pool_new ();
        g_signal_connect (data->pool, "device-added", G_CALLBACK (on_device_added), data);
        g_signal_connect (data->pool, "device-removed", G_CALLBACK (on_device_removed), data);
        g_signal_connect (data->pool, "device-changed", G_CALLBACK (on_device_changed), data);
        g_signal_connect (data->pool, "device-job-changed", G_CALLBACK (on_device_job_changed), data);
        return data;
}

static void
notification_data_free (NotificationData *data)
{
        g_signal_handlers_disconnect_by_func (data->pool, on_device_added, data);
        g_signal_handlers_disconnect_by_func (data->pool, on_device_removed, data);
        g_signal_handlers_disconnect_by_func (data->pool, on_device_changed, data);
        g_signal_handlers_disconnect_by_func (data->pool, on_device_job_changed, data);
        g_object_unref (data->pool);
        g_list_foreach (data->devices_being_unmounted, (GFunc) g_object_unref, NULL);
        g_list_free (data->devices_being_unmounted);
        g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
        NotificationData *data;

        gtk_init (&argc, &argv);

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        gtk_window_set_default_icon_name ("palimpsest");

        data = notification_data_new ();

        gtk_main ();

        notification_data_free (data);

        return 0;
}

/* ---------------------------------------------------------------------------------------------------- */
