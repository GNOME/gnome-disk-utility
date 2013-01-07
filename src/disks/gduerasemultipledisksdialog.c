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
#include "gduerasemultipledisksdialog.h"

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
  volatile gint ref_count;

  GduWindow *window;
  GList *blocks;

  GList *blocks_ensure_iter;
  GList *blocks_erase_iter;
  gchar *erase_type;

  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *erase_combobox;
} DialogData;


static DialogData *
dialog_data_ref (DialogData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
dialog_data_unref (DialogData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_object_unref (data->window);
      g_list_free_full (data->blocks, g_object_unref);
      g_free (data->erase_type);
      if (data->dialog != NULL)
        {
          gtk_widget_hide (data->dialog);
          gtk_widget_destroy (data->dialog);
        }
      if (data->builder != NULL)
        g_object_unref (data->builder);
      g_free (data);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update (DialogData *data)
{
}

static void
on_property_changed (GObject     *object,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  DialogData *data = user_data;
  update (data);
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
populate (DialogData *data)
{
  GtkListStore *model;
  GtkCellRenderer *renderer;
  gchar *s;

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

  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("ATA Secure Erase / Enhanced Secure Erase"),
                       _("If Available, Slow"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "secure-erase",
                                     MODEL_COLUMN_MARKUP, s,
                                     MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
  g_free (s);


  gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->erase_combobox), "");
}

/* ---------------------------------------------------------------------------------------------------- */

static void ensure_next (DialogData *data);
static void erase_next (DialogData *data);

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  DialogData *data = user_data;
  UDisksBlock *block = UDISKS_BLOCK (source_object);
  GError *error;

  error = NULL;
  if (!udisks_block_call_format_finish (block,
                                        res,
                                        &error))
    {
      gchar *s = g_strdup_printf (_("Error erasing device %s"), udisks_block_get_preferred_device (block));
      gdu_utils_show_error (GTK_WINDOW (data->window), s, error);
      g_free (s);
      g_clear_error (&error);
      /* Bail on first error */
      dialog_data_unref (data);
      return;
    }

  if (data->blocks_erase_iter != NULL)
    erase_next (data);
  else
    dialog_data_unref (data);
}

static void
erase_next (DialogData  *data)
{
  UDisksBlock *block = NULL;
  GVariantBuilder options_builder;
  const gchar *erase_type;

  g_assert (data->blocks_erase_iter != NULL);

  block = UDISKS_BLOCK (data->blocks_erase_iter->data);
  data->blocks_erase_iter = data->blocks_erase_iter->next;

  erase_type = data->erase_type;

  /* Fall back to 'zero' if secure erase is not available */
  if (g_strcmp0 (erase_type, "secure-erase") == 0)
    {
      UDisksDrive *drive = NULL;
      UDisksDriveAta *ata = NULL;

      /* assume not available, then adjust below if available */
      erase_type = "zero";

      drive = udisks_client_get_drive_for_block (gdu_window_get_client (data->window), block);
      if (drive != NULL)
        {
          GDBusObject *drive_object;
          drive_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (drive));
          if (drive_object != NULL)
            ata = udisks_object_get_drive_ata (UDISKS_OBJECT (drive_object));
        }

      if (ata != NULL)
        {
          if (!udisks_drive_ata_get_security_frozen (ata))
            {
              if (udisks_drive_ata_get_security_enhanced_erase_unit_minutes (ata) > 0)
                erase_type = "ata-secure-erase-enhanced";
              else if (udisks_drive_ata_get_security_erase_unit_minutes (ata) > 0)
                erase_type = "ata-secure-erase";
            }
        }
      g_clear_object (&ata);
      g_clear_object (&drive);
    }

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options_builder, "{sv}", "no-block", g_variant_new_boolean (TRUE));
  if (strlen (erase_type) > 0)
    g_variant_builder_add (&options_builder, "{sv}", "erase", g_variant_new_string (erase_type));


  udisks_block_call_format (block,
                            "empty",
                            g_variant_builder_end (&options_builder),
                            NULL, /* GCancellable */
                            format_cb,
                            data);
}

