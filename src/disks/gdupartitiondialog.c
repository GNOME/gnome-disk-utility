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

#include <inttypes.h>
#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdupartitiondialog.h"
#include "gduvolumegrid.h"

/* ---------------------------------------------------------------------------------------------------- */

enum
{
  MODEL_COLUMN_SELECTABLE,
  MODEL_COLUMN_NAME_MARKUP,
  MODEL_COLUMN_TYPE,
  MODEL_N_COLUMNS
};

struct _GduPartitionDialog
{
  GtkDialog             parent_instance;

  GtkComboBox          *type_combobox;
  GtkEntry             *name_entry;

  GtkCheckButton       *bootable_check_button;
  GtkCheckButton       *system_check_button;
  GtkCheckButton       *hide_from_firmware_check_button;

  GtkWindow            *window;
  UDisksObject         *udisks_object;
  UDisksClient         *client;
  UDisksPartition      *udisks_partition;
  UDisksPartitionTable *udisks_partition_table;
  char                 *partition_table_type;
};


G_DEFINE_TYPE (GduPartitionDialog, gdu_partition_dialog, GTK_TYPE_DIALOG)

static void
edit_partition_get (GduPartitionDialog  *self,
                    char               **out_type,
                    char               **out_name,
                    guint64             *out_flags)
{
  gchar *type = NULL;
  gchar *name = NULL;
  guint64 flags = 0;
  GtkTreeIter iter;

  if (gtk_combo_box_get_active_iter (self->type_combobox, &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (gtk_combo_box_get_model (self->type_combobox)),
                          &iter,
                          MODEL_COLUMN_TYPE, &type,
                          -1);
    }

  if (g_strcmp0 (self->partition_table_type, "gpt") == 0)
    {
      name = g_strdup (gtk_entry_get_text (self->name_entry));
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->system_check_button)))
        flags |= (1UL<<0);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->hide_from_firmware_check_button)))
        flags |= (1UL<<1);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->bootable_check_button)))
        flags |= (1UL<<2);
    }
  else if (g_strcmp0 (self->partition_table_type, "dos") == 0)
    {
      name = g_strdup ("");
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->bootable_check_button)))
        flags |= (1UL<<7);
    }
  else
    {
      name = g_strdup ("");
    }

  *out_type = type;
  *out_name = name;
  *out_flags = flags;
}

static void
partition_dialog_response_cb (GduPartitionDialog *self,
                              int                 response_id)
{
  g_autofree char *type = NULL;
  g_autofree char *name = NULL;
  g_autoptr(GError) error = NULL;
  guint64 flags;

  g_assert (GDU_IS_PARTITION_DIALOG (self));

  if (response_id == GTK_RESPONSE_CANCEL ||
      response_id == GTK_RESPONSE_CLOSE ||
      response_id == GTK_RESPONSE_DELETE_EVENT)
    goto end;

  edit_partition_get (self, &type, &name, &flags);

  if (g_strcmp0 (udisks_partition_get_type_ (self->udisks_partition), type) != 0)
    {
      if (!udisks_partition_call_set_type_sync (self->udisks_partition,
                                                type,
                                                g_variant_new ("a{sv}", NULL), /* options */
                                                NULL, /* GCancellable */
                                                &error))
        {
          gdu_utils_show_error (self->window, _("Error setting partition type"), error);
          goto end;
        }
    }
  if (g_strcmp0 (udisks_partition_get_name (self->udisks_partition), name) != 0)
    {
      if (!udisks_partition_call_set_name_sync (self->udisks_partition,
                                                name,
                                                g_variant_new ("a{sv}", NULL), /* options */
                                                NULL, /* GCancellable */
                                                &error))
        {
          gdu_utils_show_error (self->window, _("Error setting partition name"), error);
          goto end;
        }
    }
  if (udisks_partition_get_flags (self->udisks_partition) != flags)
    {
      if (!udisks_partition_call_set_flags_sync (self->udisks_partition,
                                                 flags,
                                                 g_variant_new ("a{sv}", NULL), /* options */
                                                 NULL, /* GCancellable */
                                                 &error))
        {
          gdu_utils_show_error (self->window, _("Error setting partition flags"), error);
          goto end;
        }
    }

 end:
  gtk_widget_hide (GTK_WIDGET (self));
  gtk_widget_destroy (GTK_WIDGET (self));
}

