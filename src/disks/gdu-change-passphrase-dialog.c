/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "gdu-application.h"
#include "gdu-change-passphrase-dialog.h"
#include "gdu-create-password-page.h"

struct _GduChangePassphraseDialog
{
  AdwDialog          parent_instance;

  GtkWidget         *banner;
  GtkWidget         *change_pass_button;

  GtkWidget         *curr_pass_row;
  GtkWidget         *new_pass_row;
  GtkWidget         *confirm_pass_row;
  GtkWidget         *strength_indicator;
  GtkWidget         *strength_hint_label;
  GtkWidget         *window_title;

  UDisksObject      *udisks_object;
  UDisksBlock       *udisks_block;
  UDisksClient      *udisks_client;
  UDisksEncrypted   *udisks_encrypted;

  GVariant          *crypttab_details;
  gboolean           has_passphrase_in_conf;
};


G_DEFINE_TYPE (GduChangePassphraseDialog, gdu_change_passphrase_dialog, ADW_TYPE_DIALOG)

static gpointer
gdu_change_passphrase_dialog_get_window (GduChangePassphraseDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static void
update_password_strength (GduChangePassphraseDialog *self)
{
  gint strength_level;
  const gchar *hint;
  const gchar *password;

  password = gtk_editable_get_text (GTK_EDITABLE (self->new_pass_row));

  pw_strength (password, &hint, &strength_level);

  gtk_level_bar_set_value (GTK_LEVEL_BAR (self->strength_indicator), strength_level);
  gtk_label_set_label (GTK_LABEL (self->strength_hint_label), hint);
}

static void
gdu_change_passphrase_dialog_set_title (GduChangePassphraseDialog *self)
{
  g_autoptr(UDisksObjectInfo) info = NULL;

  info = udisks_client_get_object_info (self->udisks_client, self->udisks_object);
  adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title), udisks_object_info_get_one_liner (info));
}

static void
on_dialog_entry_changed (GduChangePassphraseDialog *self)
{
  const char *curr_pass, *new_pass, *confirm_pass;
  gboolean can_proceed = FALSE;

  curr_pass = gtk_editable_get_text (GTK_EDITABLE (self->curr_pass_row));
  new_pass = gtk_editable_get_text (GTK_EDITABLE (self->new_pass_row));
  confirm_pass = gtk_editable_get_text (GTK_EDITABLE (self->confirm_pass_row));

  if (strlen (curr_pass) > 0 && strlen (new_pass) > 0 &&
      g_strcmp0 (new_pass, confirm_pass) == 0 &&
      g_strcmp0 (new_pass, curr_pass) != 0)
    can_proceed = TRUE;

  update_password_strength (self);
  gtk_widget_set_sensitive (GTK_WIDGET (self->change_pass_button), can_proceed);
}

static void
update_configuration_item_cb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  GduChangePassphraseDialog *self = user_data;
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_update_configuration_item_finish (self->udisks_block, res, &error))
    gdu_utils_show_error (gdu_change_passphrase_dialog_get_window (self),
                          _("Error updating /etc/crypttab"), error);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
change_passphrase_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  GduChangePassphraseDialog *self = user_data;
  g_autoptr(GError) error = NULL;
  GVariantBuilder builder;
  GVariantIter iter;
  const gchar *key;
  GVariant *value;

  g_assert (GDU_IS_CHANGE_PASSPHRASE_DIALOG (self));

  if (!udisks_encrypted_call_change_passphrase_finish (self->udisks_encrypted, res, &error))
    gdu_utils_show_error (gdu_change_passphrase_dialog_get_window (self),
                          _("Error changing passphrase"), error);

  if (!self->has_passphrase_in_conf)
    {
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  /* Update the system-level configuration, if applicable */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_iter_init (&iter, self->crypttab_details);
  while (g_variant_iter_next (&iter, "{sv}", &key, &value))
    {
      if (g_strcmp0 (key, "passphrase-contents") == 0)
        {
          g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                  g_variant_new_bytestring (gtk_editable_get_text (GTK_EDITABLE (self->new_pass_row))));
        }
      else
        {
          g_variant_builder_add (&builder, "{sv}", key, value);
        }
      g_variant_unref (value);
    }

  udisks_block_call_update_configuration_item (self->udisks_block,
                                                g_variant_new ("(s@a{sv})", "crypttab", self->crypttab_details),
                                                g_variant_new ("(sa{sv})", "crypttab", &builder),
                                                g_variant_new ("a{sv}", NULL), /* options */
                                                NULL, /* cancellable */
                                                update_configuration_item_cb,
                                                self);
}

