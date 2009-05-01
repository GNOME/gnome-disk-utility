/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  gnome-disk-utility-format.c
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
 *          David Zeuthen <davidz@redhat.com>
 *
 */

#include "config.h"

#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <glib/gthread.h>
#include <gio/gio.h>
#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>
#include <gtk/gtk.h>
#include <stdlib.h>

#include "gdu-format-dialog.h"
#include "gdu-format-progress-dialog.h"

typedef struct
{
        GMainLoop *loop;
        GError    *error;
        gchar     *mount_point;
} FormatData;

static gboolean
on_hack_timeout (gpointer user_data)
{
        GMainLoop *loop = user_data;
        g_main_loop_quit (loop);
        return FALSE;
}

static void
fs_mount_cb (GduDevice *device,
             gchar     *mount_point,
             GError    *error,
             gpointer   user_data)
{
        FormatData *data = user_data;
        if (error != NULL)
                data->error = error;
        else
                data->mount_point = g_strdup (mount_point);
        g_main_loop_quit (data->loop);
}

static void
fs_create_cb (GduDevice *device,
              GError    *error,
              gpointer   user_data)
{
        FormatData *data = user_data;
        if (error != NULL)
                data->error = error;
        g_main_loop_quit (data->loop);
}

static void
show_error_dialog (GtkWindow *parent,
                   const gchar *primary,
                   const gchar *secondary)
{
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new_with_markup (parent,
                                                     GTK_DIALOG_MODAL,
                                                     GTK_MESSAGE_ERROR,
                                                     GTK_BUTTONS_OK,
                                                     "<b><big><big>%s</big></big></b>\n\n%s",
                                                     primary,
                                                     secondary);
        gtk_widget_show_all (dialog);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
}

static void
launch_palimpsest (const gchar *device_file)
{
        gchar *command_line;
        GError *error;

        command_line = g_strdup_printf ("palimpsest --show-volume \"%s\"", device_file);

        error = NULL;
        if (!g_spawn_command_line_async (command_line, &error)) {
                show_error_dialog (NULL,
                                   _("Error launching Disk Utility"),
                                   error->message);
                g_error_free (error);
        }
        g_free (command_line);
}

static void
launch_file_manager (const gchar *mount_point)
{
        gchar *command_line;

        /* Nautilus itself will complain on errors so no need to do error handling*/
        command_line = g_strdup_printf ("nautilus \"%s\"", mount_point);
        g_spawn_command_line_async (command_line, NULL);
        g_free (command_line);
}

static gchar *device_file = NULL;
static GOptionEntry entries[] = {
        { "device-file", 'd', 0, G_OPTION_ARG_FILENAME, &device_file, N_("Device to format"), N_("DEVICE") },
        { NULL }
};

