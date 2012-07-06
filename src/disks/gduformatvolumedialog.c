/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduformatvolumedialog.h"
#include "gduvolumegrid.h"
#include "gduutils.h"
#include "gducreatefilesystemwidget.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GduWindow *window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *contents_box;
  GtkWidget *create_filesystem_widget;
} FormatVolumeData;

static void
format_volume_data_free (FormatVolumeData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->object);
  g_object_unref (data->block);
  g_clear_object (&data->drive);
  if (data->dialog != NULL)
    {
      gtk_widget_hide (data->dialog);
      gtk_widget_destroy (data->dialog);
    }
  if (data->builder != NULL)
    g_object_unref (data->builder);
  g_free (data);
}

static void
format_volume_update (FormatVolumeData *data)
{
  gboolean can_proceed;

  can_proceed = gdu_create_filesystem_widget_get_has_info (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
format_volume_property_changed (GObject     *object,
                                GParamSpec  *pspec,
                                gpointer     user_data)
{
  FormatVolumeData *data = user_data;
  format_volume_update (data);
}

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  FormatVolumeData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_block_call_format_finish (UDISKS_BLOCK (source_object),
                                        res,
                                        &error))
    {
      gdu_window_show_error (data->window, _("Error formatting volume"), error);
      g_error_free (error);
    }
  format_volume_data_free (data);
}

void
gdu_format_volume_dialog_show (GduWindow    *window,
                               UDisksObject *object)
{
  FormatVolumeData *data;
  gint response;

  data = g_new0 (FormatVolumeData, 1);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), data->block);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "format-volume-dialog.ui",
                                                         "format-volume-dialog",
                                                         &data->builder));

  data->contents_box = GTK_WIDGET (gtk_builder_get_object (data->builder, "contents-box"));
  data->create_filesystem_widget = gdu_create_filesystem_widget_new (gdu_window_get_application (window),
                                                                     data->drive,
                                                                     NULL); /* additional_fstypes */
  gtk_box_pack_start (GTK_BOX (data->contents_box),
                      data->create_filesystem_widget,
                      TRUE, TRUE, 0);
  g_signal_connect (data->create_filesystem_widget, "notify::has-info",
                    G_CALLBACK (format_volume_property_changed), data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  format_volume_update (data);

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (gdu_create_filesystem_widget_get_name_entry (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget)));

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      GVariantBuilder options_builder;
      const gchar *erase_type;
      const gchar *fstype;
      const gchar *name;
      const gchar *passphrase;
      const gchar *primary_message;
      GString *str;

      erase_type = gdu_create_filesystem_widget_get_erase (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
      fstype = gdu_create_filesystem_widget_get_fstype (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
      name = gdu_create_filesystem_widget_get_name (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
      passphrase = gdu_create_filesystem_widget_get_passphrase (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));

      gtk_widget_hide (data->dialog);

      primary_message = _("Are you sure you want to format the volume?");
      if (erase_type == NULL || g_strcmp0 (erase_type, "") == 0)
        {
          /* Translators: warning used for quick format of the volume*/
          str = g_string_new (_("All data on the volume will be lost but may still be recoverable by data recovery services"));
          g_string_append (str, "\n\n");
          g_string_append (str, _("<b>Tip</b>: If you are planning to recycle, sell or give away your old computer or disk, you should use a more thorough erase type to keep your private information from falling into the wrong hands"));
        }
      else
        {
          /* Translators: warning used when overwriting data of the volume */
          str = g_string_new (_("All data on the volume will be overwritten and will likely not be recoverable by data recovery services"));
        }

      if (!gdu_window_show_confirmation (window,
                                         primary_message,
                                         str->str,
                                         _("_Format")))
        {
          g_string_free (str, TRUE);
          goto out;
        }
      g_string_free (str, TRUE);

      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      if (name != NULL && strlen (name) > 0)
        g_variant_builder_add (&options_builder, "{sv}", "label", g_variant_new_string (name));
      if (!(g_strcmp0 (fstype, "vfat") == 0 || g_strcmp0 (fstype, "ntfs") == 0))
        {
          /* TODO: need a better way to determine if this should be TRUE */
          g_variant_builder_add (&options_builder, "{sv}", "take-ownership", g_variant_new_boolean (TRUE));
        }
      if (passphrase != NULL && strlen (passphrase) > 0)
        g_variant_builder_add (&options_builder, "{sv}", "encrypt.passphrase", g_variant_new_string (passphrase));

      if (erase_type != NULL)
        g_variant_builder_add (&options_builder, "{sv}", "erase", g_variant_new_string (erase_type));

      udisks_block_call_format (data->block,
                                fstype,
                                g_variant_builder_end (&options_builder),
                                NULL, /* GCancellable */
                                format_cb,
                                data);
      return;
    }
 out:
  format_volume_data_free (data);
}
