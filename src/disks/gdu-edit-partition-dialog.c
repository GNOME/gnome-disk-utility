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

#include "gdu-application.h"
#include "gdu-edit-partition-dialog.h"

struct _GduEditPartitionDialog
{
  AdwDialog             parent_instance;

  GtkWidget            *confirm_button;

  GtkWidget            *type_row;
  GtkWidget            *name_entry;

  GtkWidget            *bootable_info_label;
  GtkWidget            *bootable_row;
  GtkWidget            *bootable_switch;
  GtkWidget            *system_partition_row;
  GtkWidget            *system_partition_switch;
  GtkWidget            *firmware_hide_row;
  GtkWidget            *firmware_hide_switch;

  GtkWindow            *window;
  UDisksObject         *udisks_object;
  UDisksClient         *client;
  UDisksPartition      *udisks_partition;
  UDisksPartitionTable *udisks_partition_table;
  char                 *partition_table_type;
  GListModel           *model;
};


G_DEFINE_TYPE (GduEditPartitionDialog, gdu_edit_partition_dialog, ADW_TYPE_DIALOG)

static char *
gdu_edit_partition_dialog_get_new_type (GduEditPartitionDialog  *self)
{
  GObject *item;
  char *type;

  item = adw_combo_row_get_selected_item (ADW_COMBO_ROW (self->type_row));
  type = g_strdup (g_object_get_data (G_OBJECT (item), "type"));

  return type;
}

static guint64
gdu_edit_partition_dialog_get_new_flags (GduEditPartitionDialog *self)
{
  guint64 flags = 0;

  if (g_strcmp0 (self->partition_table_type, "dos") == 0
      && gtk_switch_get_active (GTK_SWITCH (self->bootable_switch)))
    {
      return flags | (1UL << 7);
    }

  if (gtk_switch_get_active (GTK_SWITCH (self->system_partition_switch)))
    flags |= (1UL << 0);
  if (gtk_switch_get_active (GTK_SWITCH (self->firmware_hide_switch)))
    flags |= (1UL << 1);
  if (gtk_switch_get_active (GTK_SWITCH (self->bootable_switch)))
    flags |= (1UL << 2);

  return flags;
}

static char *
gdu_edit_partition_dialog_get_new_name (GduEditPartitionDialog *self)
{
  if (g_strcmp0 (self->partition_table_type, "dos") == 0)
    {
      return g_strdup ("");
    }

  return g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->name_entry)));
}

