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
#include "gdupartitiondialog.h"
#include "gduvolumegrid.h"
#include "gduutils.h"

/* ---------------------------------------------------------------------------------------------------- */


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

  const gchar **part_types;
} EditPartitionData;

static void
edit_partition_data_free (EditPartitionData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->object);
  g_object_unref (data->partition);
  g_object_unref (data->partition_table);
  g_free (data->partition_table_type);
  g_free (data->part_types);
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
  gint active;

  active = gtk_combo_box_get_active (GTK_COMBO_BOX (data->type_combobox));
  if (active > 0)
    type = g_strdup (data->part_types[active]);

  if (g_strcmp0 (data->partition_table_type, "gpt") == 0)
    {
      name = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->name_entry)));
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->system_checkbutton)))
        flags |= (1UL<<0);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->bootable_checkbutton)))
        flags |= (1UL<<2);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->readonly_checkbutton)))
        flags |= (1UL<<60);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->hidden_checkbutton)))
        flags |= (1UL<<62);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->do_not_automount_checkbutton)))
        flags |= (1UL<<63);
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
  guint n;
  gint active_index;

  cur_type = udisks_partition_get_type_ (data->partition);
  data->part_types = udisks_client_get_partition_types (gdu_window_get_client (data->window),
                                                        data->partition_table_type);
  active_index = -1;
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (data->type_combobox));
  for (n = 0; data->part_types != NULL && data->part_types[n] != NULL; n++)
    {
      const gchar *type = data->part_types[n];
      const gchar *type_for_display;
      gchar *s;

      type_for_display = udisks_client_get_partition_type_for_display (gdu_window_get_client (data->window),
                                                                       data->partition_table_type,
                                                                       type);
      if (g_strcmp0 (type, cur_type) == 0)
        active_index = n;
      s = g_strdup_printf ("%s (%s)", type_for_display, type);
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (data->type_combobox), NULL, s);
      g_free (s);
    }
  if (active_index > 0)
    gtk_combo_box_set_active (GTK_COMBO_BOX (data->type_combobox), active_index);

  if (g_strcmp0 (data->partition_table_type, "gpt") == 0)
    {
      guint64 flags;
      gtk_entry_set_text (GTK_ENTRY (data->name_entry), udisks_partition_get_name (data->partition));
      flags = udisks_partition_get_flags (data->partition);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->system_checkbutton),           (flags & (1UL<< 0)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->bootable_checkbutton),         (flags & (1UL<< 2)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->readonly_checkbutton),         (flags & (1UL<<60)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->hidden_checkbutton),           (flags & (1UL<<62)) != 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->do_not_automount_checkbutton), (flags & (1UL<<63)) != 0);
    }
  else if (g_strcmp0 (data->partition_table_type, "dos") == 0)
    {
      guint64 flags;
      flags = udisks_partition_get_flags (data->partition);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->bootable_checkbutton),         (flags & (1UL<< 7)) != 0);
    }
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
      data->dialog = gdu_application_new_widget (gdu_window_get_application (window),
                                                 "edit-gpt-partition-dialog.ui",
                                                 "edit-gpt-partition-dialog",
                                                 &data->builder);
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
      data->dialog = gdu_application_new_widget (gdu_window_get_application (window),
                                                 "edit-dos-partition-dialog.ui",
                                                 "edit-dos-partition-dialog",
                                                 &data->builder);
      data->bootable_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "bootable-checkbutton"));
      g_signal_connect (data->bootable_checkbutton,
                        "notify::active", G_CALLBACK (edit_partition_property_changed), data);
    }
  else
    {
      data->dialog = gdu_application_new_widget (gdu_window_get_application (window),
                                                 "edit-partition-dialog.ui",
                                                 "edit-partition-dialog",
                                                 &data->builder);
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
              gdu_window_show_error (window, _("Error setting partition type"), error);
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
              gdu_window_show_error (window, _("Error setting partition name"), error);
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
              gdu_window_show_error (window, _("Error setting partition flags"), error);
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
