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

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduchangepassphrasedialog.h"
#include "gdupasswordstrengthwidget.h"

struct _GduChangePassphraseDialog
{
  GtkDialog          parent_instance;

  GtkBox            *infobar_box;
  GtkEntry          *existing_passphrase_entry;
  GtkEntry          *new_passphrase_entry;
  GtkEntry          *confirm_passphrase_entry;
  GtkWidget         *passphrase_strength_widget;

  GduWindow         *window;
  UDisksObject      *udisks_object;
  UDisksBlock       *udisks_block;
  UDisksEncrypted   *udisks_encrypted;

  GVariant          *crypttab_details;
  gboolean           has_passphrase_in_conf;
};


G_DEFINE_TYPE (GduChangePassphraseDialog, gdu_change_passphrase_dialog, GTK_TYPE_DIALOG)

static void
dialog_passhphrase_changed_cb (GduChangePassphraseDialog *self)
{
  const char *existing_passphrase, *new_passphrase, *confirm_passphrase;
  gboolean can_proceed = FALSE;

  g_assert (GDU_IS_CHANGE_PASSPHRASE_DIALOG (self));

  existing_passphrase = gtk_entry_get_text (self->existing_passphrase_entry);
  new_passphrase = gtk_entry_get_text (self->new_passphrase_entry);
  confirm_passphrase = gtk_entry_get_text (self->confirm_passphrase_entry);

  gtk_entry_set_icon_from_icon_name (self->confirm_passphrase_entry,
                                     GTK_ENTRY_ICON_SECONDARY,
                                     NULL);
  gtk_entry_set_icon_tooltip_text (self->confirm_passphrase_entry,
                                   GTK_ENTRY_ICON_SECONDARY,
                                   NULL);
  gtk_entry_set_icon_from_icon_name (self->new_passphrase_entry,
                                     GTK_ENTRY_ICON_SECONDARY,
                                     NULL);
  gtk_entry_set_icon_tooltip_text (self->new_passphrase_entry,
                                   GTK_ENTRY_ICON_SECONDARY,
                                   NULL);

  gdu_password_strength_widget_set_password (GDU_PASSWORD_STRENGTH_WIDGET (self->passphrase_strength_widget),
                                             new_passphrase);

  if (strlen (new_passphrase) > 0 && strlen (confirm_passphrase) > 0 && g_strcmp0 (new_passphrase, confirm_passphrase) != 0)
    {
      gtk_entry_set_icon_from_icon_name (self->confirm_passphrase_entry,
                                         GTK_ENTRY_ICON_SECONDARY,
                                         "dialog-warning-symbolic");
      gtk_entry_set_icon_tooltip_text (self->confirm_passphrase_entry,
                                       GTK_ENTRY_ICON_SECONDARY,
                                       _("The passphrases do not match"));
    }
  if (strlen (existing_passphrase) > 0 && strlen (new_passphrase) > 0 && g_strcmp0 (new_passphrase, existing_passphrase) == 0)
    {
      gtk_entry_set_icon_from_icon_name (self->new_passphrase_entry,
                                         GTK_ENTRY_ICON_SECONDARY,
                                         "dialog-warning-symbolic");
      gtk_entry_set_icon_tooltip_text (self->new_passphrase_entry,
                                       GTK_ENTRY_ICON_SECONDARY,
                                       _("The passphrase matches the existing passphrase"));
    }

  if (strlen (existing_passphrase) > 0 && strlen (new_passphrase) > 0 &&
      g_strcmp0 (new_passphrase, confirm_passphrase) == 0 &&
      g_strcmp0 (new_passphrase, existing_passphrase) != 0)
    can_proceed = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (self), GTK_RESPONSE_OK, can_proceed);
}

static void
update_configuration_item_cb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  GduChangePassphraseDialog *self = user_data;
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_update_configuration_item_finish (self->udisks_block, res, &error))
    gdu_utils_show_error (GTK_WINDOW (self->window), _("Error updating /etc/crypttab"), error);

  gtk_widget_hide (GTK_WIDGET (self));
  gtk_widget_destroy (GTK_WIDGET (self));
}

