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
#include <stdlib.h>
#include <errno.h>

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
  gchar* type;

  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;
  GtkWidget *unlock_button;
  GtkWidget *infobar_vbox;
  GtkWidget *passphrase_entry;
  GtkWidget *show_passphrase_check_button;
  GtkWidget *tcrypt_hidden_check_button;
  GtkWidget *tcrypt_system_check_button;
  GtkWidget *pim_entry;

  GList *tcrypt_keyfile_button_list;

  gchar *passphrase;
  GVariant *keyfiles;
  guint num_keyfiles;
  guint pim;
  gboolean hidden_volume;
  gboolean system_volume;
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

  if (g_strcmp0 (data->type, "crypto_TCRYPT") == 0 || g_strcmp0 (data->type, "crypto_unknown") == 0)
    // The elements are already freed by the builder / parent widget
    g_list_free (data->tcrypt_keyfile_button_list);

  g_free (data->type);
  g_free (data->passphrase);
  g_free (data);
}

static void
update_unlock_button_sensitivity (GObject *object,
                                  gpointer user_data)
{
  DialogData *data = user_data;
  gboolean password_entered = gtk_entry_get_text_length (GTK_ENTRY (data->passphrase_entry)) > 0;
  gboolean keyfile_chosen = (data->tcrypt_keyfile_button_list != NULL &&
                             g_list_length (data->tcrypt_keyfile_button_list) > 1);

  gtk_widget_set_sensitive (data->unlock_button, password_entered || keyfile_chosen);
}

static void
on_tcrypt_keyfile_set (GObject     *object,
                       gpointer     user_data)
{
  DialogData *data = user_data;
  GtkWidget *button;
  GtkWidget *sibling;
  GtkGrid *grid;
  GList *list;

  grid = GTK_GRID (gtk_builder_get_object (data->builder, "unlock-device-grid"));

  button = gtk_file_chooser_button_new (_("Select a Keyfile"), GTK_FILE_CHOOSER_ACTION_OPEN);

  sibling = NULL;
  for (list = data->tcrypt_keyfile_button_list; list != NULL; list = list->next)
    sibling = (GTK_WIDGET (list->data));

  gtk_grid_attach_next_to (grid, button, sibling, GTK_POS_BOTTOM, 1, 1);
  gtk_widget_show (button);

  data->tcrypt_keyfile_button_list = g_list_append (data->tcrypt_keyfile_button_list, button);
  g_signal_connect (button, "file-set", G_CALLBACK (on_tcrypt_keyfile_set), data);

  // Don't call this function again for this instance
  g_signal_handlers_disconnect_by_func (object, on_tcrypt_keyfile_set, user_data);

  update_unlock_button_sensitivity (object, user_data);
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
                            _("Error unlocking device"),
                            error);
      g_error_free (error);
    }
  dialog_data_free (data);
}

static void
do_unlock (DialogData *data)
{
  GVariantBuilder options_builder;
  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (data->hidden_volume)
    g_variant_builder_add (&options_builder, "{sv}", "hidden", g_variant_new_boolean (TRUE));
  if (data->system_volume)
    g_variant_builder_add (&options_builder, "{sv}", "system", g_variant_new_boolean (TRUE));
  if (data->pim != 0)
    g_variant_builder_add (&options_builder, "{sv}", "pim", g_variant_new_uint32 (data->pim));
  if (data->num_keyfiles != 0)
    g_variant_builder_add (&options_builder, "{sv}", "keyfiles", data->keyfiles);

  udisks_encrypted_call_unlock (data->encrypted,
                                data->passphrase,
                                g_variant_builder_end (&options_builder),
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) unlock_cb,
                                data);
}