static void
ensure_unused_cb (GduWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  DialogData *data = user_data;

  if (!gdu_window_ensure_unused_finish (data->window, res, NULL))
    {
      /* fail */
      dialog_data_unref (data);
    }
  else
    {
      if (data->blocks_ensure_iter != NULL)
        {
          ensure_next (data);
        }
      else
        {
          /* done ensuring, now erase */
          data->blocks_erase_iter = data->blocks;
          erase_next (data);
        }
    }
}

static void
ensure_next (DialogData *data)
{
  UDisksBlock *block;
  UDisksObject *object;

  g_assert (data->blocks_ensure_iter != NULL);

  block = UDISKS_BLOCK (data->blocks_ensure_iter->data);
  data->blocks_ensure_iter = data->blocks_ensure_iter->next;

  object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  gdu_window_ensure_unused (data->window,
                            object,
                            (GAsyncReadyCallback) ensure_unused_cb,
                            NULL, /* GCancellable */
                            data);
}

static void
erase_devices (DialogData  *data,
               const gchar *erase_type)
{
  dialog_data_ref (data);

  data->erase_type = g_strdup (erase_type);

  /* First ensure all are unused... then if all that works, erase them */
  data->blocks_ensure_iter = data->blocks;
  ensure_next (data);
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
gdu_erase_multiple_disks_dialog_show (GduWindow *window,
                                      GList     *blocks)
{
  DialogData *data;
  gboolean ret = FALSE;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->window = g_object_ref (window);
  data->blocks = g_list_copy_deep (blocks, (GCopyFunc) g_object_ref, NULL);
  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "erase-multiple-disks-dialog.ui",
                                                         "erase-multiple-disks-dialog",
                                                         &data->builder));
  data->erase_combobox = GTK_WIDGET (gtk_builder_get_object (data->builder, "erase-combobox"));
  g_signal_connect (data->erase_combobox, "notify::active", G_CALLBACK (on_property_changed), data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  populate (data);
  update (data);

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (data->erase_combobox);

  while (TRUE)
    {
      gint response;

      response = gtk_dialog_run (GTK_DIALOG (data->dialog));
      if (response != GTK_RESPONSE_OK)
        {
          goto out;
        }
      else /* response == GTK_RESPONSE_OK */
        {
          const gchar *primary_message;
          GString *str;
          const gchar *erase_type;
          GList *objects = NULL, *l;

          erase_type = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->erase_combobox));

          primary_message = _("Are you sure you want to erase the disks?");
          if (g_strcmp0 (erase_type, "") == 0)
            {
              /* Translators: warning used for erasure of multiple disks */
              str = g_string_new (_("All data on the selected disks will be lost but may still be recoverable by data recovery services"));
              g_string_append (str, "\n\n");
              g_string_append (str, _("<b>Tip</b>: If you are planning to recycle, sell or give away your old computer or disk, you should use a more thorough erase type to keep your private information from falling into the wrong hands"));
            }
          else
            {
              /* Translators: warning used when overwriting data on multiple disks */
              str = g_string_new (_("All data on the selected disks will be overwritten and will likely not be recoverable by data recovery services"));
            }

          if (g_strcmp0 (erase_type, "secure-erase") == 0)
            {
              g_string_append (str, "\n\n");
              g_string_append (str, _("<b>WARNING</b>: The Secure Erase command may take a very long time to complete, can’t be canceled and may not work properly with some hardware. In the worst case, your drive may be rendered unusable or your system may crash or lock up. Before proceeding, please read the article about <a href='https://ata.wiki.kernel.org/index.php/ATA_Secure_Erase'>ATA Secure Erase</a> and make sure you understand the risks"));
            }

          for (l = data->blocks; l != NULL; l = l->next)
            {
              UDisksObject *object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (l->data));
              if (object != NULL)
                objects = g_list_append (objects, object);
            }
          if (!gdu_utils_show_confirmation (GTK_WINDOW (data->dialog),
                                            primary_message,
                                            str->str,
                                            _("_Erase"),
                                            NULL, NULL,
                                            gdu_window_get_client (data->window), objects))
            {
              g_string_free (str, TRUE);
              g_list_free (objects);
              continue;
            }
          g_string_free (str, TRUE);
          g_list_free (objects);
          gtk_widget_hide (data->dialog);
          erase_devices (data, erase_type);
          ret = TRUE;
          goto out;
        }
    }

 out:
  dialog_data_unref (data);
  return ret;
}