static void
on_confirm_button_clicked_cb (GduEditPartitionDialog *self,
                              GtkButton              *button)
{
  g_autofree char *type = NULL;
  g_autofree char *name = NULL;
  g_autoptr(GError) error = NULL;
  guint64 flags;

  name = gdu_edit_partition_dialog_get_new_name (self);
  flags = gdu_edit_partition_dialog_get_new_flags (self);
  type = gdu_edit_partition_dialog_get_new_type (self);

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
  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_property_changed_cb (GduEditPartitionDialog *self)
{
  g_autofree char *type = NULL;
  g_autofree char *name = NULL;
  gboolean differs = FALSE;
  guint64 flags;

  g_assert (GDU_IS_EDIT_PARTITION_DIALOG (self));

  name = gdu_edit_partition_dialog_get_new_name (self);
  flags = gdu_edit_partition_dialog_get_new_flags (self);
  type = gdu_edit_partition_dialog_get_new_type (self);

  differs = g_strcmp0 (udisks_partition_get_type_ (self->udisks_partition), type) != 0 ||
            g_strcmp0 (udisks_partition_get_name (self->udisks_partition), name) != 0 ||
            udisks_partition_get_flags (self->udisks_partition) != flags;

  gtk_widget_set_sensitive (self->confirm_button, differs);
}

static void
gdu_edit_partition_dialog_populate_types_row (GduEditPartitionDialog *self)
{
  guint n = 0;
  GList *l;
  g_autoptr(GList) infos;
  const gchar *cur_type;

  cur_type = udisks_partition_get_type_ (self->udisks_partition);
  infos = udisks_client_get_partition_type_infos (self->client,
                                                  self->partition_table_type,
                                                  NULL);
  for (l = infos; l != NULL; l = l->next)
    {
      UDisksPartitionTypeInfo *info = l->data;
      g_autoptr(GObject) obj = NULL;
      const gchar *s;

      /* skip type like 'Extended Partition' (dos 0x05) since we can't
       * just change the partition type to that
       */
      if (info->flags & UDISKS_PARTITION_TYPE_INFO_FLAGS_CREATE_ONLY)
        continue;

      s = udisks_client_get_partition_type_and_subtype_for_display (self->client,
                                                                    self->partition_table_type,
                                                                    info->table_subtype,
                                                                    info->type);
      gtk_string_list_append (GTK_STRING_LIST (self->model), s);
      obj = g_list_model_get_item (G_LIST_MODEL (self->model), n);
      g_object_set_data (G_OBJECT (obj), "type", (gpointer) info->type);
      if (g_strcmp0 (info->type, cur_type) == 0)
        adw_combo_row_set_selected (ADW_COMBO_ROW (self->type_row), n);
      n+=1;
    }

  g_list_foreach (infos, (GFunc) udisks_partition_type_info_free, NULL);
  g_signal_connect_swapped (G_OBJECT (self->type_row),
                            "notify::selected-item",
                            G_CALLBACK (on_property_changed_cb),
                            self);
}

static void
gdu_edit_partition_dialog_populate (GduEditPartitionDialog *self)
{
  guint64 flags;

  flags = udisks_partition_get_flags (self->udisks_partition);

  if (g_strcmp0 (self->partition_table_type, "dos") == 0)
    {
      gtk_switch_set_active (GTK_SWITCH (self->bootable_switch), (flags & (1UL<< 7)) != 0);
      return;
    }

  gtk_editable_set_text (GTK_EDITABLE (self->name_entry), udisks_partition_get_name (self->udisks_partition));
  gtk_switch_set_active (GTK_SWITCH (self->system_partition_switch), (flags & (1UL << 0)) != 0);
  gtk_switch_set_active (GTK_SWITCH (self->firmware_hide_switch), (flags & (1UL << 1)) != 0);
  gtk_switch_set_active (GTK_SWITCH (self->bootable_switch), (flags & (1UL << 2)) != 0);
}

static void
gdu_edit_partition_dialog_finalize (GObject *object)
{
  GduEditPartitionDialog *self = (GduEditPartitionDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_partition);
  g_clear_object (&self->udisks_partition_table);
  g_clear_pointer (&self->partition_table_type, g_free);

  G_OBJECT_CLASS (gdu_edit_partition_dialog_parent_class)->finalize (object);
}

static void
gdu_edit_partition_dialog_class_init (GduEditPartitionDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_edit_partition_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-edit-partition-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, confirm_button);

  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, type_row);
  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, name_entry);

  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, bootable_info_label);
  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, bootable_row);
  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, bootable_switch);
  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, system_partition_row);
  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, system_partition_switch);
  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, firmware_hide_row);
  gtk_widget_class_bind_template_child (widget_class, GduEditPartitionDialog, firmware_hide_switch);

  gtk_widget_class_bind_template_callback (widget_class, on_confirm_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_property_changed_cb);
}

static void
gdu_edit_partition_dialog_init (GduEditPartitionDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->model = G_LIST_MODEL (gtk_string_list_new (NULL));

  adw_combo_row_set_model (ADW_COMBO_ROW (self->type_row),
                           G_LIST_MODEL (self->model));
}

void
gdu_edit_partition_dialog_show (GtkWindow    *parent_window,
                                UDisksObject *object,
                                UDisksClient *client)
{
  GduEditPartitionDialog *self;

  self = g_object_new (GDU_TYPE_EDIT_PARTITION_DIALOG,
                       NULL);

  self->window = parent_window;
  self->client = client;
  self->udisks_object = g_object_ref (object);
  self->udisks_partition = udisks_object_get_partition (object);
  self->udisks_partition_table = udisks_client_get_partition_table (client,
                                                                    self->udisks_partition);
  self->partition_table_type = udisks_partition_table_dup_type_ (self->udisks_partition_table);

  if (g_strcmp0 (self->partition_table_type, "dos") == 0)
    {
      gtk_widget_set_visible (GTK_WIDGET (self->name_entry), FALSE);
      gtk_widget_set_visible (GTK_WIDGET (self->system_partition_row), FALSE);
      gtk_widget_set_visible (GTK_WIDGET (self->firmware_hide_row), FALSE);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->type_row),
                                   _("The partition type as a 8-bit unsigned integer"));
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->bootable_row), _("_Bootable"));
      gtk_label_set_label (GTK_LABEL (self->bootable_info_label),
                           _("A flag used by the Platform bootloader to determine where the OS "
                            "should be loaded from. Sometimes the partition with this flag set "
                            "is referred to as the “active” partition"));
    }

  gdu_edit_partition_dialog_populate (self);
  gdu_edit_partition_dialog_populate_types_row (self);

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
