/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2023 Purism SPC
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduformatdiskdialog.h"
#include "gduvolumegrid.h"

/* ---------------------------------------------------------------------------------------------------- */

struct _GduFormatDiskDialog
{
  GtkDialog          parent_instance;

  GtkComboBox       *type_combobox;
  GtkComboBox       *erase_combobox;

  GduWindow         *window;
  UDisksObject      *udisks_object;
  UDisksBlock       *udisks_block;
  UDisksDrive       *udisks_drive;
  UDisksDriveAta    *udisks_drive_ata;
};


G_DEFINE_TYPE (GduFormatDiskDialog, gdu_format_disk_dialog, GTK_TYPE_DIALOG)

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  GduFormatDiskDialog *self = user_data;
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_format_finish (self->udisks_block, res, &error))
    gdu_utils_show_error (GTK_WINDOW (self->window), _("Error formatting disk"), error);

  gtk_widget_hide (GTK_WIDGET (self));
  gtk_widget_destroy (GTK_WIDGET (self));
}

static void
ensure_unused_cb (GduWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  GduFormatDiskDialog *self = user_data;
  const char *partition_table_type, *erase_type;
  GVariantBuilder options_builder;

  if (!gdu_window_ensure_unused_finish (window, res, NULL))
    {
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_widget_destroy (GTK_WIDGET (self));

      return;
    }

  partition_table_type = gtk_combo_box_get_active_id (self->type_combobox);
  erase_type = gtk_combo_box_get_active_id (self->erase_combobox);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (erase_type && *erase_type)
    g_variant_builder_add (&options_builder, "{sv}", "erase", g_variant_new_string (erase_type));
  udisks_block_call_format (self->udisks_block,
                            partition_table_type,
                            g_variant_builder_end (&options_builder),
                            NULL, /* GCancellable */
                            format_cb,
                            self);

}

static void
format_disk_dialog_response_cb (GduFormatDiskDialog *self,
                                int                  response_id)
{
  const char *erase_type, *primary_message;
  g_autoptr(GList) objects = NULL;
  g_autoptr(GString) str = NULL;

  g_assert (GDU_IS_FORMAT_DISK_DIALOG (self));

  if (response_id == GTK_RESPONSE_CANCEL ||
      response_id == GTK_RESPONSE_CLOSE ||
      response_id == GTK_RESPONSE_DELETE_EVENT)
    {
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_widget_destroy (GTK_WIDGET (self));
      return;
    }

  erase_type = gtk_combo_box_get_active_id (self->erase_combobox);
  primary_message = _("Are you sure you want to format the disk?");

  if (!erase_type || !*erase_type)
    {
      /* Translators: warning used for quick format */
      str = g_string_new (_("All data on the disk will be lost but may still be recoverable by "
                            "data recovery services"));
      g_string_append (str, "\n\n");
      g_string_append (str, _("<b>Tip</b>: If you are planning to recycle, sell or give away your "
                              "old computer or disk, you should use a more thorough erase type to "
                              "keep your private information from falling into the wrong hands"));
    }
  else
    {
      /* Translators: warning used when overwriting data */
      str = g_string_new (_("All data on the disk will be overwritten and will likely not be "
                            "recoverable by data recovery services"));
    }

  if (self->udisks_drive_ata &&
      (g_strcmp0 (erase_type, "ata-secure-erase") == 0 ||
       g_strcmp0 (erase_type, "ata-secure-erase-enhanced") == 0))
    {
      g_string_append (str, "\n\n");
      g_string_append (str, _("<b>WARNING</b>: The Secure Erase command may take a very long time "
                              "to complete, can’t be canceled and may not work properly with some "
                              "hardware. In the worst case, your drive may be rendered unusable or "
                               "your system may crash or lock up. Before proceeding, please read the "
                               "article about <a href='https://ata.wiki.kernel.org/index.php/ATA_Secure_Erase'>ATA Secure Erase</a> "
                               "and make sure you understand the risks"));
    }

  objects = g_list_append (NULL, self->udisks_object);
  gtk_widget_hide (GTK_WIDGET (self));
  if (!gdu_utils_show_confirmation (GTK_WINDOW (self->window),
                                    primary_message,
                                    str->str,
                                    _("_Format"),
                                    NULL, NULL,
                                    gdu_window_get_client (self->window), objects, TRUE))
    {
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_widget_destroy (GTK_WIDGET (self));

      return;
    }

  /* ensure the volume is unused (e.g. unmounted) before formatting it... */
  gdu_window_ensure_unused (self->window,
                            self->udisks_object,
                            (GAsyncReadyCallback) ensure_unused_cb,
                            NULL, /* GCancellable */
                            self);
}

