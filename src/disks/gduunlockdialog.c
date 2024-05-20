/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2022 Purism SPC
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 */

#include "config.h"

#include <glib/gi18n.h>
#include <libsecret/secret.h>
#include <stdlib.h>
#include <errno.h>

#include "gdu-application.h"
#include "gdu-window.h"
#include "gduunlockdialog.h"

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

struct _GduUnlockDialog
{
  GtkDialog             parent_instance;

  GtkBox               *infobar_box;
  GtkLabel             *unknown_crypto_label;
  GtkEntry             *passphrase_entry;

  GtkCheckButton       *tcrypt_hidden_check_button;
  GtkCheckButton       *tcrypt_system_check_button;
  GtkEntry             *pim_entry;

  GtkBox               *keyfile_file_chooser_box;
  // GtkFileChooserButton *keyfile_file_chooser_button;

  GtkButton            *cancel_button;
  GtkButton            *unlock_button;

  UDisksObject         *udisks_object;
  UDisksBlock          *udisks_block;
  UDisksEncrypted      *udisks_encrypted;

  GListStore           *key_files_store;
  GVariant             *keyfiles;
  gulong               pim;
};

G_DEFINE_TYPE (GduUnlockDialog, gdu_unlock_dialog, GTK_TYPE_DIALOG)

static void
luks_find_passphrase_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GduUnlockDialog) self = user_data;
  g_autofree char *passphrase = NULL;

  g_assert (GDU_IS_UNLOCK_DIALOG (self));

  /* Don't fail if a keyring error occured... but if we do find a
   * passphrase then put it into the entry field and show a
   * cluebar
   */
  passphrase = secret_password_lookup_finish (result, NULL);
  if (passphrase)
    {
      /* gtk4 todo: use AdwBanner
      GtkWidget *infobar;

      infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO,
                                           _("The encryption passphrase was retrieved from the keyring"),
                                           NULL);
      gtk_box_append (self->infobar_box, infobar);
      gtk_widget_set_visible (GTK_WIDGET (self->infobar_box), TRUE);
      gtk_editable_set_text (GTK_EDITABLE (self->passphrase_entry), passphrase);
      */
    }

  gtk_window_present (GTK_WINDOW (self));
}

static void
unlock_cb (UDisksEncrypted *encrypted,
           GAsyncResult    *result,
           gpointer         user_data)
{
  g_autoptr(GduUnlockDialog) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_UNLOCK_DIALOG (self));

  if (!udisks_encrypted_call_unlock_finish (encrypted,
                                            NULL, /* out_cleartext_device */
                                            result,
                                            &error))
    {
      GtkWindow *window;

      window = gtk_window_get_transient_for (GTK_WINDOW (self));
      gdu_utils_show_error (window, _("Error unlocking device"), error);
    }

  gtk_window_close (GTK_WINDOW (self));
}

static void
do_unlock (GduUnlockDialog *self)
{
  GVariantBuilder options_builder;
  const char *passphrase;
  gboolean is_hidden, is_system;

  is_hidden = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->tcrypt_hidden_check_button));
  is_system = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->tcrypt_system_check_button));

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (is_hidden)
    g_variant_builder_add (&options_builder, "{sv}", "hidden", g_variant_new_boolean (TRUE));
  if (is_system)
    g_variant_builder_add (&options_builder, "{sv}", "system", g_variant_new_boolean (TRUE));
  if (self->pim)
    g_variant_builder_add (&options_builder, "{sv}", "pim", g_variant_new_uint32 (self->pim));
  if (self->keyfiles)
    g_variant_builder_add (&options_builder, "{sv}", "keyfiles", g_variant_ref (self->keyfiles));

  passphrase = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry));
  udisks_encrypted_call_unlock (self->udisks_encrypted,
                                passphrase,
                                g_variant_builder_end (&options_builder),
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) unlock_cb,
                                g_object_ref (self));
}

static void
unlock_dialog_response_cb (GduUnlockDialog *self,
                           int              response_id)
{
  g_assert (GDU_IS_UNLOCK_DIALOG (self));

  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);

  if (response_id == GTK_RESPONSE_OK)
    {
      const char *type;

      type = udisks_block_get_id_type (self->udisks_block);

      if (g_strcmp0 (type, "crypto_TCRYPT") == 0 || g_strcmp0 (type, "crypto_unknown") == 0)
        {
          g_autoptr(GVariantBuilder) builder = NULL;
          guint n_items;

          n_items = g_list_model_get_n_items (G_LIST_MODEL (self->key_files_store));
          if (n_items)
            builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));

          /* gtk4 todo: Replace with GtkFileDialog
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr(GtkFileChooserButton) button = NULL;
              g_autofree char *filename = NULL;

              button = g_list_model_get_item (G_LIST_MODEL (self->key_files_store), i);
              filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (button));

              if (filename)
                g_variant_builder_add (builder, "s", filename);
            }
          */

          if (n_items)
            self->keyfiles = g_variant_new ("as", builder);
        }
      do_unlock (self);
    }
  else
    {
      /* otherwise, we are done */
      gtk_window_close (GTK_WINDOW (self));
    }
}

