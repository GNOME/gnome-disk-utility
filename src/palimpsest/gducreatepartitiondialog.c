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
#include "gducreatepartitiondialog.h"
#include "gducreatefilesystemwidget.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GduWindow *window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksPartitionTable *table;
  UDisksDrive *drive;
  guint64 offset;
  guint64 max_size;

  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *size_spinbutton;
  GtkAdjustment *size_adjustment;
  GtkAdjustment *free_following_adjustment;

  GtkWidget *contents_box;
  GtkWidget *create_filesystem_widget;

} CreatePartitionData;

static void
create_partition_data_free (CreatePartitionData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->object);
  g_object_unref (data->block);
  g_object_unref (data->table);
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
create_partition_update (CreatePartitionData *data)
{
  gboolean can_proceed = FALSE;

  if (gtk_adjustment_get_value (data->size_adjustment) > 0 &&
      gdu_create_filesystem_widget_get_has_info (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget)))
    can_proceed = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
create_partition_property_changed (GObject     *object,
                                   GParamSpec  *pspec,
                                   gpointer     user_data)
{
  CreatePartitionData *data = user_data;
  create_partition_update (data);
}

static void
create_partition_populate (CreatePartitionData *data)
{
}

static gboolean
size_binding_func (GBinding     *binding,
                   const GValue *source_value,
                   GValue       *target_value,
                   gpointer      user_data)
{
  CreatePartitionData *data = user_data;
  g_value_set_double (target_value, data->max_size / (1000*1000) - g_value_get_double (source_value));
  return TRUE;
}

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  CreatePartitionData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_block_call_format_finish (UDISKS_BLOCK (source_object),
                                        res,
                                        &error))
    {
      gdu_window_show_error (data->window, _("Error formatting partition"), error);
      g_error_free (error);
    }
  create_partition_data_free (data);
}

static void
create_partition_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  CreatePartitionData *data = user_data;
  GError *error;
  gchar *created_partition_object_path = NULL;
  UDisksObject *partition_object = NULL;
  UDisksBlock *partition_block;
  GVariantBuilder options_builder;
  const gchar *fstype;
  const gchar *name;
  const gchar *passphrase;

  error = NULL;
  if (!udisks_partition_table_call_create_partition_finish (UDISKS_PARTITION_TABLE (source_object),
                                                            &created_partition_object_path,
                                                            res,
                                                            &error))
    {
      gdu_window_show_error (data->window, _("Error creating partition"), error);
      g_error_free (error);
      create_partition_data_free (data);
      goto out;
    }

  udisks_client_settle (gdu_window_get_client (data->window));

  partition_object = udisks_client_get_object (gdu_window_get_client (data->window), created_partition_object_path);
  gdu_window_select_object (data->window, partition_object);

  /* OK, cool, now format the created partition */
  partition_block = udisks_object_peek_block (partition_object);
  if (partition_block == NULL)
    {
      g_warning ("Created partition has no block interface");
      create_partition_data_free (data);
      goto out;
    }

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

  udisks_block_call_format (partition_block,
                            fstype,
                            g_variant_builder_end (&options_builder),
                            NULL, /* GCancellable */
                            format_cb,
                            data);

 out:
  g_free (created_partition_object_path);
  g_clear_object (&partition_object);
}

void
gdu_create_partition_dialog_show (GduWindow    *window,
                                  UDisksObject *object,
                                  guint64       offset,
                                  guint64       max_size)
{
  CreatePartitionData *data;
  guint64 max_size_mb;
  gint response;

  data = g_new0 (CreatePartitionData, 1);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->table = udisks_object_get_partition_table (object);
  g_assert (data->table != NULL);
  data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), data->block);
  data->offset = offset;
  data->max_size = max_size;

  data->dialog = gdu_application_new_widget (gdu_window_get_application (window),
                                             "create-partition-dialog.ui",
                                             "create-partition-dialog",
                                             &data->builder);
  data->size_spinbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-spinbutton"));
  data->size_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (data->builder, "size-adjustment"));
  g_signal_connect (data->size_adjustment, "notify::value", G_CALLBACK (create_partition_property_changed), data);
  data->free_following_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (data->builder, "free-following-adjustment"));
  data->contents_box = GTK_WIDGET (gtk_builder_get_object (data->builder, "contents-box"));
  data->create_filesystem_widget = gdu_create_filesystem_widget_new (gdu_window_get_application (window), data->drive);
  gtk_box_pack_start (GTK_BOX (data->contents_box),
                      data->create_filesystem_widget,
                      TRUE, TRUE, 0);
  g_signal_connect (data->create_filesystem_widget, "notify::has-info",
                    G_CALLBACK (create_partition_property_changed), data);

  /* The adjustments count MB, not bytes */
  max_size_mb = max_size / (1000L*1000L);
  gtk_adjustment_configure (data->size_adjustment,
                            max_size_mb,
                            0.0,                    /* lower */
                            max_size_mb,            /* upper */
                            100,                    /* step increment */
                            1000,                   /* page increment */
                            0.0);                   /* page_size */
  gtk_adjustment_configure (data->free_following_adjustment,
                            0,
                            0.0,                    /* lower */
                            max_size_mb,            /* upper */
                            100,                    /* step increment */
                            1000,                   /* page increment */
                            0.0);                   /* page_size */

  g_object_bind_property_full (data->size_adjustment,
                               "value",
                               data->free_following_adjustment,
                               "value",
                               G_BINDING_BIDIRECTIONAL,
                               size_binding_func,
                               size_binding_func,
                               data,
                               NULL);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  create_partition_populate (data);
  create_partition_update (data);

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (data->size_spinbutton);

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      guint64 size;

      gtk_widget_hide (data->dialog);

      size = gtk_adjustment_get_value (data->size_adjustment) * 1000L * 1000L;
      udisks_partition_table_call_create_partition (data->table,
                                                    data->offset,
                                                    size,
                                                    "", /* use default type */
                                                    "", /* use blank partition name */
                                                    g_variant_new ("a{sv}", NULL), /* options */
                                                    NULL, /* GCancellable */
                                                    create_partition_cb,
                                                    data);
      return;
    }

  create_partition_data_free (data);
}
