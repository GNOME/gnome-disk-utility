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
#include <libsecret/secret.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduunlockdialog.h"
#include "gduvolumegrid.h"

/* From GVfs's monitor/udisks2/gvfsudisks2volume.c */
static const SecretSchema luks_passphrase_schema =
{
  "org.gnome.GVfs.Luks.Password",
  SECRET_SCHEMA_DONT_MATCH_NAME,
  {
    { "gvfs-luks-uuid", SECRET_SCHEMA_ATTRIBUTE_STRING },
    { NULL, 0 },
  }
};

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksObject *object;
  UDisksBlock *block;
  UDisksEncrypted *encrypted;

  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;
  GtkWidget *infobar_vbox;
  GtkWidget *entry;
  GtkWidget *show_passphrase_check_button;

  gchar *passphrase;
} DialogData;

static void
dialog_data_free (DialogData *data)
{
  if (data->dialog != NULL)
    {
      gtk_widget_hide (data->dialog);
      gtk_widget_destroy (data->dialog);
    }
  if (data->object != NULL)
    g_object_unref (data->object);
  if (data->window != NULL)
    g_object_unref (data->window);
  if (data->builder != NULL)
    g_object_unref (data->builder);

  g_free (data->passphrase);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
unlock_cb (UDisksEncrypted *encrypted,
           GAsyncResult    *res,
           gpointer         user_data)
{
  DialogData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_encrypted_call_unlock_finish (encrypted,
                                            NULL, /* out_cleartext_device */
                                            res,
                                            &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error unlocking encrypted device"),
                            error);
      g_error_free (error);
    }
  dialog_data_free (data);
}

static void
do_unlock (DialogData *data)
{
  udisks_encrypted_call_unlock (data->encrypted,
                                data->passphrase,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) unlock_cb,
                                data);
}

static void
show_dialog (DialogData *data)
{
  gint response;

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (data->entry);

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      gtk_widget_hide (data->dialog);
      data->passphrase = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->entry)));
      do_unlock (data);
    }
  else
    {
      /* otherwise, we are done */
      dialog_data_free (data);
    }
}

static void
luks_find_passphrase_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  DialogData *data = user_data;
  gchar *passphrase = NULL;

  /* Don't fail if a keyring error occured... but if we do find a
   * passphrase then put it into the entry field and show a
   * cluebar
   */
  passphrase = secret_password_lookup_finish (result, NULL);
  if (passphrase != NULL)
    {
      GtkWidget *infobar;
      infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO,
                                           _("The encryption passphrase was retrieved from the keyring"),
                                           NULL);
      gtk_box_pack_start (GTK_BOX (data->infobar_vbox), infobar, TRUE, TRUE, 0);
      gtk_entry_set_text (GTK_ENTRY (data->entry), passphrase);
    }
  else
    {
      gtk_widget_hide (data->infobar_vbox);
      gtk_widget_set_no_show_all (data->infobar_vbox, TRUE);
    }
  show_dialog (data);
  g_free (passphrase);
}

void
gdu_unlock_dialog_show (GduWindow    *window,
                        UDisksObject *object)
{
  gboolean has_passphrase_in_crypttab = FALSE;
  DialogData *data;

  data = g_new0 (DialogData, 1);
  data->object = g_object_ref (object);
  data->block = udisks_object_peek_block (object);
  data->encrypted = udisks_object_peek_encrypted (object);
  data->window = g_object_ref (window);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "unlock-device-dialog.ui",
                                                         "unlock-device-dialog",
                                                         &data->builder));
  data->infobar_vbox = GTK_WIDGET (gtk_builder_get_object (data->builder, "infobar-vbox"));
  data->entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-passphrase-entry"));
  data->show_passphrase_check_button = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-show-passphrase-check-button"));

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (data->window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->show_passphrase_check_button), FALSE);
  gtk_entry_set_text (GTK_ENTRY (data->entry), "");

  g_object_bind_property (data->show_passphrase_check_button,
                          "active",
                          data->entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);

  if (gdu_utils_has_configuration (data->block, "crypttab", &has_passphrase_in_crypttab) &&
      has_passphrase_in_crypttab)
    {
      data->passphrase = g_strdup ("");
      do_unlock (data);
    }
  else
    {
      /* see if there's a passphrase in the keyring */
      secret_password_lookup (&luks_passphrase_schema,
                              NULL, /* GCancellable */
                              luks_find_passphrase_cb,
                              data,
                              "gvfs-luks-uuid", udisks_block_get_id_uuid (data->block),
                              NULL); /* sentinel */
    }
}
