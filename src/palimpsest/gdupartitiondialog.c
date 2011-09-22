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
  GtkWidget *dialog;
  gchar *orig_type;
  const gchar **part_types;
} ChangePartitionTypeData;

static void
on_change_partition_type_combo_box_changed (GtkComboBox *combo_box,
                                            gpointer     user_data)
{
  ChangePartitionTypeData *data = user_data;
  gint active;
  gboolean sensitive;

  sensitive = FALSE;
  active = gtk_combo_box_get_active (combo_box);
  if (active > 0)
    {
      if (g_strcmp0 (data->part_types[active], data->orig_type) != 0)
        {
          sensitive = TRUE;
        }
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     sensitive);
}

void
gdu_partition_dialog_show (GduWindow    *window,
                           UDisksObject *object)
{
  gint response;
  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *combo_box;
  UDisksBlock *block;
  const gchar *scheme;
  const gchar *cur_type;
  const gchar **part_types;
  guint n;
  gint active_index;
  ChangePartitionTypeData data;
  const gchar *type_to_set;

  block = udisks_object_peek_block (object);
  g_assert (block != NULL);

  dialog = gdu_application_new_widget (gdu_window_get_application (window),
                                       "edit-partition-dialog.ui",
                                       "change-partition-type-dialog",
                                       &builder);
  combo_box = GTK_WIDGET (gtk_builder_get_object (builder, "change-partition-type-combo-box"));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  scheme = udisks_block_get_part_entry_scheme (block);
  cur_type = udisks_block_get_part_entry_type (block);
  part_types = udisks_util_get_part_types_for_scheme (scheme);
  active_index = -1;
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (combo_box));
  for (n = 0; part_types != NULL && part_types[n] != NULL; n++)
    {
      const gchar *type;
      gchar *type_for_display;
      type = part_types[n];
      type_for_display = udisks_util_get_part_type_for_display (scheme, type, TRUE);
      if (g_strcmp0 (type, cur_type) == 0)
        active_index = n;
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo_box), NULL, type_for_display);
      g_free (type_for_display);
    }

  g_signal_connect (combo_box,
                    "changed",
                    G_CALLBACK (on_change_partition_type_combo_box_changed),
                    &data);
  memset (&data, '\0', sizeof (ChangePartitionTypeData));
  data.dialog = dialog;
  data.orig_type = g_strdup (cur_type);
  data.part_types = part_types;

  if (active_index > 0)
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), active_index);

  gtk_widget_show_all (dialog);
  gtk_widget_grab_focus (combo_box);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    goto out;

  type_to_set = part_types[gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box))];

  g_debug ("TODO: set partition type to %s", type_to_set);

 out:
  g_free (part_types);
  g_free (data.orig_type);
  gtk_widget_hide (dialog);
  gtk_widget_destroy (dialog);
  g_object_unref (builder);
}