int
main (int argc, char *argv[])
{
        int ret;
        GError *error;
        GduPool *pool;
        GduDevice *device;
        GduDevice *device_to_mount;
        GduPresentable *volume;
        GtkWidget *dialog;
        gint response;
        gchar *fs_type;
        gchar *fs_label;
        gboolean encrypt;
        gchar *passphrase;
        gboolean save_passphrase_in_keyring;
        gboolean save_passphrase_in_keyring_session;
        GMainLoop *loop;

        ret = 1;
        pool = NULL;
        device = NULL;
        device_to_mount = NULL;
        volume = NULL;
        dialog = NULL;
        fs_type = NULL;
        fs_label = NULL;
        passphrase = NULL;
        loop = NULL;

        g_thread_init (NULL);

        /* Initialize gettext support */
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        /*  Initialize gtk  */
        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, NULL, entries, GETTEXT_PACKAGE, &error)) {
                g_printerr ("Could not parse arguments: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        loop = g_main_loop_new (NULL, FALSE);

        g_set_prgname ("gdu-format-tool");
        g_set_application_name (_("Gnome Disk Utility formatting tool"));

        if (device_file == NULL) {
                g_printerr ("Incorrect usage. Try --help.\n");
                goto out;
        }

        pool = gdu_pool_new ();
        if (pool == NULL) {
                g_warning ("Unable to get device pool");
                goto out;
        }

        device = gdu_pool_get_by_device_file (pool, device_file);
        if (device == NULL) {
                g_printerr ("No device for %s\n", device_file);
                goto out;
        }

        volume = gdu_pool_get_volume_by_device (pool, device);
        if (volume == NULL) {
                g_printerr ("%s is not a volume\n", device_file);
                goto out;
        }

        dialog = gdu_format_dialog_new (NULL, GDU_VOLUME (volume));
        gtk_widget_show_all (dialog);

        response = gtk_dialog_run (GTK_DIALOG (dialog));

        switch (response) {
        case GTK_RESPONSE_OK:
                break;

        case GTK_RESPONSE_ACCEPT:
                gtk_widget_destroy (dialog);
                dialog = NULL;
                launch_palimpsest (device_file);
                goto out;

        default: /* explicit fallthrough */
        case GTK_RESPONSE_CANCEL:
                goto out;
        }

        fs_type = gdu_format_dialog_get_fs_type (GDU_FORMAT_DIALOG (dialog));
        fs_label = gdu_format_dialog_get_fs_label (GDU_FORMAT_DIALOG (dialog));
        encrypt = gdu_format_dialog_get_encrypt (GDU_FORMAT_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        dialog = NULL;

        passphrase = NULL;
        save_passphrase_in_keyring = FALSE;
        save_passphrase_in_keyring_session = FALSE;
        if (encrypt) {
                passphrase = gdu_util_dialog_ask_for_new_secret (NULL,
                                                                 &save_passphrase_in_keyring,
                                                                 &save_passphrase_in_keyring_session);
                if (passphrase == NULL)
                        goto out;
        }

        gboolean take_ownership;
        FormatData data;

        take_ownership = (g_strcmp0 (fs_type, "vfat") != 0);

 try_again:
        data.loop = loop;
        data.error = NULL;
        dialog = gdu_format_progress_dialog_new (NULL,
                                                 device,
                                                 _("Formatting volume..."));
        gtk_widget_show_all (dialog);
        gdu_device_op_filesystem_create (device,
                                         fs_type,
                                         fs_label,
                                         passphrase,
                                         take_ownership,
                                         fs_create_cb,
                                         &data);
        g_main_loop_run (loop);
        if (data.error != NULL) {
                PolKitAction *pk_action;
                PolKitResult pk_result;
                if (gdu_error_check_polkit_not_authorized (data.error,
                                                           &pk_action,
                                                           &pk_result)) {
                        gtk_widget_destroy (dialog);
                        dialog = NULL;

                        if (pk_result == POLKIT_RESULT_UNKNOWN ||
                            pk_result == POLKIT_RESULT_NO) {
                                show_error_dialog (NULL,
                                                   _("You are not authorized to format the volume"),
                                                   _("Contact your system administrator to obtain "
                                                     "the necessary authorization."));
                                g_error_free (data.error);
                                polkit_action_unref (pk_action);
                                goto out;
                        } else {
                                char *action_id;
                                DBusError dbus_error;
                                polkit_action_get_action_id (pk_action, &action_id);
                                dbus_error_init (&dbus_error);
                                if (!polkit_auth_obtain (action_id, 0, getpid (), &dbus_error)) {
                                        polkit_action_unref (pk_action);
                                        dbus_error_free (&dbus_error);
                                        goto out;
                                } else {
                                        g_error_free (data.error);
                                        /* try again */
                                        goto try_again;
                                }
                        }

                } else {
                        gtk_widget_destroy (dialog);
                        dialog = NULL;

                        /* TODO: we could handle things like GDU_ERROR_BUSY here, e.g. unmount
                         *       and/or tear down LUKS mapping
                         */
                        show_error_dialog (NULL,
                                           _("Error creating filesystem"),
                                           data.error->message);
                        g_error_free (data.error);
                        goto out;
                }
        }

        /* ugh, DeviceKit-disks bug - spin around in the mainloop for some time to ensure we
         * have gotten all changes
         */
        gdu_format_progress_dialog_set_text (GDU_FORMAT_PROGRESS_DIALOG (dialog),
                                             _("Mounting volume..."));
        g_timeout_add (1500, on_hack_timeout, loop);
        g_main_loop_run (loop);

        /* OK, peachy, now mount the volume and open a window */
        if (passphrase != NULL) {
                const gchar *cleartext_objpath;

                g_assert (gdu_device_is_luks (device));
                cleartext_objpath = gdu_device_luks_get_holder (device);
                device_to_mount = gdu_pool_get_by_object_path (pool, cleartext_objpath);
        } else {
                device_to_mount = g_object_ref (device);
        }

        gdu_device_op_filesystem_mount (device_to_mount,
                                        NULL,
                                        fs_mount_cb,
                                        &data);
        g_main_loop_run (loop);
        gtk_widget_destroy (dialog);
        dialog = NULL;
        if (data.error != NULL) {
                show_error_dialog (NULL,
                                   _("Error mounting device"),
                                   data.error->message);
                g_error_free (data.error);
                goto out;
        }

        /* open file manager */
        launch_file_manager (data.mount_point);

        g_free (data.mount_point);

        /* save passphrase in keyring if requested */
        if (passphrase != NULL && (save_passphrase_in_keyring || save_passphrase_in_keyring_session)) {
                if (!gdu_util_save_secret (device,
                                           passphrase,
                                           save_passphrase_in_keyring_session)) {
                        show_error_dialog (NULL,
                                           _("Error storing passphrase in keyring"),
                                           "");
                        goto out;
                }
        }

        ret = 0;

 out:
        g_free (passphrase);
        g_free (fs_type);
        g_free (fs_label);
        g_free (device_file);
        if (loop != NULL)
                g_main_loop_unref (loop);
        if (dialog != NULL)
                gtk_widget_destroy (dialog);
        if (volume != NULL)
                g_object_unref (volume);
        if (device != NULL)
                g_object_unref (device);
        if (device_to_mount != NULL)
                g_object_unref (device_to_mount);
        if (pool != NULL)
                g_object_unref (pool);
        return ret;
}
