/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  format-window-operation.c
 *
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Tomas Bzatek <tbzatek@redhat.com>
 *
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include <gdu-gtk/gdu-gtk.h>

#include <polkit-gnome/polkit-gnome.h>

#include "gdu-utils.h"
#include "format-window-operation.h"



/*  TODO: find a better way for this  */
#define DEVICE_SETTLE_TIMEOUT 3000


/*  Look whether the device needs to be partitioned  */
/*  - generally we don't want to have partitions on optical drives and floppy disks  */
static gboolean
device_needs_partition_table (GduDevice *device)
{
        gchar **media_compat;
        gboolean needs = TRUE;  /*  default to TRUE  */

        media_compat = gdu_device_drive_get_media_compatibility (device);
        for (; *media_compat; media_compat++) {
                g_debug ("     compat '%s'\n", *media_compat);
                /*  http://hal.freedesktop.org/docs/DeviceKit-disks/Device.html#Device:drive-media-compatibility  */
                if (strstr (*media_compat, "optical") == *media_compat ||
                    strstr (*media_compat, "floppy") == *media_compat) {
                        needs = FALSE;
                        break;
                }
        }
#if 0
        g_strfreev (media_compat);   /* so, is this const then?  */
#endif
        g_debug ("device_needs_partition_table = %d", needs);
        return needs;
}

/* -------------------------------------------------------------------------- */

static gboolean
job_progress_pulse_timeout_handler (gpointer user_data)
{
        FormatProcessData *data = user_data;

        g_return_val_if_fail (data != NULL, TRUE);

        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (data->priv->progress_bar));

        return TRUE;
}

static void
do_progress_bar_update (FormatProcessData *data,
                        const gchar       *label,
                        gdouble            percentage,
                        gboolean           active)
{
        if (active) {
                if (label)
                        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (data->priv->progress_bar), label);

                if (percentage < 0) {
                        gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (data->priv->progress_bar), 2.0 / 50);
                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (data->priv->progress_bar));
                        if (data->job_progress_pulse_timer_id == 0) {
                                data->job_progress_pulse_timer_id = g_timeout_add (
                                                                                   1000 / 50,
                                                                                   job_progress_pulse_timeout_handler,
                                                                                   data);
                        }
                } else {
                        if (data->job_progress_pulse_timer_id > 0) {
                                g_source_remove (data->job_progress_pulse_timer_id);
                                data->job_progress_pulse_timer_id = 0;
                        }
                        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (data->priv->progress_bar),
                                                       percentage / 100.0);
                }
        }
        else {
                if (data->job_progress_pulse_timer_id > 0) {
                        g_source_remove (data->job_progress_pulse_timer_id);
                        data->job_progress_pulse_timer_id = 0;
                }
        }
}

static void
presentable_job_changed (GduPresentable *presentable,
                         gpointer        user_data)
{
        FormatProcessData *data = user_data;
        gchar *job_description;
        gdouble percentage;

        g_return_if_fail (data != NULL);

        if (data->device != NULL && gdu_device_job_in_progress (data->device)) {
                job_description = gdu_get_job_description (gdu_device_job_get_id (data->device));

                percentage = gdu_device_job_get_percentage (data->device);
                do_progress_bar_update (data, job_description, percentage, TRUE);

                g_free (job_description);

        } else {
                /*  do_progress_bar_update (data, NULL, -1, FALSE);  */
                /*  Mask inactivity by bouncing -- this should be fixed in libgdu  */
                do_progress_bar_update (data, NULL, -1, TRUE);
        }
}


/* -------------------------------------------------------------------------- */

static void unmount_auth_end_callback (PolKitGnomeAction *action, gboolean gained_privilege, gpointer user_data);
static void format_auth_end_callback (PolKitGnomeAction *action, gboolean gained_privilege, gpointer user_data);
static void format_action_callback (GtkAction *action, gpointer user_data);
static void part_modify_auth_end_callback (PolKitGnomeAction *action, gboolean gained_privilege, gpointer user_data);
static void part_modify_action_callback (GtkAction *action, gpointer user_data);
static void part_table_new_auth_end_callback (PolKitGnomeAction *action, gboolean gained_privilege, gpointer user_data);
static void part_table_new_action_callback (GtkAction *action, gpointer user_data);
static void part_new_auth_end_callback (PolKitGnomeAction *action, gboolean gained_privilege, gpointer user_data);
static void part_new_action_callback (GtkAction *action, gpointer user_data);

