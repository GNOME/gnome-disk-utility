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
#include "gdufilesystemdialog.h"
#include "gduvolumegrid.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GtkWidget *dialog;
  gchar *orig_label;
} ChangeFilesystemLabelData;

static void
on_change_filesystem_label_entry_changed (GtkEditable *editable,
                                          gpointer     user_data)
{
  ChangeFilesystemLabelData *data = user_data;
  gboolean sensitive;

  sensitive = FALSE;
  if (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (editable)), data->orig_label) != 0)
    {
      sensitive = TRUE;
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     sensitive);
}

static void
change_filesystem_label_cb (UDisksFilesystem  *filesystem,
                            GAsyncResult      *res,
                            gpointer           user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_filesystem_call_set_label_finish (filesystem,
                                                res,
                                                &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error setting label"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

void
gdu_filesystem_dialog_show (GduWindow    *window,
                            UDisksObject *object)
{
  gint response;
  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *entry;
  UDisksBlock *block;
  UDisksFilesystem *filesystem;
  const gchar *label;
  ChangeFilesystemLabelData data;
  const gchar *label_to_set;

  block = udisks_object_peek_block (object);
  filesystem = udisks_object_peek_filesystem (object);
  g_assert (block != NULL);
  g_assert (filesystem != NULL);

  dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                   "edit-filesystem-dialog.ui",
                                                   "change-filesystem-label-dialog",
                                                   &builder));
  entry = GTK_WIDGET (gtk_builder_get_object (builder, "change-filesystem-label-entry"));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  label = udisks_block_get_id_label (block);
  g_signal_connect (entry,
                    "changed",
                    G_CALLBACK (on_change_filesystem_label_entry_changed),
                    &data);
  memset (&data, '\0', sizeof (ChangeFilesystemLabelData));
  data.dialog = dialog;
  data.orig_label = g_strdup (label);

  gtk_entry_set_text (GTK_ENTRY (entry), label);
  gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);

  gtk_widget_show_all (dialog);
  gtk_widget_grab_focus (entry);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    goto out;

  label_to_set = gtk_entry_get_text (GTK_ENTRY (entry));

  udisks_filesystem_call_set_label (filesystem,
                                    label_to_set,
                                    g_variant_new ("a{sv}", NULL), /* options */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback) change_filesystem_label_cb,
                                    g_object_ref (window));

 out:
  g_free (data.orig_label);
  gtk_widget_hide (dialog);
  gtk_widget_destroy (dialog);
  g_object_unref (builder);
}