static void
on_change_passphrase_clicked (GduChangePassphraseDialog *self)
{
  udisks_encrypted_call_change_passphrase (self->udisks_encrypted,
                                           gtk_editable_get_text (GTK_EDITABLE (self->curr_pass_row)),
                                           gtk_editable_get_text (GTK_EDITABLE (self->new_pass_row)),
                                           g_variant_new ("a{sv}", NULL), /* options */
                                           NULL, /* GCancellable */
                                           change_passphrase_cb,
                                           self);
}

static gboolean
has_passphrase_in_configuration (GduChangePassphraseDialog *self)
{
  GVariantIter iter;
  const gchar *type;
  GVariant *details;

  g_variant_iter_init (&iter, udisks_block_get_configuration (self->udisks_block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &type, &details))
    {
      if (g_strcmp0 (type, "crypttab") == 0)
        {
          const gchar *passphrase_path;
          if (g_variant_lookup (details, "passphrase-path", "^&ay", &passphrase_path) &&
              strlen (passphrase_path) > 0)
            {
              g_variant_unref (details);
              return TRUE;
            }
        }
      g_variant_unref (details);
    }

  return FALSE;
}

static void
on_get_secret_configuration_cb (GObject      *source_object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  GduChangePassphraseDialog *self = user_data;
  GVariantIter iter;
  const gchar *type;
  GVariant *details;
  g_autoptr(GVariant) configuration = NULL;
  GError *error;

  if (!udisks_block_call_get_secret_configuration_finish (self->udisks_block,
                                                          &configuration,
                                                          res,
                                                          &error))
    {
      gdu_utils_show_error (gdu_change_passphrase_dialog_get_window (self),
                            _("Error retrieving configuration data"),
                            error);
      
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  g_variant_iter_init (&iter, configuration);
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &type, &details))
    {
      if (g_strcmp0 (type, "crypttab") == 0)
        {
          const gchar *passphrase_contents;
          if (g_variant_lookup (details, "passphrase-contents", "^&ay", &passphrase_contents))
            {
              self->crypttab_details = g_variant_ref (details);

              gtk_editable_set_text (GTK_EDITABLE (self->curr_pass_row), passphrase_contents);
              /* Don't focus on the "Existing passphrase" entry */
              gtk_editable_select_region (GTK_EDITABLE (self->curr_pass_row), 0, 0);
              gtk_widget_grab_focus (GTK_WIDGET (self->new_pass_row));
              
              adw_dialog_present (ADW_DIALOG (self), gdu_change_passphrase_dialog_get_window (self));
              return;
            }
        }
    }

  gdu_utils_show_error (gdu_change_passphrase_dialog_get_window (self),
                        _("/etc/crypttab configuration data is malformed"),
                        NULL);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
gdu_change_passphrase_dialog_finalize (GObject *object)
{
  GduChangePassphraseDialog *self = (GduChangePassphraseDialog *)object;

  g_clear_object (&self->udisks_block);
  g_clear_object (&self->udisks_encrypted);
  g_clear_pointer (&self->crypttab_details, g_variant_unref);
  g_clear_object (&self->udisks_object);

  G_OBJECT_CLASS (gdu_change_passphrase_dialog_parent_class)->finalize (object);
}

static void
gdu_change_passphrase_dialog_class_init (GduChangePassphraseDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_change_passphrase_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-change-passphrase-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, banner);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, change_pass_button);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, curr_pass_row);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, new_pass_row);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, confirm_pass_row);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, strength_indicator);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, strength_hint_label);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, window_title);

  gtk_widget_class_bind_template_callback (widget_class, on_change_passphrase_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_dialog_entry_changed);
}

static void
gdu_change_passphrase_dialog_init (GduChangePassphraseDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_change_passphrase_dialog_show (GtkWindow    *window,
                                   UDisksObject *object,
                                   UDisksClient *client)
{
  GduChangePassphraseDialog *self;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self = g_object_new (GDU_TYPE_CHANGE_PASSPHRASE_DIALOG, NULL);

  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);
  self->udisks_client = client;
  self->udisks_encrypted = udisks_object_get_encrypted (object);
  self->has_passphrase_in_conf = has_passphrase_in_configuration (self);

  gdu_change_passphrase_dialog_set_title (self);

  if (self->has_passphrase_in_conf)
    {
      adw_banner_set_revealed (ADW_BANNER (self->banner), TRUE);

      udisks_block_call_get_secret_configuration (self->udisks_block,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* cancellable */
                                                  on_get_secret_configuration_cb,
                                                  self);
      return;
    }

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (window));
}
