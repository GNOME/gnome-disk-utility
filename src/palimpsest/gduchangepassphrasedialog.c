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
#include "gduchangepassphrasedialog.h"
#include "gdupasswordstrengthwidget.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GduWindow *window;
  UDisksObject *object;
  UDisksEncrypted *encrypted;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *existing_passphrase_entry;
  GtkWidget *passphrase_entry;
  GtkWidget *confirm_passphrase_entry;
  GtkWidget *show_passphrase_checkbutton;
  GtkWidget *passphrase_strengh_box;
  GtkWidget *passphrase_strengh_widget;

} ChangePassphraseData;

static void
change_passphrase_data_free (ChangePassphraseData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->object);
  g_object_unref (data->encrypted);
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
populate (ChangePassphraseData *data)
{
  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->existing_passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->confirm_passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
}

static void
update (ChangePassphraseData *data)
{
  gboolean can_proceed = FALSE;
  const gchar *existing_passphrase;
  const gchar *passphrase;
  const gchar *confirm_passphrase;

  existing_passphrase = gtk_entry_get_text (GTK_ENTRY (data->existing_passphrase_entry));
  passphrase = gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry));
  confirm_passphrase = gtk_entry_get_text (GTK_ENTRY (data->confirm_passphrase_entry));

  gdu_password_strength_widget_set_password (GDU_PASSWORD_STRENGTH_WIDGET (data->passphrase_strengh_widget),
                                             passphrase);

  if (strlen (existing_passphrase) > 0 && strlen (passphrase) > 0 &&
      g_strcmp0 (passphrase, confirm_passphrase) == 0)
    can_proceed = TRUE;
  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
on_property_changed (GObject     *object,
                                    GParamSpec  *pspec,
                                    gpointer     user_data)
{
  ChangePassphraseData *data = user_data;
  update (data);
}

static void
change_passphrase_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  ChangePassphraseData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_encrypted_call_change_passphrase_finish (UDISKS_ENCRYPTED (source_object),
                                                       res,
                                                       &error))
    {
      gdu_window_show_error (data->window, _("Error changing passphrase"), error);
      g_error_free (error);
    }
  change_passphrase_data_free (data);
}

void
gdu_change_passphrase_dialog_show (GduWindow    *window,
                                   UDisksObject *object)
{
  ChangePassphraseData *data;
  gint response;

  data = g_new0 (ChangePassphraseData, 1);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->encrypted = udisks_object_get_encrypted (object);
  g_assert (data->encrypted != NULL);

  data->dialog = gdu_application_new_widget (gdu_window_get_application (window),
                                             "change-passphrase-dialog.ui",
                                             "change-passphrase-dialog",
                                             &data->builder);

  data->existing_passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "existing-passphrase-entry"));
  g_signal_connect (data->existing_passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), data);

  data->passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "passphrase-entry"));
  g_signal_connect (data->passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), data);

  data->confirm_passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "confirm-passphrase-entry"));
  g_signal_connect (data->confirm_passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), data);
  data->show_passphrase_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "show-passphrase-checkbutton"));
  g_signal_connect (data->show_passphrase_checkbutton, "notify::active", G_CALLBACK (on_property_changed), data);

  data->passphrase_strengh_box = GTK_WIDGET (gtk_builder_get_object (data->builder, "passphrase-strength-box"));
  data->passphrase_strengh_widget = gdu_password_strength_widget_new ();
  gtk_widget_set_tooltip_markup (data->passphrase_strengh_widget,
                                 _("The strength of the passphrase"));
  gtk_box_pack_start (GTK_BOX (data->passphrase_strengh_box), data->passphrase_strengh_widget,
                      TRUE, TRUE, 0);


  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  populate (data);
  update (data);

  gtk_widget_show_all (data->dialog);

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  gtk_widget_hide (data->dialog);
  if (response == GTK_RESPONSE_OK)
    {
      udisks_encrypted_call_change_passphrase (data->encrypted,
                                               gtk_entry_get_text (GTK_ENTRY (data->existing_passphrase_entry)),
                                               gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry)),
                                               g_variant_new ("a{sv}", NULL), /* options */
                                               NULL, /* GCancellable */
                                               change_passphrase_cb,
                                               data);
    }
  else
    {
      change_passphrase_data_free (data);
    }
}
