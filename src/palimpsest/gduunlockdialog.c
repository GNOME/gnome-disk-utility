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
#include "gduunlockdialog.h"
#include "gduvolumegrid.h"
#include "gduutils.h"

/* ---------------------------------------------------------------------------------------------------- */

static void
unlock_cb (UDisksEncrypted *encrypted,
           GAsyncResult    *res,
           gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_encrypted_call_unlock_finish (encrypted,
                                            NULL, /* out_cleartext_device */
                                            res,
                                            &error))
    {
      gdu_window_show_error (window,
                             _("Error unlocking encrypted device"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

void
gdu_unlock_dialog_show (GduWindow    *window,
                        UDisksObject *object)
{
  gint response;
  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *entry;
  GtkWidget *show_passphrase_check_button;
  UDisksBlock *block;
  UDisksEncrypted *encrypted;
  const gchar *passphrase;
  gboolean has_passphrase;

  dialog = NULL;
  builder = NULL;

  /* TODO: look up passphrase from gnome-keyring? */

  block = udisks_object_peek_block (object);
  encrypted = udisks_object_peek_encrypted (object);

  passphrase = "";
  has_passphrase = FALSE;
  if (gdu_utils_has_configuration (block, "crypttab", &has_passphrase) && has_passphrase)
    goto do_call;

  dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                   "unlock-device-dialog.ui",
                                                   "unlock-device-dialog",
                                                   &builder));
  entry = GTK_WIDGET (gtk_builder_get_object (builder, "unlock-device-passphrase-entry"));
  show_passphrase_check_button = GTK_WIDGET (gtk_builder_get_object (builder, "unlock-device-show-passphrase-check-button"));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_passphrase_check_button), FALSE);
  gtk_entry_set_text (GTK_ENTRY (entry), "");

  g_object_bind_property (show_passphrase_check_button,
                          "active",
                          entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);

  gtk_widget_show_all (dialog);
  gtk_widget_grab_focus (entry);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    goto out;

  passphrase = gtk_entry_get_text (GTK_ENTRY (entry));

 do_call:
  udisks_encrypted_call_unlock (encrypted,
                                passphrase,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) unlock_cb,
                                g_object_ref (window));

 out:
  if (dialog != NULL)
    {
      gtk_widget_hide (dialog);
      gtk_widget_destroy (dialog);
    }
  if (builder != NULL)
    g_object_unref (builder);
}