static void
unlock_dialog_update_unlock_button_cb (GduUnlockDialog *self)
{
  GtkStyleContext *context;
  const char *passphrase, *pim_str;
  guint n_items;
  gboolean can_unlock;

  g_assert (GDU_IS_UNLOCK_DIALOG (self));

  context = gtk_widget_get_style_context (GTK_WIDGET (self->pim_entry));
  passphrase = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry));
  can_unlock = passphrase && *passphrase;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->key_files_store));
  can_unlock = can_unlock || n_items > 0;

  pim_str = gtk_editable_get_text (GTK_EDITABLE (self->pim_entry));
  if (gtk_widget_is_visible (GTK_WIDGET (self->pim_entry)) &&
      pim_str && *pim_str)
    {
      char *end;

      errno = 0;
      self->pim = strtoul (pim_str, &end, 10);
      if (*end || errno || self->pim == 0 || self->pim > G_MAXUINT32)
        {
          gtk_style_context_add_class (context, "error");
          self->pim = 0;
          can_unlock = FALSE;
        }
      else
        {
          gtk_style_context_remove_class (context, "error");
        }
    }
  else
    {
      gtk_style_context_remove_class (context, "error");
    }

  gtk_widget_set_sensitive (GTK_WIDGET (self->unlock_button), can_unlock);
}

static void
unlock_dialog_keyfile_set_cb (GduUnlockDialog *self,
                              GtkWidget       *button)
{
  GtkWidget *new_button;

  g_assert (GDU_IS_UNLOCK_DIALOG (self));
  // g_assert (GTK_IS_FILE_CHOOSER_BUTTON (button)); gtk4 todo

  /* Don't call this function again for this instance */
  g_signal_handlers_disconnect_by_func (button, unlock_dialog_keyfile_set_cb, self);

  g_list_store_append (self->key_files_store, button);

  /* gtk4 todo: Replace with GtkFileDialog
  new_button = gtk_file_chooser_button_new (_("Select a Keyfile"), GTK_FILE_CHOOSER_ACTION_OPEN);
  gtk_widget_set_visible (new_button, TRUE);
  gtk_container_add (GTK_CONTAINER (self->keyfile_file_chooser_box), new_button);
  g_signal_connect_object (new_button, "file-set",
                           G_CALLBACK (unlock_dialog_keyfile_set_cb),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (new_button, "file-set",
                           G_CALLBACK (unlock_dialog_update_unlock_button_cb),
                           self, G_CONNECT_SWAPPED);
  */
}

static void
gdu_unlock_dialog_finalize (GObject *object)
{
  GduUnlockDialog *self = (GduUnlockDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_block);
  g_clear_object (&self->udisks_encrypted);
  g_clear_object (&self->key_files_store);

  G_OBJECT_CLASS (gdu_unlock_dialog_parent_class)->finalize (object);
}

static void
gdu_unlock_dialog_class_init (GduUnlockDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gdu_unlock_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "unlock-device-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, unknown_crypto_label);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, passphrase_entry);

  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, tcrypt_hidden_check_button);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, tcrypt_system_check_button);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, pim_entry);

  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, keyfile_file_chooser_box);
  // gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, keyfile_file_chooser_button); gtk4 todo

  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, cancel_button);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, unlock_button);

  gtk_widget_class_bind_template_callback (widget_class, unlock_dialog_response_cb);
  gtk_widget_class_bind_template_callback (widget_class, unlock_dialog_update_unlock_button_cb);
  gtk_widget_class_bind_template_callback (widget_class, unlock_dialog_keyfile_set_cb);
}

static void
gdu_unlock_dialog_init (GduUnlockDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  /* gtk4 todo
  self->key_files_store = g_list_store_new (GTK_TYPE_FILE_CHOOSER_BUTTON);
  */
}

static GduUnlockDialog *
gdu_unlock_dialog_new (void)
{
  return g_object_new (GDU_TYPE_UNLOCK_DIALOG, NULL);
}

static void
gdu_unlock_dialog_set_disk (GduUnlockDialog *self,
                            UDisksObject    *object)
{
  const char *type;
  gboolean password_in_crypttab;

  g_return_if_fail (GDU_IS_UNLOCK_DIALOG (self));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);
  self->udisks_encrypted = udisks_object_get_encrypted (object);

  type = udisks_block_get_id_type (self->udisks_block);

  if (g_strcmp0 (type, "crypto_TCRYPT") == 0 || g_strcmp0 (type, "crypto_unknown") == 0)
    {
      gtk_window_set_title (GTK_WINDOW (self), _("Set options to unlock"));
      // gtk_widget_set_visible (GTK_WIDGET (self->keyfile_file_chooser_button), TRUE); gtk4 todo

      if (g_str_equal (type, "crypto_unknown"))
        gtk_widget_set_visible (GTK_WIDGET (self->unknown_crypto_label), TRUE);
    }

  if (gdu_utils_has_configuration (self->udisks_block, "crypttab", &password_in_crypttab) &&
      password_in_crypttab)
    {
      do_unlock (self);
    }
  else
    {
      secret_password_lookup (&luks_passphrase_schema,
                              NULL, /* GCancellable */
                              luks_find_passphrase_cb,
                              g_object_ref (self),
                              "gvfs-luks-uuid", udisks_block_get_id_uuid (self->udisks_block),
                              NULL); /* sentinel */
    }
}

void
gdu_unlock_dialog_show (GduWindow    *window,
                        UDisksObject *object)
{
  GduUnlockDialog *self;

  self = gdu_unlock_dialog_new ();
  gdu_unlock_dialog_set_disk (self, object);
  gtk_window_set_transient_for (GTK_WINDOW (self), GTK_WINDOW (window));
}
