/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2023 Purism SPC
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "gdu-encryption-options-dialog.h"

struct _GduEncryptionOptionsDialog
{
  AdwDialog      parent_instance;

  GtkWidget     *infobar;
  GtkWidget     *done_button;
  GtkWidget     *use_defaults_switch;

  GtkWidget     *name_entry;
  GtkWidget     *passphrase_entry;

  GtkWidget     *unlock_at_startup_switch;
  GtkWidget     *require_auth_switch;
  GtkWidget     *options_entry;

  GtkWidget     *passphrase_path_label;

  UDisksObject  *udisks_object;
  UDisksBlock   *udisks_block;
  UDisksDrive   *udisks_drive;
  GVariant      *crypttab_config;

  gboolean       is_self_change;
};


G_DEFINE_TYPE (GduEncryptionOptionsDialog, gdu_encryption_options_dialog, ADW_TYPE_DIALOG)

static gpointer
gdu_encryption_options_dialog_get_window (GduEncryptionOptionsDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static GVariant *
gdu_encryption_options_dialog_get_new_crypttab (GduEncryptionOptionsDialog *self)
{
  GVariantBuilder builder;
  const char *name, *options, *passphrase;
  const char *old_passphrase_path = NULL;
  g_autofree char *s;

  name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
  options = gtk_editable_get_text (GTK_EDITABLE (self->options_entry));
  passphrase = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry));

  if (self->crypttab_config != NULL)
    {
      const char *path;
      if (g_variant_lookup (self->crypttab_config, "passphrase-path", "^&ay", &path))
        if (path && *path && !g_str_has_prefix (path, "/dev"))
          old_passphrase_path = path;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  s = g_strdup_printf ("UUID=%s", udisks_block_get_id_uuid (self->udisks_block));
  g_variant_builder_add (&builder, "{sv}", "device", g_variant_new_bytestring (s));
  
  g_variant_builder_add (&builder, "{sv}", "name", g_variant_new_bytestring (name));
  g_variant_builder_add (&builder, "{sv}", "options", g_variant_new_bytestring (options));
  
  g_free (s);
  if(strlen (passphrase) > 0)
    s = old_passphrase_path ? g_strdup (old_passphrase_path) : g_strdup_printf ("/etc/luks-keys/%s", name);
  else
    s = g_strdup ("");

  g_variant_builder_add (&builder, "{sv}", "passphrase-path", g_variant_new_bytestring (s));
  g_variant_builder_add (&builder, "{sv}", "passphrase-contents", g_variant_new_bytestring (passphrase));

  return g_variant_new ("(sa{sv})", "crypttab", &builder);
}

static GVariant *
gdu_encryption_options_dialog_get_crypttab_from_config (GVariant *config)
{
  GVariantIter iter;
  GVariant *conf_dict;
  const gchar *conf_type;

  /* there could be multiple fstab entries - we only consider the first one */
  g_variant_iter_init (&iter, config);
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &conf_type, &conf_dict))
    {
      if (g_strcmp0 (conf_type, "crypttab") == 0)
        return conf_dict;

      g_variant_unref (conf_dict);
    }

  return NULL;
}

static void
on_done_clicked_cb (GduEncryptionOptionsDialog *self)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) old_crypttab = NULL;
  g_autoptr(GVariant) new_crypttab = NULL;
  gboolean use_defaults;


  use_defaults = adw_switch_row_get_active (ADW_SWITCH_ROW (self->use_defaults_switch));

  if (self->crypttab_config != NULL && use_defaults)
    {
      if (!udisks_block_call_remove_configuration_item_sync (self->udisks_block,
                                                             g_variant_new ("(s@a{sv})", "crypttab",
                                                                            self->crypttab_config),
                                                             g_variant_new ("a{sv}", NULL), /* options */
                                                             NULL, /* GCancellable */
                                                             &error))
        {
          if (!g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
            gdu_utils_show_error (gdu_encryption_options_dialog_get_window (self),
                              _("Error removing /etc/crypttab entry"),
                              error);
        }
        return;
    }
    
  new_crypttab = gdu_encryption_options_dialog_get_new_crypttab (self);
  if (self->crypttab_config == NULL && new_crypttab != NULL)
    {
      if (!udisks_block_call_add_configuration_item_sync (self->udisks_block,
                                                          new_crypttab,
                                                          g_variant_new ("a{sv}", NULL), /* options */
                                                          NULL, /* GCancellable */
                                                          &error))
        {
          if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
            gdu_utils_show_error (gdu_encryption_options_dialog_get_window (self),
                                  _("Error adding /etc/crypttab entry"),
                                  error);
        }
        return;
    }
  
  if (self->crypttab_config != NULL)
    old_crypttab = g_variant_new ("(s@a{sv})", "crypttab", self->crypttab_config);

  if (old_crypttab != NULL && new_crypttab != NULL)
    {
      if (!udisks_block_call_update_configuration_item_sync (self->udisks_block,
                                                              old_crypttab,
                                                              new_crypttab,
                                                              g_variant_new ("a{sv}", NULL), /* options */
                                                              NULL, /* GCancellable */
                                                              &error))
        {
          if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
            gdu_utils_show_error (gdu_encryption_options_dialog_get_window (self),
                                  _("Error updating /etc/crypttab entry"),
                                  error);
        }
        return;
    }
}

