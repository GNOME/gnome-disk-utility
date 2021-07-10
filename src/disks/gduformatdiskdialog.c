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

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduformatdiskdialog.h"
#include "gduvolumegrid.h"

/* ---------------------------------------------------------------------------------------------------- */

enum
{
  MODEL_COLUMN_ID,
  MODEL_COLUMN_MARKUP,
  MODEL_COLUMN_SEPARATOR,
  MODEL_COLUMN_SENSITIVE,
  MODEL_N_COLUMNS,
};

typedef struct
{
  GduWindow *window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;
  UDisksDriveAta *ata;

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
  g_clear_object (&data->ata);
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

static gchar *
get_erase_duration_string (gint minutes)
{
  gchar *s;
  if (minutes == 510)
    {
      gchar *s2 = gdu_utils_format_duration_usec ((minutes - 2) * 60LL * G_USEC_PER_SEC,
                                                  GDU_FORMAT_DURATION_FLAGS_NONE);
      /* Translators: Used to convey that something takes at least
       * some specificed duration but may take longer. The %s is a
       * time duration e.g. "8 hours and 28 minutes"
       */
      s = g_strdup_printf (_("At least %s"), s2);
      g_free (s2);
    }
  else
    {
      gchar *s2 = gdu_utils_format_duration_usec (minutes * 60LL * G_USEC_PER_SEC,
                                                  GDU_FORMAT_DURATION_FLAGS_NONE);
      /* Translators: Used to convey that something takes
       * approximately some specificed duration. The %s is a time
       * duration e.g. "2 hours and 2 minutes"
       */
      s = g_strdup_printf (_("Approximately %s"), s2);
      g_free (s2);
    }
  return s;
}

static void
populate_erase_combobox (FormatDiskData *data)
{
  GtkListStore *model;
  GtkCellRenderer *renderer;
  gchar *s, *s2;

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (GTK_COMBO_BOX (data->erase_combobox), GTK_TREE_MODEL (model));
  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (data->erase_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (data->erase_combobox), renderer,
                                  "markup", MODEL_COLUMN_MARKUP,
                                  "sensitive", MODEL_COLUMN_SENSITIVE,
                                  NULL);

  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (data->erase_combobox),
                                        separator_func,
                                        data,
                                        NULL); /* GDestroyNotify */

  /* Quick */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Don’t overwrite existing data"),
                       _("Quick"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "",
                                     MODEL_COLUMN_MARKUP, s,
                                     MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
  g_free (s);

  /* Full */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Overwrite existing data with zeroes"),
                       _("Slow"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "zero",
                                     MODEL_COLUMN_MARKUP, s,
                                     MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
  g_free (s);

  /* TODO: include 7-pass and 35-pass (DoD 5220-22 M) */

  if (data->ata != NULL)
    {
      gint erase_minutes, enhanced_erase_minutes;
      gboolean frozen;

      erase_minutes = udisks_drive_ata_get_security_erase_unit_minutes (data->ata);
      enhanced_erase_minutes = udisks_drive_ata_get_security_enhanced_erase_unit_minutes (data->ata);
      frozen = udisks_drive_ata_get_security_frozen (data->ata);

      if (erase_minutes > 0 || enhanced_erase_minutes > 0)
        {
          /* separator */
          gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                             MODEL_COLUMN_SEPARATOR, TRUE,
                                             MODEL_COLUMN_SENSITIVE, TRUE,
                                             -1);

          /* if both normal and enhanced erase methods are available, only show the enhanced one */
          if (erase_minutes > 0 && enhanced_erase_minutes == 0)
            {
              s2 = get_erase_duration_string (erase_minutes);
              s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                                   _("ATA Secure Erase"),
                                   s2);
              gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                                 MODEL_COLUMN_ID, "ata-secure-erase",
                                                 MODEL_COLUMN_MARKUP, s,
                                                 MODEL_COLUMN_SENSITIVE, !frozen,
                                                 -1);
              g_free (s);
              g_free (s2);
            }

          if (enhanced_erase_minutes > 0)
            {
              s2 = get_erase_duration_string (enhanced_erase_minutes);
              s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                                   _("ATA Enhanced Secure Erase"),
                                   s2);
              gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                                 MODEL_COLUMN_ID, "ata-secure-erase-enhanced",
                                                 MODEL_COLUMN_MARKUP, s,
                                                 MODEL_COLUMN_SENSITIVE, !frozen,
                                                 -1);
              g_free (s);
              g_free (s2);
            }
        }
    }

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
                              G_TYPE_BOOLEAN,
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
                                     MODEL_COLUMN_ID, "dos",
                                     MODEL_COLUMN_MARKUP, s,
                                     MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
  g_free (s);

  /* GPT */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Compatible with modern systems and hard disks > 2TB"),
                       _("GPT"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "gpt",
                                     MODEL_COLUMN_MARKUP, s,
                                     MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
  g_free (s);

  /* separator */
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_SEPARATOR, TRUE,
                                     MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);


  /* Empty */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("No partitioning"),
                       _("Empty"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "empty",
                                     MODEL_COLUMN_MARKUP, s,
                                     MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
  g_free (s);


  /* Default to MBR for removable drives < 2TB... GPT otherwise */
  if (data->drive != NULL &&
      udisks_drive_get_removable (data->drive) &&
      udisks_drive_get_size (data->drive) < (guint64)(2ULL * 1000ULL*1000ULL*1000ULL*1000ULL))
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
      gdu_utils_show_error (GTK_WINDOW (data->window), _("Error formatting disk"), error);
      g_error_free (error);
    }
  format_disk_data_free (data);
}


