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

typedef struct
{
        GduPool *pool;

        GtkStatusIcon *status_icon;

        /* List of GduDevice objects currently being unmounted */
        GList *devices_being_unmounted;

        /* List of GduDevice objects with ATA SMART failures */
        GList *ata_smart_failures;
} NotificationData;

/* ---------------------------------------------------------------------------------------------------- */

static void diff_sorted_lists (GList         *list1,
                               GList         *list2,
                               GCompareFunc   compare,
                               GList        **added,
                               GList        **removed);

static gint ptr_compare (gconstpointer a, gconstpointer b);

static void update_unmount_dialogs (NotificationData *data);

static void update_ata_smart_failures (NotificationData *data);

static void update_status_icon (NotificationData *data);

static void show_menu_for_status_icon (NotificationData *data);

/* ---------------------------------------------------------------------------------------------------- */

static void
update_all (NotificationData *data)
{
        update_unmount_dialogs (data);
        update_ata_smart_failures (data);
}

static void
on_device_added (GduPool   *pool,
                 GduDevice *device,
                 gpointer   user_data)
{
        NotificationData *data = user_data;
        update_all (data);
}

static void
on_device_removed (GduPool   *pool,
                   GduDevice *device,
                   gpointer   user_data)
{
        NotificationData *data = user_data;
        update_all (data);
}

static void
on_device_changed (GduPool   *pool,
                   GduDevice *device,
                   gpointer   user_data)
{
        NotificationData *data = user_data;
        update_all (data);
}

static void
on_device_job_changed (GduPool   *pool,
                       GduDevice *device,
                       gpointer   user_data)
{
        NotificationData *data = user_data;
        update_all (data);
}

static void
on_status_icon_activate (GtkStatusIcon *status_icon,
                         gpointer       user_data)
{
        NotificationData *data = user_data;
        show_menu_for_status_icon (data);
}

static void
on_status_icon_popup_menu (GtkStatusIcon *status_icon,
                           guint          button,
                           guint          activate_time,
                           gpointer       user_data)
{
        NotificationData *data = user_data;
        show_menu_for_status_icon (data);
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

        data->status_icon = gtk_status_icon_new ();
        gtk_status_icon_set_visible (data->status_icon, FALSE);
        gtk_status_icon_set_from_icon_name (data->status_icon, "gdu-warning");
        gtk_status_icon_set_tooltip_markup (data->status_icon, _("One or more disks are failing"));
        g_signal_connect (data->status_icon, "activate", G_CALLBACK (on_status_icon_activate), data);
        g_signal_connect (data->status_icon, "popup-menu", G_CALLBACK (on_status_icon_popup_menu), data);

        return data;
}

