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

typedef struct
{
  GduWindow *window;
  UDisksObject *object;
  UDisksPartition *partition;
  UDisksPartitionTable *partition_table;
  gchar *partition_table_type;

  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *type_combobox;
  GtkWidget *name_entry;
  GtkWidget *system_checkbutton;
  GtkWidget *bootable_checkbutton;
  GtkWidget *readonly_checkbutton;
  GtkWidget *hidden_checkbutton;
  GtkWidget *do_not_automount_checkbutton;

} EditPartitionData;

static void
edit_partition_data_free (EditPartitionData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->object);
  g_object_unref (data->partition);
  g_object_unref (data->partition_table);
  g_free (data->partition_table_type);
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
edit_partition_get (EditPartitionData   *data,
                    gchar              **out_type,
                    gchar              **out_name,
                    guint64             *out_flags)
{
  gchar *type = NULL;
  gchar *name = NULL;
  guint64 flags = 0;
  GtkTreeIter iter;

  if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (data->type_combobox), &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (gtk_combo_box_get_model (GTK_COMBO_BOX (data->type_combobox))),
                          &iter,
                          MODEL_COLUMN_TYPE, &type,
                          -1);
    }

  if (g_strcmp0 (data->partition_table_type, "gpt") == 0)
    {
      name = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->name_entry)));
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->system_checkbutton)))
        flags |= (1UL<<0);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->bootable_checkbutton)))
        flags |= (1UL<<2);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->readonly_checkbutton)))
        flags |= (1ULL<<60);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->hidden_checkbutton)))
        flags |= (1ULL<<62);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->do_not_automount_checkbutton)))
        flags |= (1ULL<<63);
    }
  else if (g_strcmp0 (data->partition_table_type, "dos") == 0)
    {
      name = g_strdup ("");
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->bootable_checkbutton)))
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
edit_partition_update (EditPartitionData *data)
{
  gboolean differs = FALSE;
  gchar *type;
  gchar *name;
  guint64 flags;

  edit_partition_get (data, &type, &name, &flags);

  if (g_strcmp0 (udisks_partition_get_type_ (data->partition), type) != 0)
    differs = TRUE;
  if (g_strcmp0 (udisks_partition_get_name (data->partition), name) != 0)
    differs = TRUE;
  if (udisks_partition_get_flags (data->partition) != flags)
    differs = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, differs);

  g_free (type);
  g_free (name);
}

static void
edit_partition_property_changed (GObject     *object,
                                 GParamSpec  *pspec,
                                 gpointer     user_data)
{
  EditPartitionData *data = user_data;
  edit_partition_update (data);
}

static void
edit_partition_populate (EditPartitionData *data)
{
  const gchar *cur_type;
  GList *l;
  guint n;
  GtkTreeIter *active_iter = NULL;
  GtkListStore *model;
  GList *infos;
  const gchar *cur_table_subtype;
  UDisksClient *client;
  GtkCellRenderer *renderer;

  client = gdu_window_get_client (data->window);

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_STRING);

  cur_type = udisks_partition_get_type_ (data->partition);
  infos = udisks_client_get_partition_type_infos (client,
                                                  data->partition_table_type,
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

#if UDISKS_CHECK_VERSION(2, 1, 1)
      type_for_display = udisks_client_get_partition_type_and_subtype_for_display (client,
                                                                                   data->partition_table_type,
                                                                                   info->table_subtype,
                                                                                   info->type);
#else
      type_for_display = udisks_client_get_partition_type_for_display (client,
                                                                       data->partition_table_type,
                                                                       info->type);
#endif
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
  gtk_combo_box_set_model (GTK_COMBO_BOX (data->type_combobox), GTK_TREE_MODEL (model));
  if (active_iter != NULL)
    {
      gtk_combo_box_set_active_iter (GTK_COMBO_BOX (data->type_combobox), active_iter);
      gtk_tree_iter_free (active_iter);
    }

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (data->type_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (data->type_combobox), renderer,
                                  "sensitive", MODEL_COLUMN_SELECTABLE,
                                  "markup", MODEL_COLUMN_NAME_MARKUP,
                                  NULL);

  if (g_strcmp0 (data->partition_table_type, "gpt") == 0)
    {
      guint64 flags;
      gtk_entry_set_text (GTK_ENTRY (data->name_entry), udisks_partition_get_name (data->partition));
      flags = udisks_partition_get_flags (data->partition);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->system_checkbutton),           (flags & (1UL<< 0)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->bootable_checkbutton),         (flags & (1UL<< 2)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->readonly_checkbutton),         (flags & (1ULL<<60)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->hidden_checkbutton),           (flags & (1ULL<<62)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->do_not_automount_checkbutton), (flags & (1ULL<<63)) != 0);
    }
  else if (g_strcmp0 (data->partition_table_type, "dos") == 0)
    {
      guint64 flags;
      flags = udisks_partition_get_flags (data->partition);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->bootable_checkbutton),         (flags & (1UL<< 7)) != 0);
    }

  g_list_foreach (infos, (GFunc) udisks_partition_type_info_free, NULL);
  g_list_free (infos);
  g_object_unref (model);
}