void
update_ui_progress (FormatDialogPrivate *priv,
                    FormatProcessData   *data,
                    gboolean             working)
{
        g_return_if_fail (priv != NULL);

        priv->job_running = working;

        if (working) {
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (priv->progress_bar), NULL);
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progress_bar), 0.0);
                gtk_button_set_label (GTK_BUTTON (priv->close_button), GTK_STOCK_STOP);
                gtk_widget_show (priv->progress_bar_box);
                gtk_widget_hide (priv->all_controls_box);
                if (data) {
                        g_signal_connect (data->presentable, "job-changed", G_CALLBACK (presentable_job_changed), data);

                        /*  set up PolicyKit actions  */
                        data->pk_format_action = polkit_action_new ();
                        polkit_action_set_action_id (data->pk_format_action, "org.freedesktop.devicekit.disks.change");
                        data->format_action = polkit_gnome_action_new_default ("format", data->pk_format_action, NULL, NULL);
                        g_signal_connect (data->format_action, "auth-end", G_CALLBACK (format_auth_end_callback), data);
                        g_signal_connect (data->format_action, "activate", G_CALLBACK (format_action_callback), data);

                        data->pk_part_modify_action = polkit_action_new ();
                        /*  action_id is the same as for format, but sometimes authentication is one shot  */
                        polkit_action_set_action_id (data->pk_part_modify_action, "org.freedesktop.devicekit.disks.change");
                        data->part_modify_action = polkit_gnome_action_new_default ("part_modify", data->pk_part_modify_action, NULL, NULL);
                        g_signal_connect (data->part_modify_action, "auth-end", G_CALLBACK (part_modify_auth_end_callback), data);
                        g_signal_connect (data->part_modify_action, "activate", G_CALLBACK (part_modify_action_callback), data);

                        data->pk_part_table_new_action = polkit_action_new ();
                        polkit_action_set_action_id (data->pk_part_table_new_action, "org.freedesktop.devicekit.disks.change");
                        data->part_table_new_action = polkit_gnome_action_new_default ("part_table_new", data->pk_part_table_new_action, NULL, NULL);
                        g_signal_connect (data->part_table_new_action, "auth-end", G_CALLBACK (part_table_new_auth_end_callback), data);
                        g_signal_connect (data->part_table_new_action, "activate", G_CALLBACK (part_table_new_action_callback), data);

                        data->pk_part_new_action = polkit_action_new ();
                        polkit_action_set_action_id (data->pk_part_new_action, "org.freedesktop.devicekit.disks.change");
                        data->part_new_action = polkit_gnome_action_new_default ("part_new", data->pk_part_new_action, NULL, NULL);
                        g_signal_connect (data->part_new_action, "auth-end", G_CALLBACK (part_new_auth_end_callback), data);
                        g_signal_connect (data->part_new_action, "activate", G_CALLBACK (part_new_action_callback), data);

                }
        }
        else
                {
                        if (data) {
                                g_signal_handlers_disconnect_matched (data->format_action, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);
                                g_signal_handlers_disconnect_matched (data->part_modify_action, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);
                                g_signal_handlers_disconnect_matched (data->part_table_new_action, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);
                                g_signal_handlers_disconnect_matched (data->part_new_action, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);

                                /*  destroy PolicyKit actions  */
                                polkit_action_unref (data->pk_format_action);
                                g_object_unref (data->format_action);
                                polkit_action_unref (data->pk_part_modify_action);
                                g_object_unref (data->part_modify_action);
                                polkit_action_unref (data->pk_part_table_new_action);
                                g_object_unref (data->part_table_new_action);
                                polkit_action_unref (data->pk_part_new_action);
                                g_object_unref (data->part_new_action);

                                g_signal_handlers_disconnect_matched (data->presentable, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);
                                if (data->job_progress_pulse_timer_id > 0) {
                                        g_source_remove (data->job_progress_pulse_timer_id);
                                        data->job_progress_pulse_timer_id = 0;
                                }
                        }
                        gtk_widget_show (priv->all_controls_box);
                        gtk_widget_hide (priv->progress_bar_box);
                        gtk_button_set_label (GTK_BUTTON (priv->close_button), GTK_STOCK_CLOSE);
                }
        update_ui_controls (priv);
}