static void
notification_data_free (NotificationData *data)
{
        g_signal_handlers_disconnect_by_func (data->status_icon, on_status_icon_activate, data);
        g_signal_handlers_disconnect_by_func (data->status_icon, on_status_icon_popup_menu, data);
        g_object_unref (data->status_icon);

        g_signal_handlers_disconnect_by_func (data->pool, on_device_added, data);
        g_signal_handlers_disconnect_by_func (data->pool, on_device_removed, data);
        g_signal_handlers_disconnect_by_func (data->pool, on_device_changed, data);
        g_signal_handlers_disconnect_by_func (data->pool, on_device_job_changed, data);
        g_object_unref (data->pool);

        g_list_foreach (data->devices_being_unmounted, (GFunc) g_object_unref, NULL);
        g_list_free (data->devices_being_unmounted);
        g_list_foreach (data->ata_smart_failures, (GFunc) g_object_unref, NULL);
        g_list_free (data->ata_smart_failures);
        g_free (data);
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

static gboolean
show_unmount_dialog (gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        GtkWidget *dialog;

        //g_debug ("show unmount dialog for %s", gdu_device_get_device_file (device));

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
        GList *current;
        GList *l;
        GList *added;
        GList *removed;

        devices = gdu_pool_get_devices (data->pool);

        current = NULL;

        for (l = devices; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);

                /* TODO: maybe we shouldn't put up a dialog if other bits of the device
                 *       is busy (e.g. another mounted file system)
                 */

                if (!(gdu_device_job_in_progress (device) &&
                      g_strcmp0 (gdu_device_job_get_id (device), "FilesystemUnmount") == 0 &&
                      gdu_device_job_get_initiated_by_uid (device) == getuid ()))
                        continue;

                current = g_list_prepend (current, device);
        }

        current = g_list_sort (current, ptr_compare);
        data->devices_being_unmounted = g_list_sort (data->devices_being_unmounted, ptr_compare);

        added = removed = NULL;
        diff_sorted_lists (data->devices_being_unmounted,
                           current,
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
update_ata_smart_failures (NotificationData *data)
{
        GList *devices;
        GList *current;
        GList *l;
        GList *added;
        GList *removed;

        devices = gdu_pool_get_devices (data->pool);

        current = NULL;

        for (l = devices; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);

                if (!gdu_device_drive_ata_smart_get_is_available (device))
                        continue;

                if (!((gdu_device_drive_ata_smart_get_is_failing (device) &&
                       gdu_device_drive_ata_smart_get_is_failing_valid (device)) ||
                      gdu_device_drive_ata_smart_get_has_bad_sectors (device) ||
                      gdu_device_drive_ata_smart_get_has_bad_attributes (device)))
                        continue;

                current = g_list_prepend (current, device);
        }

        current = g_list_sort (current, ptr_compare);
        data->ata_smart_failures = g_list_sort (data->ata_smart_failures, ptr_compare);

        added = removed = NULL;
        diff_sorted_lists (data->ata_smart_failures,
                           current,
                           ptr_compare,
                           &added,
                           &removed);

        for (l = removed; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);

                //g_debug ("%s is no longer failing", gdu_device_get_device_file (device));

                data->ata_smart_failures = g_list_remove (data->ata_smart_failures, device);
                g_object_unref (device);
        }

        for (l = added; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);
                data->ata_smart_failures = g_list_prepend (data->ata_smart_failures, g_object_ref (device));

                //g_debug ("%s is now failing", gdu_device_get_device_file (device));
        }

        g_list_foreach (devices, (GFunc) g_object_unref, NULL);
        g_list_free (devices);

        update_status_icon (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_status_icon (NotificationData *data)
{
        gboolean show_icon;

        show_icon = FALSE;
        if (g_list_length (data->ata_smart_failures) > 0)
                show_icon = TRUE;

        if (!show_icon) {
                gtk_status_icon_set_visible (data->status_icon, FALSE);
                goto out;
        }

        gtk_status_icon_set_visible (data->status_icon, TRUE);
 out:
        ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_menu_item_activated (GtkMenuItem *menu_item,
                        gpointer     user_data)
{
        NotificationData *data = user_data;
        GduDevice *device;
        GdkScreen *screen;
        gchar *command_line;

        device = GDU_DEVICE (g_object_get_data (G_OBJECT (menu_item), "gdu-device"));

        screen = gtk_status_icon_get_screen (data->status_icon);
        command_line = g_strdup_printf ("palimpsest --show-drive=%s", gdu_device_get_device_file (device));
        gdk_spawn_command_line_on_screen (screen, command_line, NULL);
        g_free (command_line);
}

static void
show_menu_for_status_icon (NotificationData *data)
{
        GtkWidget *menu;
        GList *l;

        /* TODO: it would be nice to display something like
         *
         *              Select a disk to get more information...
         *       -----------------------------------------------
         *       [Icon] 80 GB ATA INTEL SSDSA2MH08
         *       [Icon] 250GB WD 2500JB External
         *
         * but unfortunately that would require fucking with gtk+'s
         * internals the same way the display-settings applet does
         * it; see e.g. line 951 of
         *
         * http://svn.gnome.org/viewvc/gnome-settings-daemon/trunk/plugins/xrandr/gsd-xrandr-manager.c?revision=810&view=markup
         */

        menu = gtk_menu_new ();
        for (l = data->ata_smart_failures; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);
                GduPresentable *presentable;
                gchar *device_name;
                GdkPixbuf *pixbuf;
                GtkWidget *image;
                GtkWidget *menu_item;

                presentable = gdu_pool_get_drive_by_device (data->pool, device);
                device_name = gdu_presentable_get_name (presentable);

                menu_item = gtk_image_menu_item_new_with_label (device_name);
                g_object_set_data_full (G_OBJECT (menu_item), "gdu-device", g_object_ref (device), g_object_unref);

                pixbuf = gdu_util_get_pixbuf_for_presentable (presentable, GTK_ICON_SIZE_MENU);
                image = gtk_image_new_from_pixbuf (pixbuf);
                g_object_unref (pixbuf);
                gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);

                g_signal_connect (menu_item,
                                  "activate",
                                  G_CALLBACK (on_menu_item_activated),
                                  data);

                gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

                g_free (device_name);
                g_object_unref (presentable);
        }
        gtk_widget_show_all (menu);

        gtk_menu_popup (GTK_MENU (menu),
                        NULL,
                        NULL,
                        gtk_status_icon_position_menu,
                        data->status_icon,
                        0,
                        gtk_get_current_event_time ());

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
        update_all (data);

        gtk_main ();

        notification_data_free (data);

        return 0;
}

/* ---------------------------------------------------------------------------------------------------- */