static void
change_passphrase_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  GduChangePassphraseDialog *self = user_data;
  g_autoptr(GError) error = NULL;

  if (!udisks_encrypted_call_change_passphrase_finish (self->udisks_encrypted, res, &error))
    gdu_utils_show_error (GTK_WINDOW (self->window), _("Error changing passphrase"), error);

  /* Update the system-level configuration, if applicable */
  if (self->has_passphrase_in_conf)
    {
      GVariantBuilder builder;
      GVariantIter iter;
      const gchar *key;
      GVariant *value;

      g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
      g_variant_iter_init (&iter, self->crypttab_details);
      while (g_variant_iter_next (&iter, "{sv}", &key, &value))
        {
          if (g_strcmp0 (key, "passphrase-contents") == 0)
            {
              g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                     g_variant_new_bytestring (gtk_entry_get_text (self->new_passphrase_entry)));
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
  else
    {
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_widget_destroy (GTK_WIDGET (self));
    }
}

static void
change_passphrase_dialog_response_cb (GduChangePassphraseDialog *self,
                                      int                        response_id)
{
  g_assert (GDU_IS_CHANGE_PASSPHRASE_DIALOG (self));

  gtk_widget_hide (GTK_WIDGET (self));

  if (response_id == GTK_RESPONSE_OK)
    {
      udisks_encrypted_call_change_passphrase (self->udisks_encrypted,
                                               gtk_entry_get_text (self->existing_passphrase_entry),
                                               gtk_entry_get_text (self->new_passphrase_entry),
                                               g_variant_new ("a{sv}", NULL), /* options */
                                               NULL, /* GCancellable */
                                               change_passphrase_cb,
                                               self);
    }
  else
    {
      gtk_widget_destroy (GTK_WIDGET (self));
    }

}

static void
gdu_change_passphrase_dialog_finalize (GObject *object)
{
  GduChangePassphraseDialog *self = (GduChangePassphraseDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_block);
  g_clear_object (&self->udisks_encrypted);
  g_clear_pointer (&self->crypttab_details, g_variant_unref);

  G_OBJECT_CLASS (gdu_change_passphrase_dialog_parent_class)->finalize (object);
}

static void
gdu_change_passphrase_dialog_class_init (GduChangePassphraseDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_change_passphrase_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Disks/ui/"
                                               "change-passphrase-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, infobar_box);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, existing_passphrase_entry);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, new_passphrase_entry);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, confirm_passphrase_entry);
  gtk_widget_class_bind_template_child (widget_class, GduChangePassphraseDialog, passphrase_strength_widget);

  gtk_widget_class_bind_template_callback (widget_class, change_passphrase_dialog_response_cb);
  gtk_widget_class_bind_template_callback (widget_class, dialog_passhphrase_changed_cb);
}

static void
gdu_change_passphrase_dialog_init (GduChangePassphraseDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  dialog_passhphrase_changed_cb (self);
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
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_get_secret_configuration_finish (self->udisks_block,
                                                          &configuration,
                                                          res,
                                                          &error))
    {
      gdu_utils_show_error (GTK_WINDOW (self->window),
                            _("Error retrieving configuration data"),
                            error);
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_widget_destroy (GTK_WIDGET (self));
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

              gtk_entry_set_text (self->existing_passphrase_entry, passphrase_contents);
              /* Don't focus on the "Existing passphrase" entry */
              gtk_editable_select_region (GTK_EDITABLE (self->existing_passphrase_entry), 0, 0);
              gtk_widget_grab_focus (GTK_WIDGET (self->new_passphrase_entry));
              gtk_window_present (GTK_WINDOW (self));

              return;
            }
        }
    }

  gdu_utils_show_error (GTK_WINDOW (self->window), _("/etc/crypttab configuration data is malformed"), NULL);
  gtk_widget_hide (GTK_WIDGET (self));
  gtk_widget_destroy (GTK_WIDGET (self));
}

void
gdu_change_passphrase_dialog_show (GduWindow    *window,
                                   UDisksObject *object)
{
  GduChangePassphraseDialog *self;

  g_return_if_fail (GDU_IS_WINDOW (window));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self = g_object_new (GDU_TYPE_CHANGE_PASSPHRASE_DIALOG,
                       "transient-for", window,
                       NULL);

  self->window = window;
  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);
  self->udisks_encrypted = udisks_object_get_encrypted (object);
  self->has_passphrase_in_conf = has_passphrase_in_configuration (self);

  if (self->has_passphrase_in_conf)
    {
      GtkWidget *infobar;

      infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO,
                                           _("Changing the passphrase for this device, will also update "
                                             "the passphrase referenced by the <i>/etc/crypttab</i> file"),
                                           NULL);
      gtk_widget_show (infobar);
      gtk_box_pack_start (self->infobar_box, infobar, TRUE, TRUE, 0);

      udisks_block_call_get_secret_configuration (self->udisks_block,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* cancellable */
                                                  on_get_secret_configuration_cb,
                                                  self);
    }
  else
    {
      gtk_window_present (GTK_WINDOW (self));
    }
}