static void
ensure_unused_cb (GduWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  FormatDiskData *data = user_data;
  const gchar *partition_table_type;
  const gchar *erase_type;
  GVariantBuilder options_builder;


  if (!gdu_window_ensure_unused_finish (window, res, NULL))
    {
      format_disk_data_free (data);
      goto out;
    }

  partition_table_type = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->type_combobox));
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

 out:
  ;
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
  if (data->drive != NULL)
    {
      GDBusObject *drive_object;
      drive_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (data->drive));
      if (drive_object != NULL)
        {
          data->ata = udisks_object_get_drive_ata (UDISKS_OBJECT (drive_object));
        }
    }
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
      const gchar *erase_type;
      const gchar *primary_message;
      GString *str;
      GList *objects = NULL;

      erase_type = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->erase_combobox));

      primary_message = _("Are you sure you want to format the disk?");
      if (g_strcmp0 (erase_type, "") == 0)
        {
          /* Translators: warning used for quick format */
          str = g_string_new (_("All data on the disk will be lost but may still be recoverable by data recovery services"));
          g_string_append (str, "\n\n");
          g_string_append (str, _("<b>Tip</b>: If you are planning to recycle, sell or give away your old computer or disk, you should use a more thorough erase type to keep your private information from falling into the wrong hands"));
        }
      else
        {
          /* Translators: warning used when overwriting data */
          str = g_string_new (_("All data on the disk will be overwritten and will likely not be recoverable by data recovery services"));
        }

      if (data->ata != NULL &&
          (g_strcmp0 (erase_type, "ata-secure-erase") == 0 ||
           g_strcmp0 (erase_type, "ata-secure-erase-enhanced") == 0))
        {
          g_string_append (str, "\n\n");
          g_string_append (str, _("<b>WARNING</b>: The Secure Erase command may take a very long time to complete, can’t be canceled and may not work properly with some hardware. In the worst case, your drive may be rendered unusable or your system may crash or lock up. Before proceeding, please read the article about <a href='https://ata.wiki.kernel.org/index.php/ATA_Secure_Erase'>ATA Secure Erase</a> and make sure you understand the risks"));
        }

      objects = g_list_append (NULL, object);
      gtk_widget_hide (data->dialog);
      if (!gdu_utils_show_confirmation (GTK_WINDOW (window),
                                        primary_message,
                                        str->str,
                                        _("_Format"),
                                        NULL, NULL,
                                        gdu_window_get_client (data->window), objects, TRUE))
        {
          g_list_free (objects);
          g_string_free (str, TRUE);
          goto out;
        }
      g_list_free (objects);
      g_string_free (str, TRUE);

      /* ensure the volume is unused (e.g. unmounted) before formatting it... */
      gdu_window_ensure_unused (data->window,
                                data->object,
                                (GAsyncReadyCallback) ensure_unused_cb,
                                NULL, /* GCancellable */
                                data);
      return;
    }

 out:
  format_disk_data_free (data);
}
