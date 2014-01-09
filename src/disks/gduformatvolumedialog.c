/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gdk/gdkx.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduformatvolumedialog.h"
#include "gduvolumegrid.h"
#include "gducreatefilesystemwidget.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GtkWindow *parent_window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *contents_box;
  GtkWidget *create_filesystem_widget;

  GCallback finished_cb;
  gpointer  cb_data;
} FormatVolumeData;

static void
format_volume_data_free (FormatVolumeData *data)
{
  if (data->finished_cb)
    ((GDestroyNotify) data->finished_cb) (data->cb_data);
  g_clear_object (&data->parent_window);
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
      gdu_utils_show_error (GTK_WINDOW (data->parent_window), _("Error formatting volume"), error);
      g_error_free (error);
    }
  format_volume_data_free (data);
}

static void
ensure_unused_cb (UDisksClient  *client,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  FormatVolumeData *data = user_data;
  GVariantBuilder options_builder;
  const gchar *erase_type;
  const gchar *fstype;
  const gchar *name;
  const gchar *passphrase;

  if (!gdu_utils_ensure_unused_finish (client, res, NULL))
    {
      format_volume_data_free (data);
      goto out;
    }

  erase_type = gdu_create_filesystem_widget_get_erase (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
  fstype = gdu_create_filesystem_widget_get_fstype (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
  name = gdu_create_filesystem_widget_get_name (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
  passphrase = gdu_create_filesystem_widget_get_passphrase (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));

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

  g_variant_builder_add (&options_builder, "{sv}", "update-partition-type", g_variant_new_boolean (TRUE));

  udisks_block_call_format (data->block,
                            fstype,
                            g_variant_builder_end (&options_builder),
                            NULL, /* GCancellable */
                            format_cb,
                            data);

 out:
  ;
}

static void
gdu_format_volume_dialog_show_internal (UDisksClient *client,
                                        GtkWindow    *parent_window,
                                        gint          parent_xid,
                                        UDisksObject *object,
                                        GCallback     finished_cb,
                                        gpointer      cb_data)
{
  GduApplication *app = GDU_APPLICATION (g_application_get_default ());
  FormatVolumeData *data;
  gint response;

  data = g_new0 (FormatVolumeData, 1);
  data->finished_cb = finished_cb;
  data->cb_data = cb_data;
  data->parent_window = (parent_window != NULL) ? g_object_ref (parent_window) : NULL;
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->drive = udisks_client_get_drive_for_block (client, data->block);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (app,
                                                         "format-volume-dialog.ui",
                                                         "format-volume-dialog",
                                                         &data->builder));

  data->contents_box = GTK_WIDGET (gtk_builder_get_object (data->builder, "contents-box"));
  data->create_filesystem_widget = gdu_create_filesystem_widget_new (app,
                                                                     data->drive,
                                                                     NULL); /* additional_fstypes */
  gtk_box_pack_start (GTK_BOX (data->contents_box),
                      data->create_filesystem_widget,
                      TRUE, TRUE, 0);
  g_signal_connect (data->create_filesystem_widget, "notify::has-info",
                    G_CALLBACK (format_volume_property_changed), data);

  if (parent_window != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (data->dialog), parent_window);
    }
  else if (parent_xid != -1)
    {
      GdkWindow *foreign_window = gdk_x11_window_foreign_new_for_display (gdk_display_get_default (), parent_xid);
      if (!gtk_widget_get_realized (data->dialog))
          gtk_widget_realize (data->dialog);
      if (foreign_window != NULL)
        gdk_window_set_transient_for (gtk_widget_get_window (data->dialog), foreign_window);
    }

  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  format_volume_update (data);

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (gdu_create_filesystem_widget_get_name_entry (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget)));

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      const gchar *primary_message;
      const gchar *erase_type;
      GString *str;
      GList *objects = NULL;

      gtk_widget_hide (data->dialog);

      erase_type = gdu_create_filesystem_widget_get_erase (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));

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

      objects = g_list_append (NULL, object);
      if (!gdu_utils_show_confirmation (GTK_WINDOW (data->parent_window),
                                        primary_message,
                                        str->str,
                                        _("_Format"),
                                        NULL, NULL,
                                        client, objects))
        {
          g_list_free (objects);
          g_string_free (str, TRUE);
          goto out;
        }

      g_list_free (objects);
      g_string_free (str, TRUE);

      /* ensure the volume is unused (e.g. unmounted) before formatting it... */
      gdu_utils_ensure_unused (client,
                               GTK_WINDOW (data->parent_window),
                               data->object,
                               (GAsyncReadyCallback) ensure_unused_cb,
                               NULL, /* GCancellable */
                               data);
      return;
    }
 out:
  format_volume_data_free (data);
}

void
gdu_format_volume_dialog_show_for_xid (UDisksClient *client,
                                       gint          xid,
                                       UDisksObject *object,
                                       GCallback     finished_cb,
                                       gpointer      cb_data)
{
  gdu_format_volume_dialog_show_internal (client, NULL, xid, object, finished_cb, cb_data);
}

void
gdu_format_volume_dialog_show (GduWindow    *window,
                               UDisksObject *object)
{
  gdu_format_volume_dialog_show_internal (gdu_window_get_client (window), GTK_WINDOW (window),
                                          -1, object, NULL, NULL);
}
