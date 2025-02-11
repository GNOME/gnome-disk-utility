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

#include "gdu-unlock-dialog.h"

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
  AdwAlertDialog        parent_instance;

  GtkWidget            *keyring_banner;
  GtkWidget            *veracrypt_banner;
  GtkWidget            *parent_window;
  GtkWidget            *passphrase_entry;
  GtkWidget            *tcrypt_hidden_switch_row;
  GtkWidget            *tcrypt_system_switch_row;
  GtkWidget            *pim_entry_row;
  GtkWidget            *keyfile_file_chooser_box;
  GtkWidget            *keyfile_chooser;
  GtkWidget            *keyfile_row;
  GtkWidget            *unlock_button;

  UDisksObject         *udisks_object;
  UDisksBlock          *udisks_block;
  UDisksEncrypted      *udisks_encrypted;

  GListStore           *key_files_store;
  GVariant             *keyfiles;
  gulong                pim;
};

G_DEFINE_TYPE (GduUnlockDialog, gdu_unlock_dialog, ADW_TYPE_DIALOG)

static void
unlock_dialog_update_unlock_button_cb (GduUnlockDialog *self)
{
  const char *passphrase, *pim_str;
  guint n_items;
  gboolean can_unlock;

  g_assert (GDU_IS_UNLOCK_DIALOG (self));

  passphrase = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry));
  can_unlock = passphrase && *passphrase;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->key_files_store));
  can_unlock |= n_items > 0;

  pim_str = gtk_editable_get_text (GTK_EDITABLE (self->pim_entry_row));
  if (gtk_widget_is_visible (GTK_WIDGET (self->pim_entry_row)) && pim_str && *pim_str)
    {
      char *end;

      errno = 0;
      self->pim = strtoul (pim_str, &end, 10);
      if (*end || errno || self->pim == 0 || self->pim > G_MAXUINT32)
        {
          gtk_widget_add_css_class (GTK_WIDGET (self->pim_entry_row), "error");
          self->pim = 0;
          can_unlock = FALSE;
        }
      else
        {
          gtk_widget_remove_css_class (GTK_WIDGET (self->pim_entry_row), "error");
        }
    }
  else
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self->pim_entry_row), "error");
    }

  gtk_widget_set_sensitive (self->unlock_button, can_unlock);
}

static void
unlock_dialog_keyfiles_set_cb (GObject      *source_object,
                               GAsyncResult *res,
                               gpointer      user_data)
{
  GduUnlockDialog *self = GDU_UNLOCK_DIALOG (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *keyfile;
  unsigned int n_items;
  g_autoptr(GString) new_label = NULL;
  g_return_if_fail (GDU_IS_UNLOCK_DIALOG (self));
  g_return_if_fail (GTK_IS_FILE_DIALOG (dialog));

  keyfile = gtk_file_dialog_open_finish (dialog, res, NULL);
  if (!keyfile)
    return;
  g_list_store_append (self->key_files_store, keyfile);

  new_label = g_string_new (NULL);
  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->key_files_store));
  for (unsigned int i = 0; i < n_items; i++)
    {
      keyfile = g_list_model_get_item (G_LIST_MODEL (self->key_files_store), i);
      if (i > 0)
        g_string_append_c (new_label, ',');
      g_string_append (new_label, g_file_get_basename (keyfile));
    }

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->keyfile_row), new_label
                                                                   ? new_label->str
                                                                   : _("None Selected"));
}

static void
on_keyfile_chooser_clicked_cb (GduUnlockDialog *self)
{
  GtkFileDialog *file_dialog;

  file_dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (file_dialog, _("Select keyfiles to unlock this volume."));

  gtk_file_dialog_open (file_dialog,
                        GTK_WINDOW (self->parent_window),
                        NULL,
                        unlock_dialog_keyfiles_set_cb,
                        self);
}

static void
unlock_cb (GObject         *source_object,
           GAsyncResult    *result,
           gpointer         user_data)
{
  UDisksEncrypted *encrypted = UDISKS_ENCRYPTED (source_object);
  GduUnlockDialog *self = user_data;
  g_autoptr(GError) error = NULL;

  if (!udisks_encrypted_call_unlock_finish (encrypted,
                                            NULL, /* out_cleartext_device */
                                            result,
                                            &error))
    gdu_utils_show_error (GTK_WINDOW (self->parent_window), _("Error unlocking device"), error);

  g_object_unref (self);
}