static void
partition_dialog_property_changed_cb (GduPartitionDialog *self)
{
  g_autofree char *type = NULL;
  g_autofree char *name = NULL;
  gboolean differs = FALSE;
  guint64 flags;

  g_assert (GDU_IS_PARTITION_DIALOG (self));

  edit_partition_get (self, &type, &name, &flags);

  if (g_strcmp0 (udisks_partition_get_type_ (self->udisks_partition), type) != 0)
    differs = TRUE;
  if (g_strcmp0 (udisks_partition_get_name (self->udisks_partition), name) != 0)
    differs = TRUE;
  if (udisks_partition_get_flags (self->udisks_partition) != flags)
    differs = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (self), GTK_RESPONSE_OK, differs);
}

static void
gdu_partition_dialog_finalize (GObject *object)
{
  GduPartitionDialog *self = (GduPartitionDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_partition);
  g_clear_object (&self->udisks_partition_table);
  g_clear_pointer (&self->partition_table_type, g_free);

  G_OBJECT_CLASS (gdu_partition_dialog_parent_class)->finalize (object);
}

static void
gdu_partition_dialog_class_init (GduPartitionDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_partition_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Disks/ui/"
                                               "edit-partition-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduPartitionDialog, type_combobox);
  gtk_widget_class_bind_template_child (widget_class, GduPartitionDialog, name_entry);

  gtk_widget_class_bind_template_child (widget_class, GduPartitionDialog, bootable_check_button);
  gtk_widget_class_bind_template_child (widget_class, GduPartitionDialog, system_check_button);
  gtk_widget_class_bind_template_child (widget_class, GduPartitionDialog, hide_from_firmware_check_button);

  gtk_widget_class_bind_template_callback (widget_class, partition_dialog_response_cb);
  gtk_widget_class_bind_template_callback (widget_class, partition_dialog_property_changed_cb);
}

static void
gdu_partition_dialog_init (GduPartitionDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


static void
edit_partition_update (GduPartitionDialog *self)
{
  g_autofree char *type = NULL;
  g_autofree char *name = NULL;
  gboolean differs = FALSE;
  guint64 flags;

  edit_partition_get (self, &type, &name, &flags);

  if (g_strcmp0 (udisks_partition_get_type_ (self->udisks_partition), type) != 0)
    differs = TRUE;
  if (g_strcmp0 (udisks_partition_get_name (self->udisks_partition), name) != 0)
    differs = TRUE;
  if (udisks_partition_get_flags (self->udisks_partition) != flags)
    differs = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (self), GTK_RESPONSE_OK, differs);
}

