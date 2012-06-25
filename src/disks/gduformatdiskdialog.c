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
#include "gduformatdiskdialog.h"
#include "gduvolumegrid.h"
#include "gduutils.h"
#include "gducreatefilesystemwidget.h"

/* ---------------------------------------------------------------------------------------------------- */

enum
{
  MODEL_COLUMN_ID,
  MODEL_COLUMN_MARKUP,
  MODEL_COLUMN_SEPARATOR,
  MODEL_N_COLUMNS,
};

typedef struct
{
  GduWindow *window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;

  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *type_combobox;
  GtkWidget *erase_combobox;
} FormatDiskData;

static void
format_disk_data_free (FormatDiskData *data)
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

/* ---------------------------------------------------------------------------------------------------- */

static void
format_disk_update (FormatDiskData *data)
{
}

static void
on_property_changed (GObject     *object,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  FormatDiskData *data = user_data;
  format_disk_update (data);
}

static gboolean
separator_func (GtkTreeModel *model,
                GtkTreeIter *iter,
                gpointer data)
{
  gboolean is_separator;
  gtk_tree_model_get (model, iter,
                      MODEL_COLUMN_SEPARATOR, &is_separator,
                      -1);
  return is_separator;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
populate_erase_combobox (FormatDiskData *data)
{
  GtkListStore *model;
  GtkCellRenderer *renderer;
  gchar *s;

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (GTK_COMBO_BOX (data->erase_combobox), GTK_TREE_MODEL (model));
  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (data->erase_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (data->erase_combobox), renderer,
                                  "markup", MODEL_COLUMN_MARKUP,
                                  NULL);

  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (data->erase_combobox),
                                        separator_func,
                                        data,
                                        NULL); /* GDestroyNotify */

  /* Quick */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Don't overwrite existing data"),
                       _("Quick"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);

  /* Full */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Overwrite existing data with zeroes"),
                       _("Slow"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "zero", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);

  /* TODO: include 7-pass and 35-pass (DoD 5220-22 M) */

  /* TODO: include ATA SECURE ERASE */

  gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->erase_combobox), "");
}

static void
populate_partitioning_combobox (FormatDiskData *data)
{
  GtkListStore *model;
  GtkCellRenderer *renderer;
  gchar *s;

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (GTK_COMBO_BOX (data->type_combobox), GTK_TREE_MODEL (model));
  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (data->type_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (data->type_combobox), renderer,
                                  "markup", MODEL_COLUMN_MARKUP,
                                  NULL);

  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (data->type_combobox),
                                        separator_func,
                                        data,
                                        NULL); /* GDestroyNotify */

  /* MBR */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Compatible with all systems and devices"),
                       _("MBR / DOS"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "dos", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);

  /* GPT */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Compatible with modern systems and hard disks > 2TB"),
                       _("GPT"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "gpt", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);

  /* separator */
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_SEPARATOR, TRUE, -1);


  /* Empty */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("No partitioning"),
                       _("Empty"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "empty", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);


  /* Default to MBR for removable drives < 2TB... GPT otherwise */
  if (data->drive != NULL &&
      udisks_drive_get_removable (data->drive) &&
      udisks_drive_get_size (data->drive) < 2 * 1000L*1000L*1000L*1000L)
    {
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->type_combobox), "dos");
    }
  else
    {
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->type_combobox), "gpt");
    }
}

static void
format_disk_populate (FormatDiskData *data)
{
  populate_erase_combobox (data);
  populate_partitioning_combobox (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  FormatDiskData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_block_call_format_finish (UDISKS_BLOCK (source_object),
                                        res,
                                        &error))
    {
      gdu_window_show_error (data->window, _("Error formatting disk"), error);
      g_error_free (error);
    }
  format_disk_data_free (data);
}

void
gdu_format_disk_dialog_show (GduWindow    *window,
                               UDisksObject *object)
{
  FormatDiskData *data;
  gint response;

  data = g_new0 (FormatDiskData, 1);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), data->block);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "format-disk-dialog.ui",
                                                         "format-disk-dialog",
                                                         &data->builder));
  data->type_combobox = GTK_WIDGET (gtk_builder_get_object (data->builder, "type-combobox"));
  data->erase_combobox = GTK_WIDGET (gtk_builder_get_object (data->builder, "erase-combobox"));
  g_signal_connect (data->type_combobox, "notify::active", G_CALLBACK (on_property_changed), data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  format_disk_populate (data);
  format_disk_update (data);

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (data->type_combobox);

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      const gchar *partition_table_type;
      const gchar *erase_type;
      GVariantBuilder options_builder;

      partition_table_type = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->type_combobox));

      gtk_widget_hide (data->dialog);
      if (!gdu_window_show_confirmation (window,
                                         _("Are you sure you want to format the disk?"),
                                         _("All data on the volume will be lost"),
                                         _("_Format")))
        goto out;

      erase_type = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->erase_combobox));

      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      if (strlen (erase_type) > 0)
        g_variant_builder_add (&options_builder, "{sv}", "erase", g_variant_new_string (erase_type));
      udisks_block_call_format (data->block,
                                partition_table_type,
                                g_variant_builder_end (&options_builder),
                                NULL, /* GCancellable */
                                format_cb,
                                data);
      return;
    }

 out:
  format_disk_data_free (data);
}