static void
do_unlock (GduUnlockDialog *self)
{
  GVariantBuilder options_builder;
  const char *passphrase;
  gboolean is_hidden, is_system;

  is_hidden = gtk_switch_get_active (GTK_SWITCH (self->tcrypt_hidden_switch_row));
  is_system = gtk_switch_get_active (GTK_SWITCH (self->tcrypt_system_switch_row));

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
                                unlock_cb,
                                self);
}

static void
on_unlock_clicked_cb (GduUnlockDialog *self)
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

      for (guint i = 0; i < n_items; i++)
        {
          g_autoptr(GFile) file = NULL;
          g_autofree char *filename = NULL;

          file = g_list_model_get_item (G_LIST_MODEL (self->key_files_store), i);
          filename = g_file_get_path (file);

          g_variant_builder_add (builder, "s", filename);
        }

      if (n_items)
        self->keyfiles = g_variant_new ("as", builder);
    }
  do_unlock (g_object_ref (self));
  adw_dialog_close (ADW_DIALOG (self));
}

static void
luks_find_passphrase_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  GduUnlockDialog *self = user_data;
  const char *passphrase;

  g_assert (GDU_IS_UNLOCK_DIALOG (self));

  /* Don't fail if a keyring error occured... but if we do find a
   * passphrase then put it into the entry field and show a
   * cluebar
   */
  passphrase = secret_password_lookup_finish (result, NULL);
  if (passphrase)
    {
      adw_banner_set_revealed (ADW_BANNER (self->keyring_banner), TRUE);
      gtk_editable_set_text (GTK_EDITABLE (self->passphrase_entry), passphrase);
    }

  adw_dialog_present (ADW_DIALOG (self), self->parent_window);
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
                                               "gdu-unlock-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, unlock_button);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, passphrase_entry);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, tcrypt_hidden_switch_row);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, tcrypt_system_switch_row);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, pim_entry_row);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, keyfile_chooser);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, keyfile_row);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, keyring_banner);
  gtk_widget_class_bind_template_child (widget_class, GduUnlockDialog, veracrypt_banner);

  gtk_widget_class_bind_template_callback (widget_class, unlock_dialog_update_unlock_button_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_keyfile_chooser_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_unlock_clicked_cb);
}

static void
gdu_unlock_dialog_init (GduUnlockDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  self->key_files_store = g_list_store_new (G_TYPE_FILE);
}

void
gdu_unlock_dialog_show (GtkWindow    *parent_window,
                        UDisksObject *object)
{
  GduUnlockDialog *self;
  gboolean password_in_crypttab;
  const char *type;

  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self = g_object_new (GDU_TYPE_UNLOCK_DIALOG, NULL);
  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);
  self->udisks_encrypted = udisks_object_get_encrypted (object);

  self->parent_window = GTK_WIDGET (parent_window);

  if (gdu_utils_has_configuration (self->udisks_block, "crypttab", &password_in_crypttab) && password_in_crypttab)
    {
      do_unlock (self);
      return;
    }

  type = udisks_block_get_id_type (self->udisks_block);
  if (g_strcmp0 (type, "crypto_TCRYPT") == 0 || g_strcmp0 (type, "crypto_unknown") == 0)
    {
      gtk_widget_set_visible (GTK_WIDGET (self->keyfile_chooser), TRUE);
      adw_banner_set_revealed (ADW_BANNER (self->veracrypt_banner), g_str_equal (type, "crypto_unknown"));
    }

  g_signal_connect_swapped (self->key_files_store,
                            "notify::n-items",
                            G_CALLBACK (unlock_dialog_update_unlock_button_cb),
                            self);

  secret_password_lookup (&luks_passphrase_schema,
                          NULL, /* GCancellable */
                          luks_find_passphrase_cb,
                          self,
                          "gvfs-luks-uuid", udisks_block_get_id_uuid (self->udisks_block),
                          NULL); /* sentinel */
}