static void
gdu_format_disk_dialog_finalize (GObject *object)
{
  GduFormatDiskDialog *self = (GduFormatDiskDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_block);
  g_clear_object (&self->udisks_drive);
  g_clear_object (&self->udisks_drive_ata);

  G_OBJECT_CLASS (gdu_format_disk_dialog_parent_class)->finalize (object);
}

static void
gdu_format_disk_dialog_class_init (GduFormatDiskDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_format_disk_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Disks/ui/"
                                               "format-disk-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduFormatDiskDialog, type_combobox);
  gtk_widget_class_bind_template_child (widget_class, GduFormatDiskDialog, erase_combobox);

  gtk_widget_class_bind_template_callback (widget_class, format_disk_dialog_response_cb);
}

static void
gdu_format_disk_dialog_init (GduFormatDiskDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

enum
{
  MODEL_COLUMN_ID,
  MODEL_COLUMN_MARKUP,
  MODEL_COLUMN_SEPARATOR,
  MODEL_COLUMN_SENSITIVE,
  MODEL_N_COLUMNS,
};

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
  char *s;

  if (minutes == 510)
    {
      g_autofree char *s2 = gdu_utils_format_duration_usec ((minutes - 2) * 60LL * G_USEC_PER_SEC,
                                                            GDU_FORMAT_DURATION_FLAGS_NONE);
      /* Translators: Used to convey that something takes at least
       * some specificed duration but may take longer. The %s is a
       * time duration e.g. "8 hours and 28 minutes"
       */
      s = g_strdup_printf (_("At least %s"), s2);
    }
  else
    {
      g_autofree char *s2 = gdu_utils_format_duration_usec (minutes * 60LL * G_USEC_PER_SEC,
                                                  GDU_FORMAT_DURATION_FLAGS_NONE);
      /* Translators: Used to convey that something takes
       * approximately some specificed duration. The %s is a time
       * duration e.g. "2 hours and 2 minutes"
       */
      s = g_strdup_printf (_("Approximately %s"), s2);
    }

  return s;
}

static void
populate_erase_combobox (GduFormatDiskDialog *self)
{
  g_autoptr(GtkListStore) model = NULL;
  GtkCellRenderer *renderer;
  gchar *s, *s2;

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (self->erase_combobox, GTK_TREE_MODEL (model));

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->erase_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (self->erase_combobox), renderer,
                                  "markup", MODEL_COLUMN_MARKUP,
                                  "sensitive", MODEL_COLUMN_SENSITIVE,
                                  NULL);

  gtk_combo_box_set_row_separator_func (self->erase_combobox,
                                        separator_func,
                                        self,
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

  if (self->udisks_drive_ata != NULL)
    {
      gint erase_minutes, enhanced_erase_minutes;
      gboolean frozen;

      erase_minutes = udisks_drive_ata_get_security_erase_unit_minutes (self->udisks_drive_ata);
      enhanced_erase_minutes = udisks_drive_ata_get_security_enhanced_erase_unit_minutes (self->udisks_drive_ata);
      frozen = udisks_drive_ata_get_security_frozen (self->udisks_drive_ata);

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

  gtk_combo_box_set_active_id (self->erase_combobox, "");
}

static void
populate_partitioning_combobox (GduFormatDiskDialog *self)
{
  g_autoptr(GtkListStore) model = NULL;
  GtkCellRenderer *renderer;
  gchar *s;

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (self->type_combobox, GTK_TREE_MODEL (model));

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->type_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (self->type_combobox), renderer,
                                  "markup", MODEL_COLUMN_MARKUP,
                                  NULL);

  gtk_combo_box_set_row_separator_func (self->type_combobox,
                                        separator_func,
                                        self,
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
  if (self->udisks_drive != NULL &&
      udisks_drive_get_removable (self->udisks_drive) &&
      udisks_drive_get_size (self->udisks_drive) < (guint64)(2ULL * 1000ULL*1000ULL*1000ULL*1000ULL))
    {
      gtk_combo_box_set_active_id (self->type_combobox, "dos");
    }
  else
    {
      gtk_combo_box_set_active_id (self->type_combobox, "gpt");
    }
}

static void
format_disk_populate (GduFormatDiskDialog *self)
{
  populate_erase_combobox (self);
  populate_partitioning_combobox (self);
}

void
gdu_format_disk_dialog_show (GduWindow    *window,
                             UDisksObject *object)
{
  GduFormatDiskDialog *self;

  self = g_object_new (GDU_TYPE_FORMAT_DISK_DIALOG,
                       "transient-for", window,
                       NULL);

  self->window = window;
  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);
  self->udisks_drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), self->udisks_block);

  if (self->udisks_drive)
    {
      GDBusObject *drive_object;

      drive_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (self->udisks_drive));
      if (drive_object)
        self->udisks_drive_ata = udisks_object_get_drive_ata (UDISKS_OBJECT (drive_object));
    }

  format_disk_populate (self);
  gtk_window_present (GTK_WINDOW (self));
}