static void
on_property_changed_cb (GduEncryptionOptionsDialog *self)
{
  const char *new_name, *new_options, *new_passphrase_contents;
  const char *old_name, *old_options, *old_passphrase_contents;
  const char *passphrase_path;
  g_autofree char *s = NULL;
  gboolean use_defaults;
  gboolean can_ok;

  g_assert (GDU_IS_ENCRYPTION_OPTIONS_DIALOG (self));

  if (self->crypttab_config)
    {
      g_variant_lookup (self->crypttab_config, "name", "^&ay", &old_name);
      g_variant_lookup (self->crypttab_config, "options", "^&ay", &old_options);
      g_variant_lookup (self->crypttab_config, "passphrase-path", "^&ay", &passphrase_path);
      if (!g_variant_lookup (self->crypttab_config, "passphrase-contents", "^&ay", &old_passphrase_contents))
        old_passphrase_contents = "";
    }
  else
    {
      old_name = "";
      old_options = "";
      old_passphrase_contents = "";
      passphrase_path = "";
    }

  new_name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
  new_options = gtk_editable_get_text (GTK_EDITABLE (self->options_entry));
  new_passphrase_contents = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry));
  use_defaults = adw_switch_row_get_active (ADW_SWITCH_ROW (self->use_defaults_switch));

  if (self->crypttab_config == NULL)
    {
      if (new_passphrase_contents && *new_passphrase_contents)
        s = g_strdup_printf ("<i>%s</i>", _("Will be created"));
      else
        s = g_strdup_printf ("<i>%s</i>", _("None"));
    }
  else
    {
      if (g_str_has_prefix (passphrase_path, "/dev"))
        {
          /* if using a random source (for e.g. setting up swap), don't offer to edit the passphrase */
          gtk_widget_set_visible (GTK_WIDGET (self->passphrase_entry), FALSE);
          s = g_strdup (passphrase_path);
        }
      else if (new_passphrase_contents && *new_passphrase_contents)
        {
          if (!passphrase_path || !*passphrase_path)
            s = g_strdup_printf ("<i>%s</i>", _("Will be created"));
          else
            s = g_strdup (passphrase_path);
        }
      else
        {
          if (!passphrase_path || !*passphrase_path)
            s = g_strdup_printf ("<i>%s</i>", _("None"));
          else
            s = g_strdup_printf ("<i>%s</i>", _("Will be deleted"));
        }
    }
  // gtk_label_set_markup (self->passphrase_path_label, s);


  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "noauto",
                                   GTK_WIDGET (self->unlock_at_startup_switch),
                                   GTK_WIDGET (self->unlock_at_startup_switch), TRUE, FALSE);
  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "x-udisks-auth",
                                   GTK_WIDGET (self->require_auth_switch),
                                   GTK_WIDGET (self->require_auth_switch), FALSE, FALSE);

  can_ok = FALSE;
  if (self->crypttab_config == NULL && !use_defaults)
    can_ok = TRUE;
  else if (!use_defaults)
    {
      if (g_strcmp0 (new_name, old_name) != 0 ||
          g_strcmp0 (new_options, old_options) != 0 ||
          g_strcmp0 (new_passphrase_contents, old_passphrase_contents) != 0)
        {
          can_ok = TRUE;
        }
    }

  gtk_widget_set_sensitive (self->done_button, can_ok);
}