static void
free_format_action_data (FormatProcessData *data)
{
        if (data) {
                update_ui_progress (data->priv, data, FALSE);
                if (data->presentable != NULL)
                        g_object_unref (data->presentable);
                if (data->device != NULL)
                        g_object_unref (data->device);
                g_free (data->encrypt_passphrase);
                g_free (data->fslabel);
                g_free (data);
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
action_finished (FormatProcessData *data,
                 gchar             *new_device_path)
{
        GduDevice *new_device;
        GduPresentable *new_presentable = NULL;

        g_return_if_fail (data != NULL);

        /*  we don't want to destroy objects at this point, don't pass data in */
        update_ui_progress (data->priv, NULL, FALSE);

        /*  change to the new device  */
        if (new_device_path) {
                new_device = gdu_pool_get_by_object_path (data->priv->pool, new_device_path);
                if (new_device) {
                        g_object_unref (data->device);
                        data->device = new_device;
                        new_presentable = gdu_pool_get_volume_by_device (data->priv->pool, new_device);
                        if (new_presentable) {
                                /*  switch to new presentable  */
                                g_debug ("setting new presentable...");
                        }
                } else {
                        g_warning ("action_finished: cannot find device for the %s device path", new_device_path);
                }
                g_free (new_device_path);
        }

        /*  Force refresh of the new presentable  */
        select_new_presentable (data->priv, new_presentable != NULL ? new_presentable : data->priv->presentable);
        if (new_presentable)
                g_object_unref (new_presentable);

        /*  TODO: show encryption info somewhere?  */
        if (data->encrypt_passphrase != NULL) {
                /* now set the passphrase if requested */
                if (data->save_in_keyring || data->save_in_keyring_session) {
                        gdu_util_save_secret (data->device,
                                              data->encrypt_passphrase,
                                              data->save_in_keyring_session);
                }
        }
}

/* -------------------------------------------------------------------------- */
static void
fix_focus_cb (GtkDialog *dialog,
              gpointer   data)
{
        GtkWidget *button;

        button = gtk_window_get_default_widget (GTK_WINDOW (dialog));
        gtk_widget_grab_focus (button);
}

static void
expander_cb (GtkExpander *expander,
             GParamSpec  *pspec,
             GtkWindow   *dialog)
{
        gtk_window_set_resizable (dialog, gtk_expander_get_expanded (expander));
}

/*  keep in sync with gdu-shell.c/gdu_shell_raise_error()                                               */
static void
nautilus_gdu_show_error (GtkWidget      *parent_window,
                         GduPresentable *presentable,
                         GError         *error,
                         const gchar    *primary_markup_format,
                         ...)
{
        GtkWidget *dialog;
        gchar *error_text;
        gchar *window_title;
        GIcon *window_icon;
        va_list args;
        GtkWidget *box, *hbox, *expander, *sw, *tv;
        GList *children;
        GtkTextBuffer *buffer;

        g_return_if_fail (presentable != NULL);
        g_return_if_fail (error != NULL);

        window_title = gdu_presentable_get_name (presentable);
        window_icon = gdu_presentable_get_icon (presentable);

        va_start (args, primary_markup_format);
        error_text = g_strdup_vprintf (primary_markup_format, args);
        va_end (args);

        dialog = gtk_message_dialog_new_with_markup (
                                                     GTK_WINDOW (parent_window),
                                                     GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_MESSAGE_ERROR,
                                                     GTK_BUTTONS_CLOSE,
                                                     "<big><b>%s</b></big>",
                                                     error_text);
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
                                                  trim_dk_error (error->message));

        gtk_window_set_title (GTK_WINDOW (dialog), window_title);
        /*  TODO: no support for GIcon in GtkWindow  */
        /*  gtk_window_set_icon_name (GTK_WINDOW (dialog), window_icon_name);  */

        g_signal_connect_swapped (dialog,
                                  "response",
                                  G_CALLBACK (gtk_main_quit),
                                  NULL);
        gtk_window_present (GTK_WINDOW (dialog));

        g_free (window_title);
        if (window_icon != NULL)
                g_object_unref (window_icon);
        g_free (error_text);
}

/* -------------------------------------------------------------------------- */

static void
modify_partition_completed (GduDevice  *device,
                            GError     *error,
                            gpointer    user_data)
{
        FormatProcessData *data = user_data;

        g_debug ("modify_partition_completed");
        g_return_if_fail (data != NULL);

        if (error != NULL) {
                nautilus_gdu_show_error (GTK_WIDGET (data->priv->dialog),
                                         data->presentable,
                                         error,
                                         _("Error modifying partition"));
                g_error_free (error);
        }
        else
                {
                        /*  -- don't refresh here, wait for the "changed" callback
                            update_ui (data->priv);  */
                }
        /*  save encryption info even if operation fails  */
        action_finished (data, NULL);
        free_format_action_data (data);
}

static void
part_modify_action_callback (GtkAction *action,
                             gpointer   user_data)
{
        FormatProcessData *data = user_data;

        g_return_if_fail (data != NULL);
        g_debug ("part_modify_action_callback");

        if (data->priv->job_cancelled)
                return;

        /*  DK is buggy, passing a label string causes the operation to fail  */
        gdu_device_op_partition_modify (data->device, data->recommended_part_type, NULL, NULL, modify_partition_completed, data);
}

static void
part_modify_auth_end_callback (PolKitGnomeAction *action,
                               gboolean           gained_privilege,
                               gpointer           user_data)
{
        FormatProcessData *data = user_data;

        g_return_if_fail (data != NULL);
        g_debug ("part_modify_auth_end_callback");

        if (! gained_privilege) {
                /*  cancel the whole operation  */
                free_format_action_data (data);
        }
        else {
                /*  positive reply should be handled by part_modify_action_callback  */
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
format_action_completed (GduDevice  *device,
                         GError     *error,
                         gpointer    user_data)
{
        FormatProcessData *data = user_data;
        const gchar *part_type;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;

        g_debug ("format_action_completed");
        g_return_if_fail (data != NULL);

        if (error != NULL) {
                nautilus_gdu_show_error (GTK_WIDGET (data->priv->dialog),
                                         data->presentable,
                                         error,
                                         _("Error creating partition"));
                g_error_free (error);
        }
        else
                {
                        /*  Get device scheme if needed  */
                        if (! data->scheme || strlen (data->scheme) == 0) {
                                toplevel_presentable = gdu_presentable_get_toplevel (data->presentable);
                                if (toplevel_presentable) {
                                        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
                                        if (toplevel_device) {
                                                data->scheme = gdu_device_partition_table_get_scheme (toplevel_device);
                                                g_object_unref (toplevel_device);
                                        }
                                        g_object_unref (toplevel_presentable);
                                }
                        }

                        /*  Correct partition type  */
                        if (! data->priv->job_cancelled && data->scheme && strlen (data->scheme) > 0) {
                                part_type = gdu_device_partition_get_type (device);
                                data->recommended_part_type = gdu_util_get_default_part_type_for_scheme_and_fstype (data->scheme, data->fstype, gdu_device_partition_get_size (device));
                                g_debug ("format_action_completed: part_type = %s, recommended_part_type = %s", part_type, data->recommended_part_type);
                                /*  Change partition type if necessary  */
                                if (strcmp (part_type, data->recommended_part_type) != 0)
                                        {
                                                g_debug ("changing part type to %s, device = %s", data->recommended_part_type, gdu_device_get_device_file (device));
                                                gtk_action_activate (GTK_ACTION (data->part_modify_action));
                                                return;  /*  don't change the UI yet  */
                                        }
                        }

                        /*  formatting finished  */
                        action_finished (data, NULL);
                }
        free_format_action_data (data);
}

static void
format_action_callback (GtkAction *action,
                        gpointer   user_data)
{
        FormatProcessData *data = user_data;

        g_return_if_fail (data != NULL);
        g_debug ("format_action_callback");

        if (data->priv->job_cancelled)
                return;

        gdu_device_op_filesystem_create (data->device,
                                         data->fstype,
                                         data->fslabel,
                                         data->encrypt_passphrase,
                                         data->take_ownership,
                                         format_action_completed,
                                         data);
}

static void
format_auth_end_callback (PolKitGnomeAction *action,
                          gboolean           gained_privilege,
                          gpointer           user_data)
{
        FormatProcessData *data = user_data;

        g_return_if_fail (data != NULL);
        g_debug ("format_auth_end_callback");

        if (! gained_privilege) {
                /*  cancel the whole operation  */
                free_format_action_data (data);
        }
        else {
                /*  positive reply should be handled by format_action_callback  */
        }
}


/* ---------------------------------------------------------------------------------------------------- */

static gboolean
part_table_new_timeout_handler (gpointer user_data)
{
        FormatProcessData *data = user_data;

        g_return_val_if_fail (data != NULL, FALSE);
        g_debug ("part_table_new_timeout_handler");

        gtk_action_activate (GTK_ACTION (data->part_new_action));

        return FALSE;
}

static void
part_table_new_completed (GduDevice *device,
                          GError    *error,
                          gpointer   user_data)
{
        FormatProcessData *data = user_data;

        /*  BUG: callback shouldn't be spawned until all changes are reflected in pool  */
        g_return_if_fail (data != NULL);

        g_debug ("part_table_new_completed");
        update_ui_controls (data->priv);

        if (error != NULL) {
                nautilus_gdu_show_error (GTK_WIDGET (data->priv->dialog),
                                         data->presentable,
                                         error,
                                         _("Error creating new partition table"));
                free_format_action_data (data);
                g_error_free (error);
        }
        else
                {
                        g_debug ("  creating partition...");
                        if (data->priv->job_cancelled)
                                return;
                        /*  TODO: we should wait here for proper refresh  */
                        g_timeout_add (DEVICE_SETTLE_TIMEOUT, part_table_new_timeout_handler, data);
                        do_progress_bar_update (data, _("Waiting for device to settle..."), -1, TRUE);
                        /*  gtk_action_activate (GTK_ACTION (data->part_new_action));  -- disabled  */
                }
}

static void
part_table_new_action_callback (GtkAction *action,
                                gpointer   user_data)
{
        FormatProcessData *data = user_data;

        g_return_if_fail (data != NULL);
        g_debug ("part_table_new_action_callback");

        if (data->priv->job_cancelled)
                return;

        /*  default to MBR  */
        data->scheme = "mbr";

        gdu_device_op_partition_table_create (data->device, data->scheme, part_table_new_completed, data);
}

static void
part_table_new_auth_end_callback (PolKitGnomeAction *action,
                                  gboolean           gained_privilege,
                                  gpointer           user_data)
{
        FormatProcessData *data = user_data;

        g_return_if_fail (data != NULL);
        g_debug ("part_table_new_auth_end_callback");

        if (! gained_privilege) {
                /*  cancel the whole operation  */
                free_format_action_data (data);
        }
        else {
                /*  positive reply should be handled by unmount_action_callback  */
        }
}

/* -------------------------------------------------------------------------- */

static void
part_new_completed (GduDevice *device,
                    gchar     *created_device_object_path,
                    GError    *error,
                    gpointer   user_data)
{
        FormatProcessData *data = user_data;

        /*  BUG: callback shouldn't be spawned until all changes are reflected in pool  */
        g_return_if_fail (data != NULL);

        g_debug ("part_new_completed, created_device_object_path = %s", error == NULL ? created_device_object_path : NULL);

        if (error != NULL) {
                nautilus_gdu_show_error (GTK_WIDGET (data->priv->dialog),
                                         data->presentable,
                                         error,
                                         _("Error creating new partition"));
                g_error_free (error);
        }
        else
                {
                        /*  formatting finished  */
                        /*  TODO: we should wait here for proper refresh  */
                        action_finished (data, g_strdup (created_device_object_path));
                }
        free_format_action_data (data);
}

static void
part_new_action_callback (GtkAction *action,
                          gpointer   user_data)
{
        FormatProcessData *data = user_data;
        guint64 offset;
        guint64 size;
        gchar *type;

        g_return_if_fail (data != NULL);
        g_debug ("part_new_action_callback, device = %s", gdu_device_get_device_file (data->device));

        if (data->priv->job_cancelled)
                return;

        offset = gdu_presentable_get_offset (data->presentable);
        size = gdu_presentable_get_size (data->presentable);

        if (! data->scheme || strlen (data->scheme) == 0)
                data->scheme = gdu_device_partition_table_get_scheme (data->device);   /*  we should have toplevel device here  */
        if (! data->scheme || strlen (data->scheme) == 0)
                data->scheme = "mbr";   /*  default to MBR  */

        type = gdu_util_get_default_part_type_for_scheme_and_fstype (data->scheme, data->fstype, size);

        g_debug ("creating new partition, offset = %lu, size = %lu, scheme = %s, type = %s", offset, size, data->scheme, type);

        gdu_device_op_partition_create (data->device, offset, size, type, NULL, NULL,
                                        data->fstype, data->fslabel, data->encrypt_passphrase, data->take_ownership,
                                        part_new_completed, data);
        g_free (type);
}

static void
part_new_auth_end_callback (PolKitGnomeAction *action,
                            gboolean           gained_privilege,
                            gpointer           user_data)
{
        FormatProcessData *data = user_data;

        g_return_if_fail (data != NULL);
        g_debug ("part_new_auth_end_callback");

        if (! gained_privilege) {
                /*  cancel the whole operation  */
                free_format_action_data (data);
        }
        else {
                /*  positive reply should be handled by unmount_action_callback  */
        }
}

/* ---------------------------------------------------------------------------------------------------- */

/*  taken from palimpsest/gdu-section-unrecognized.c  */
void
do_format (FormatDialogPrivate *priv)
{
        FormatProcessData *data;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        gboolean create_new_part_table = FALSE;
        gboolean create_new_partition = FALSE;
        GduKnownFilesystem *kfs;
        gint part_combo_item_index;


        priv->job_cancelled = FALSE;
        data = g_new0 (FormatProcessData, 1);
        data->priv = priv;
        data->job_progress_pulse_timer_id = 0;
        toplevel_presentable = NULL;
        toplevel_device = NULL;


        data->presentable = g_object_ref (priv->presentable);
        toplevel_presentable = gdu_presentable_get_toplevel (data->presentable);
        if (toplevel_presentable == NULL) {
                g_warning ("%s: no toplevel presentable",  __FUNCTION__);
        }
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                free_format_action_data (data);
                goto out;
        }
        data->device = gdu_presentable_get_device (data->presentable);

        if (data->device == NULL && toplevel_device != NULL) {
                /*  no device, i.e. partition table exists but no partition  */
                data->device = g_object_ref (toplevel_device);
                create_new_part_table = FALSE;
                create_new_partition = TRUE;
                g_debug ("Partition table exists but has no partition for the selected device.");
        } else
                if (toplevel_device != NULL && ! gdu_device_is_partition_table (toplevel_device)) {
                        /*  no partition table on the device, create partition table first.  */
                        /*  also empty (zeroed) device  */
                        create_new_part_table = TRUE;
                        create_new_partition = TRUE;
                        g_debug ("Device is known but doesn't have partition table, we need to create it first.");
                } else
                        if (toplevel_device != NULL && data->device != NULL && toplevel_device == data->device && device_needs_partition_table (data->device)) {
                                /*  device is toplevel, check if we need new partitions  */
                                create_new_partition = TRUE;
                                g_debug ("Device is known but requires partitioning, we'll create new one.");
                        }

        if (data->device == NULL) {
                g_warning ("%s: device is not supposed to be NULL",  __FUNCTION__);
                free_format_action_data (data);
                goto out;
        }

        part_combo_item_index = gtk_combo_box_get_active (GTK_COMBO_BOX (priv->part_type_combo_box));
        if (part_combo_item_index < 0 || part_combo_item_index >= (int) G_N_ELEMENTS (filesystem_combo_items)) {
                g_warning ("%s: no valid filesystem type specified",  __FUNCTION__);
                free_format_action_data (data);
                goto out;
        }
        data->fstype = filesystem_combo_items[part_combo_item_index].fstype;
        data->fslabel = g_strdup (GTK_WIDGET_IS_SENSITIVE (priv->label_entry) ?
                                  gtk_entry_get_text (GTK_ENTRY (priv->label_entry)) : "");

        data->take_ownership = FALSE;
        kfs = gdu_pool_get_known_filesystem_by_id (priv->pool, data->fstype);
        if (kfs != NULL) {
                if (gdu_known_filesystem_get_supports_unix_owners (kfs))
                        data->take_ownership = TRUE;
                g_object_unref (kfs);
        }

        update_ui_progress (priv, data, TRUE);

        if (filesystem_combo_items[part_combo_item_index].encrypted) {
                data->encrypt_passphrase = gdu_util_dialog_ask_for_new_secret (GTK_WIDGET (priv->dialog),
                                                                               &data->save_in_keyring,
                                                                               &data->save_in_keyring_session);
                if (data->encrypt_passphrase == NULL) {
                        free_format_action_data (data);
                        goto out;
                }
        }

        if (create_new_part_table && device_needs_partition_table (data->device)) {
                /*  device is zeroed, create partition table first  */
                gtk_action_activate (GTK_ACTION (data->part_table_new_action));
        } else
                if (create_new_partition && device_needs_partition_table (data->device)) {
                        /*  device has partition table but has no partition  */
                        gtk_action_activate (GTK_ACTION (data->part_new_action));
                } else {
                        gtk_action_activate (GTK_ACTION (data->format_action));
                }

 out:
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
}