static void
edit_partition_populate (GduPartitionDialog *self)
{
  g_autoptr(GtkListStore) model = NULL;
  const gchar *cur_type;
  GList *l;
  guint n;
  GtkTreeIter *active_iter = NULL;
  GList *infos;
  const gchar *cur_table_subtype;
  UDisksClient *client;
  GtkCellRenderer *renderer;

  g_assert (GDU_IS_PARTITION_DIALOG (self));

  client = self->client;
  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_STRING);

  cur_type = udisks_partition_get_type_ (self->udisks_partition);
  infos = udisks_client_get_partition_type_infos (client,
                                                  self->partition_table_type,
                                                  NULL);
  /* assume that table subtypes are in order */
  cur_table_subtype = NULL;
  for (l = infos, n = 0; l != NULL; l = l->next, n++)
    {
      UDisksPartitionTypeInfo *info = l->data;
      const gchar *type_for_display;
      gchar *escaped_type_for_display;
      gchar *s;
      GtkTreeIter iter;

      /* skip type like 'Extended Partition' (dos 0x05) since we can't
       * just change the partition type to that
       */
      if (info->flags & UDISKS_PARTITION_TYPE_INFO_FLAGS_CREATE_ONLY)
        continue;

      if (g_strcmp0 (info->table_subtype, cur_table_subtype) != 0)
        {
          s = g_strdup_printf ("<i>%s</i>",
                               udisks_client_get_partition_table_subtype_for_display (client,
                                                                                      info->table_type,
                                                                                      info->table_subtype));
          gtk_list_store_insert_with_values (model,
                                             NULL, /* out iter */
                                             G_MAXINT, /* position */
                                             MODEL_COLUMN_SELECTABLE, FALSE,
                                             MODEL_COLUMN_NAME_MARKUP, s,
                                             MODEL_COLUMN_TYPE, NULL,
                                             -1);
          g_free (s);
          cur_table_subtype = info->table_subtype;
        }

      type_for_display = udisks_client_get_partition_type_and_subtype_for_display (client,
                                                                                   self->partition_table_type,
                                                                                   info->table_subtype,
                                                                                   info->type);
      escaped_type_for_display = g_markup_escape_text (type_for_display, -1);
      s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                           escaped_type_for_display,
                           info->type);
      g_free (escaped_type_for_display);

      gtk_list_store_insert_with_values (model,
                                         &iter,
                                         G_MAXINT, /* position */
                                         MODEL_COLUMN_SELECTABLE, TRUE,
                                         MODEL_COLUMN_NAME_MARKUP, s,
                                         MODEL_COLUMN_TYPE, info->type,
                                         -1);

      if (active_iter == NULL && g_strcmp0 (info->type, cur_type) == 0)
        active_iter = gtk_tree_iter_copy (&iter);

      g_free (s);
    }
  gtk_combo_box_set_model (self->type_combobox, GTK_TREE_MODEL (model));
  if (active_iter != NULL)
    {
      gtk_combo_box_set_active_iter (self->type_combobox, active_iter);
      gtk_tree_iter_free (active_iter);
    }

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->type_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (self->type_combobox), renderer,
                                  "sensitive", MODEL_COLUMN_SELECTABLE,
                                  "markup", MODEL_COLUMN_NAME_MARKUP,
                                  NULL);

  if (g_strcmp0 (self->partition_table_type, "gpt") == 0)
    {
      guint64 flags;

      gtk_entry_set_text (self->name_entry, udisks_partition_get_name (self->udisks_partition));
      flags = udisks_partition_get_flags (self->udisks_partition);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->system_check_button),           (flags & (1UL<< 0)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->hide_from_firmware_check_button), (flags & (1UL<< 1)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->bootable_check_button),         (flags & (1UL<< 2)) != 0);
    }
  else if (g_strcmp0 (self->partition_table_type, "dos") == 0)
    {
      guint64 flags;

      flags = udisks_partition_get_flags (self->udisks_partition);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->bootable_check_button),         (flags & (1UL<< 7)) != 0);
    }

  g_list_foreach (infos, (GFunc) udisks_partition_type_info_free, NULL);
  g_list_free (infos);
}

void
gdu_partition_dialog_show (GtkWindow    *parent_window,
                           UDisksObject *object,
                           UDisksClient *client)
{
  GduPartitionDialog *self;

  self = g_object_new (GDU_TYPE_PARTITION_DIALOG,
                       "transient-for", parent_window,
                       NULL);

  self->window = parent_window;
  self->client = client;
  self->udisks_object = g_object_ref (object);
  self->udisks_partition = udisks_object_get_partition (object);
  self->udisks_partition_table = udisks_client_get_partition_table (client,
                                                                    self->udisks_partition);
  self->partition_table_type = udisks_partition_table_dup_type_ (self->udisks_partition_table);

  if (g_strcmp0 (self->partition_table_type, "gpt") == 0)
    {
      gtk_widget_show (GTK_WIDGET (self->name_entry));
      gtk_widget_show (GTK_WIDGET (self->system_check_button));
      gtk_widget_show (GTK_WIDGET (self->hide_from_firmware_check_button));
      gtk_widget_set_tooltip_markup (GTK_WIDGET (self->type_combobox),
                                     _("The partition type represented as a 32-bit <i>GUID</i>"));
      gtk_button_set_label (GTK_BUTTON (self->bootable_check_button), _("Legacy BIOS _Bootable"));
      gtk_widget_set_tooltip_markup (GTK_WIDGET (self->bootable_check_button),
                                     _("This is equivalent to Master Boot Record <i>bootable</i> "
                                       "flag. It is normally only used for GPT partitions on MBR systems"));
    }
  else if (g_strcmp0 (self->partition_table_type, "dos") == 0)
    {
      gtk_widget_set_tooltip_markup (GTK_WIDGET (self->type_combobox),
                                     _("The partition type as a 8-bit unsigned integer"));
      gtk_button_set_label (GTK_BUTTON (self->bootable_check_button), _("_Bootable"));
      gtk_widget_set_tooltip_markup (GTK_WIDGET (self->bootable_check_button),
                                     _("A flag used by the Platform bootloader to determine where the OS "
                                       "should be loaded from. Sometimes the partition with this flag set "
                                       "is referred to as the <i>active</i> partition"));
    }

  edit_partition_populate (self);
  edit_partition_update (self);

  gtk_widget_grab_focus (GTK_WIDGET (self->type_combobox));
  gtk_window_present (GTK_WINDOW (self));
}