static void
show_dialog (DialogData *data)
{
  gint response;
  gchar *err;
  GError *error;
  GVariantBuilder *keyfiles_builder;
  GList *list;
  const char* filename;
  const char* text_pim;
  guint64 tmp_pim;

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (data->passphrase_entry);

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      gtk_widget_hide (data->dialog);
      data->passphrase = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry)));

      if (g_strcmp0 (data->type, "crypto_TCRYPT") == 0 || g_strcmp0 (data->type, "crypto_unknown") == 0)
        {
          data->hidden_volume = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->tcrypt_hidden_check_button));
          data->system_volume = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->tcrypt_system_check_button));

          // Add keyfiles
          data->num_keyfiles = 0;
          keyfiles_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
          for (list = data->tcrypt_keyfile_button_list; list != NULL; list = list->next)
            {
              filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (list->data));
              if (filename == NULL)
                continue;
              g_variant_builder_add (keyfiles_builder, "s", filename);
              data->num_keyfiles += 1;
            }
          data->keyfiles = g_variant_new ("as", keyfiles_builder);
          g_variant_builder_unref (keyfiles_builder);

          text_pim = gtk_entry_get_text (GTK_ENTRY (data->pim_entry));
          if (text_pim && strlen (text_pim) > 0)
            {
              errno = 0;
              tmp_pim = strtoul ( text_pim, &err, 10);
              if (*err || errno || tmp_pim <= 0 || tmp_pim > G_MAXUINT32)
                {
                  error = g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                      _("Invalid PIM"));
                  gdu_utils_show_error (GTK_WINDOW(data->window),
                                       _("Error unlocking device"),
                                       error);
                  g_error_free (error);
                  dialog_data_free (data);
                  return;
                }
              data->pim = tmp_pim;
            }
        }
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
      gtk_entry_set_text (GTK_ENTRY (data->passphrase_entry), passphrase);
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
  data->type = udisks_block_dup_id_type (data->block);
  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "unlock-device-dialog.ui",
                                                         "unlock-device-dialog",
                                                         &data->builder));
  data->unlock_button = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-unlock-button"));
  data->infobar_vbox = GTK_WIDGET (gtk_builder_get_object (data->builder, "infobar-vbox"));
  data->passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-passphrase-entry"));
  data->show_passphrase_check_button = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-show-passphrase-check-button"));

  // Add TCRYPT options if the device is (possibly) a TCRYPT volume
  if (g_strcmp0 (data->type, "crypto_TCRYPT") == 0 || g_strcmp0 (data->type, "crypto_unknown") == 0)
    {
      GtkWidget *volume_type_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-volume-type-label"));
      GtkWidget *keyfiles_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-keyfiles-label"));
      GtkWidget *pim_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-pim-label"));
      GtkWidget *button = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-tcrypt-keyfile-chooser-button"));

      gtk_window_set_title (GTK_WINDOW (data->dialog), _("Set options to unlock"));

      data->tcrypt_hidden_check_button = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-tcrypt-hidden-check-button"));
      data->tcrypt_system_check_button = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-tcrypt-system-check-button"));
      data->pim_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "unlock-device-pim-entry"));
      data->tcrypt_keyfile_button_list = NULL;
      data->tcrypt_keyfile_button_list = g_list_append (data->tcrypt_keyfile_button_list, button);

      gtk_widget_set_visible (volume_type_label, TRUE);
      gtk_widget_set_visible (pim_label, TRUE);
      gtk_widget_set_visible (keyfiles_label, TRUE);
      gtk_widget_set_visible (button, TRUE);
      gtk_widget_set_visible (data->tcrypt_hidden_check_button, TRUE);
      gtk_widget_set_visible (data->tcrypt_system_check_button, TRUE);
      gtk_widget_set_visible (data->pim_entry, TRUE);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->tcrypt_hidden_check_button), FALSE);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->tcrypt_system_check_button), FALSE);
      gtk_entry_set_text (GTK_ENTRY (data->pim_entry), "");

      g_signal_connect (button, "file-set", G_CALLBACK (on_tcrypt_keyfile_set), data);

      if (g_strcmp0 (data->type, "crypto_unknown") == 0)
        {
          GtkWidget *unknown_crypto_label;
          unknown_crypto_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "unknown-crypto-label"));
          gtk_widget_set_visible (unknown_crypto_label, TRUE);
          gtk_widget_set_no_show_all (unknown_crypto_label, FALSE);
        }
    }

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (data->window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->show_passphrase_check_button), FALSE);
  gtk_entry_set_text (GTK_ENTRY (data->passphrase_entry), "");
  g_signal_connect (data->passphrase_entry, "changed", G_CALLBACK (update_unlock_button_sensitivity), data);

  g_object_bind_property (data->show_passphrase_check_button,
                          "active",
                          data->passphrase_entry,
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