static void
gdu_encryption_options_dialog_update (GduEncryptionOptionsDialog *self)
{
  g_autofree char *name = NULL;
  const char *options, *passphrase_contents;

  g_assert (GDU_IS_ENCRYPTION_OPTIONS_DIALOG (self));

  if (self->crypttab_config != NULL)
    {
      g_variant_lookup (self->crypttab_config, "name", "^ay", &name);
      g_variant_lookup (self->crypttab_config, "options", "^&ay", &options);
      if (!g_variant_lookup (self->crypttab_config, "passphrase-contents", "^&ay", &passphrase_contents))
        passphrase_contents = "";
    }
  else
    {
      name = g_strdup_printf ("luks-%s", udisks_block_get_id_uuid (self->udisks_block));
      /* propose noauto if the media is removable - otherwise e.g. systemd will time out at boot */
      if (self->udisks_drive != NULL && udisks_drive_get_removable (self->udisks_drive))
        options = "nofail,noauto";
      else
        options = "nofail";
      passphrase_contents = "";
    }

  gtk_editable_set_text (GTK_EDITABLE (self->name_entry), name);
  gtk_editable_set_text (GTK_EDITABLE (self->options_entry), options);
  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "noauto", NULL,
                                   GTK_WIDGET (self->unlock_at_startup_switch), TRUE, FALSE);
  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "x-udisks-auth", NULL,
                                   GTK_WIDGET (self->require_auth_switch), FALSE, FALSE);
  gtk_editable_set_text (GTK_EDITABLE (self->passphrase_entry), passphrase_contents);

  on_property_changed_cb (self);
}

static void
gdu_encryption_options_dialog_finalize (GObject *object)
{
  GduEncryptionOptionsDialog *self = (GduEncryptionOptionsDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_block);
  g_clear_pointer (&self->crypttab_config, g_variant_unref);

  G_OBJECT_CLASS (gdu_encryption_options_dialog_parent_class)->finalize (object);
}

static void
gdu_encryption_options_dialog_class_init (GduEncryptionOptionsDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_encryption_options_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-encryption-options-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, infobar);
  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, done_button);
  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, use_defaults_switch);
  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, name_entry);
  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, passphrase_entry);
  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, unlock_at_startup_switch);
  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, require_auth_switch);
  gtk_widget_class_bind_template_child (widget_class, GduEncryptionOptionsDialog, options_entry);

  gtk_widget_class_bind_template_callback (widget_class, on_done_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_property_changed_cb);
}

static void
gdu_encryption_options_dialog_init (GduEncryptionOptionsDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_encryption_options_dialog_show (GtkWindow    *parent_window,
                                    UDisksClient *client,
                                    UDisksObject *object)
{
  GduEncryptionOptionsDialog *self;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) config = NULL;
  g_autoptr(UDisksObject) drive_object = NULL;

  g_return_if_fail (GTK_IS_WINDOW (parent_window));
  g_return_if_fail (UDISKS_IS_CLIENT (client));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self = g_object_new (GDU_TYPE_ENCRYPTION_OPTIONS_DIALOG, NULL);
  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);

  drive_object = (UDisksObject *)g_dbus_object_manager_get_object (udisks_client_get_object_manager (client),
                                                                   udisks_block_get_drive (self->udisks_block));
  if (drive_object)
    self->udisks_drive = udisks_object_peek_drive (drive_object);

  self->crypttab_config = gdu_encryption_options_dialog_get_crypttab_from_config (udisks_block_get_configuration (self->udisks_block));
  adw_switch_row_set_active (ADW_SWITCH_ROW (self->use_defaults_switch), self->crypttab_config == NULL);
  if (self->crypttab_config != NULL)
    {
      const char *passphrase_path;
      g_variant_lookup (self->crypttab_config, "passphrase-path", "^&ay", &passphrase_path);
      /* fetch contents of passphrase file, if it exists (unless special file)
       * and get the actual passphrase as well (involves polkit dialog)
       */
      if (passphrase_path && *passphrase_path && !g_str_has_prefix (passphrase_path, "/dev"))
        {
          if(!udisks_block_call_get_secret_configuration_sync (self->udisks_block,
                                                               g_variant_new ("a{sv}", NULL), /* Options */
                                                               &config,
                                                               NULL, /* GCancellable */
                                                               &error))
            {
              gdu_utils_show_error (gdu_encryption_options_dialog_get_window (self),
                                    _("Error retrieving configuration data"),
                                    error);
              return;
            }

          gtk_widget_set_visible (self->infobar, TRUE);
          g_clear_pointer (&self->crypttab_config, g_variant_unref);
          self->crypttab_config = gdu_encryption_options_dialog_get_crypttab_from_config (config);
        }
    }

  gdu_encryption_options_dialog_update (self);
  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