void
gdu_partition_dialog_show (GduWindow    *window,
                           UDisksObject *object)
{
  EditPartitionData *data;
  gint response;

  data = g_new0 (EditPartitionData, 1);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->partition = udisks_object_get_partition (object);
  g_assert (data->partition != NULL);
  data->partition_table = udisks_client_get_partition_table (gdu_window_get_client (window), data->partition);
  g_assert (data->partition_table != NULL);
  data->partition_table_type = udisks_partition_table_dup_type_ (data->partition_table);

  if (g_strcmp0 (data->partition_table_type, "gpt") == 0)
    {
      data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                             "edit-gpt-partition-dialog.ui",
                                                             "edit-gpt-partition-dialog",
                                                             &data->builder));
      data->name_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "name-entry"));
      data->system_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "system-checkbutton"));
      data->bootable_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "bootable-checkbutton"));
      data->readonly_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "readonly-checkbutton"));
      data->hidden_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "hidden-checkbutton"));
      data->do_not_automount_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "do-not-automount-checkbutton"));
      g_signal_connect (data->name_entry,
                        "notify::text", G_CALLBACK (edit_partition_property_changed), data);
      g_signal_connect (data->system_checkbutton,
                        "notify::active", G_CALLBACK (edit_partition_property_changed), data);
      g_signal_connect (data->bootable_checkbutton,
                        "notify::active", G_CALLBACK (edit_partition_property_changed), data);
      g_signal_connect (data->readonly_checkbutton,
                        "notify::active", G_CALLBACK (edit_partition_property_changed), data);
      g_signal_connect (data->hidden_checkbutton,
                        "notify::active", G_CALLBACK (edit_partition_property_changed), data);
      g_signal_connect (data->do_not_automount_checkbutton,
                        "notify::active", G_CALLBACK (edit_partition_property_changed), data);
    }
  else if (g_strcmp0 (data->partition_table_type, "dos") == 0)
    {
      data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                             "edit-dos-partition-dialog.ui",
                                                             "edit-dos-partition-dialog",
                                                             &data->builder));
      data->bootable_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "bootable-checkbutton"));
      g_signal_connect (data->bootable_checkbutton,
                        "notify::active", G_CALLBACK (edit_partition_property_changed), data);
    }
  else
    {
      data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                             "edit-partition-dialog.ui",
                                                             "edit-partition-dialog",
                                                             &data->builder));
    }
  data->type_combobox = GTK_WIDGET (gtk_builder_get_object (data->builder, "type-combobox"));
  g_signal_connect (data->type_combobox,
                    "notify::active", G_CALLBACK (edit_partition_property_changed), data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  edit_partition_populate (data);
  edit_partition_update (data);

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (data->type_combobox);

  /* TODO: do this async */
  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      gchar *type;
      gchar *name;
      guint64 flags;
      GError *error;

      edit_partition_get (data, &type, &name, &flags);

      if (g_strcmp0 (udisks_partition_get_type_ (data->partition), type) != 0)
        {
          error = NULL;
          if (!udisks_partition_call_set_type_sync (data->partition,
                                                    type,
                                                    g_variant_new ("a{sv}", NULL), /* options */
                                                    NULL, /* GCancellable */
                                                    &error))
            {
              gdu_utils_show_error (GTK_WINDOW (window), _("Error setting partition type"), error);
              g_error_free (error);
              goto set_out;
            }
        }
      if (g_strcmp0 (udisks_partition_get_name (data->partition), name) != 0)
        {
          error = NULL;
          if (!udisks_partition_call_set_name_sync (data->partition,
                                                    name,
                                                    g_variant_new ("a{sv}", NULL), /* options */
                                                    NULL, /* GCancellable */
                                                    &error))
            {
              gdu_utils_show_error (GTK_WINDOW (window), _("Error setting partition name"), error);
              g_error_free (error);
              goto set_out;
            }
        }
      if (udisks_partition_get_flags (data->partition) != flags)
        {
          error = NULL;
          if (!udisks_partition_call_set_flags_sync (data->partition,
                                                     flags,
                                                     g_variant_new ("a{sv}", NULL), /* options */
                                                     NULL, /* GCancellable */
                                                     &error))
            {
              gdu_utils_show_error (GTK_WINDOW (window), _("Error setting partition flags"), error);
              g_error_free (error);
              goto set_out;
            }
        }
    set_out:
      g_free (type);
      g_free (name);
    }

  edit_partition_data_free (data);
}
