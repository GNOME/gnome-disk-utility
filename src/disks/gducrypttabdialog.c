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

#include "gtk3-to-4.h"
#include "gduapplication.h"
#include "gduwindow.h"
#include "gducrypttabdialog.h"
#include "gduvolumegrid.h"

struct _GduCrypttabDialog
{
  GtkDialog             parent_instance;

  GtkBox               *infobar_box;
  GtkSwitch            *use_defaults_switch;
  GtkGrid              *main_grid;

  GtkCheckButton       *auto_unlock_check_button;
  GtkCheckButton       *require_auth_to_unlock_check_button;
  GtkEntry             *options_entry;

  GtkEntry             *name_entry;
  GtkEntry             *passphrase_entry;
  GtkLabel             *passphrase_path_label;

  GtkWidget            *warning_infobar;

  GduWindow            *window;
  UDisksObject         *udisks_object;
  UDisksBlock          *udisks_block;
  UDisksDrive          *udisks_drive;
  GVariant             *crypttab_config;

  gboolean              is_self_change;
};


G_DEFINE_TYPE (GduCrypttabDialog, gdu_crypttab_dialog, GTK_TYPE_DIALOG)

static void
crypttab_dialog_response_cb (GduCrypttabDialog *self,
                             int                response_id)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *name = NULL;
  gboolean use_modified, has_config;

  g_assert (GDU_IS_CRYPTTAB_DIALOG (self));

  if (response_id != GTK_RESPONSE_OK)
    goto end;

  has_config = (self->crypttab_config != NULL);
  use_modified = !gtk_switch_get_active (self->use_defaults_switch);

  if (has_config && !use_modified)
    {
      if (!udisks_block_call_remove_configuration_item_sync (self->udisks_block,
                                                             g_variant_new ("(s@a{sv})", "crypttab",
                                                                            self->crypttab_config),
                                                             g_variant_new ("a{sv}", NULL), /* options */
                                                             NULL, /* GCancellable */
                                                             &error))
        {
          if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
            {
              gtk_window_present (GTK_WINDOW (self));
              return;
            }
          gtk_widget_hide (GTK_WIDGET (self));
          gdu_utils_show_error (GTK_WINDOW (self->window),
                                _("Error removing /etc/crypttab entry"),
                                error);
          goto end;
        }
    }
  else
    {
      const char *new_name, *new_options, *new_passphrase_contents;
      const char *old_passphrase_path = NULL;
      GVariant *old_item = NULL;
      GVariant *new_item = NULL;
      GVariantBuilder builder;
      char *s;

      new_name = gtk_entry_get_text (self->name_entry);
      new_options = gtk_entry_get_text (self->options_entry);
      new_passphrase_contents = gtk_entry_get_text (self->passphrase_entry);

      if (self->crypttab_config)
        {
          const char *path;
          if (g_variant_lookup (self->crypttab_config, "passphrase-path", "^&ay", &path))
            {
              if (path && *path && !g_str_has_prefix (path, "/dev"))
                old_passphrase_path = path;
            }
          old_item = g_variant_new ("(s@a{sv})", "crypttab",
                                    self->crypttab_config);
        }

      g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
      s = g_strdup_printf ("UUID=%s", udisks_block_get_id_uuid (self->udisks_block));
      g_variant_builder_add (&builder, "{sv}", "device", g_variant_new_bytestring (s));
      g_free (s);
      g_variant_builder_add (&builder, "{sv}", "name", g_variant_new_bytestring (new_name));
      g_variant_builder_add (&builder, "{sv}", "options", g_variant_new_bytestring (new_options));
      if (strlen (new_passphrase_contents) > 0)
        {
          /* use old/existing passphrase file, if available */
          if (old_passphrase_path)
            {
              g_variant_builder_add (&builder, "{sv}", "passphrase-path",
                                     g_variant_new_bytestring (old_passphrase_path));
            }
          else
            {
              /* otherwise fall back to the requested name */
              s = g_strdup_printf ("/etc/luks-keys/%s", new_name);
              g_variant_builder_add (&builder, "{sv}", "passphrase-path", g_variant_new_bytestring (s));
              g_free (s);
            }
          g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                 g_variant_new_bytestring (new_passphrase_contents));
        }
      else
        {
          g_variant_builder_add (&builder, "{sv}", "passphrase-path",
                                 g_variant_new_bytestring (""));
          g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                 g_variant_new_bytestring (""));
        }
      new_item = g_variant_new ("(sa{sv})", "crypttab", &builder);

      if (!old_item && new_item)
        {
          if (!udisks_block_call_add_configuration_item_sync (self->udisks_block,
                                                              new_item,
                                                              g_variant_new ("a{sv}", NULL), /* options */
                                                              NULL, /* GCancellable */
                                                              &error))
            {
              if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
                {
                  gtk_window_present (GTK_WINDOW (self));
                  return;
                }
              gtk_widget_hide (GTK_WIDGET (self));
              gdu_utils_show_error (GTK_WINDOW (self->window),
                                    _("Error adding /etc/crypttab entry"),
                                    error);
              goto end;
            }
        }
      else if (old_item != NULL && new_item != NULL)
        {
          if (!udisks_block_call_update_configuration_item_sync (self->udisks_block,
                                                                 old_item,
                                                                 new_item,
                                                                 g_variant_new ("a{sv}", NULL), /* options */
                                                                 NULL, /* GCancellable */
                                                                 &error))
            {
              if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
                {
                  gtk_window_present (GTK_WINDOW (self));
                  return;
                }
              gtk_widget_hide (GTK_WIDGET (self));
              gdu_utils_show_error (GTK_WINDOW (self),
                                    _("Error updating /etc/crypttab entry"),
                                    error);
              goto end;
            }
        }
      else
        {
          g_assert_not_reached ();
        }
    }

 end:
  gtk_widget_hide (GTK_WIDGET (self));
  gtk_widget_destroy (GTK_WIDGET (self));
}

static void
crypttab_dialog_property_changed_cb (GduCrypttabDialog *self)
{
  const char *new_name, *new_options, *new_passphrase_contents;
  const char *old_name, *old_options, *old_passphrase_contents;
  const char *passphrase_path;
  g_autofree char *s = NULL;
  gboolean has_conf, use_modified;
  gboolean can_ok;

  g_assert (GDU_IS_CRYPTTAB_DIALOG (self));

  if (self->is_self_change)
    return;

  if (self->crypttab_config)
    {
      has_conf = TRUE;
      g_variant_lookup (self->crypttab_config, "name", "^&ay", &old_name);
      g_variant_lookup (self->crypttab_config, "options", "^&ay", &old_options);
      g_variant_lookup (self->crypttab_config, "passphrase-path", "^&ay", &passphrase_path);
      if (!g_variant_lookup (self->crypttab_config, "passphrase-contents", "^&ay", &old_passphrase_contents))
        old_passphrase_contents = "";
    }
  else
    {
      has_conf = FALSE;
      old_name = "";
      old_options = "";
      old_passphrase_contents = "";
      passphrase_path = "";
    }

  new_name = gtk_entry_get_text (self->name_entry);
  new_options = gtk_entry_get_text (self->options_entry);
  new_passphrase_contents = gtk_entry_get_text (self->passphrase_entry);
  use_modified = !gtk_switch_get_active (self->use_defaults_switch);

  if (!has_conf)
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
          gtk_widget_hide (GTK_WIDGET (self->passphrase_entry));
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

  gtk_label_set_markup (self->passphrase_path_label, s);

  self->is_self_change = TRUE;
  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "noauto",
                                   GTK_WIDGET (self->auto_unlock_check_button),
                                   GTK_WIDGET (self->auto_unlock_check_button), TRUE, FALSE);
  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "x-udisks-auth",
                                   GTK_WIDGET (self->require_auth_to_unlock_check_button),
                                   GTK_WIDGET (self->require_auth_to_unlock_check_button), FALSE, FALSE);
  self->is_self_change = FALSE;

  can_ok = FALSE;
  if (has_conf != use_modified)
    {
      can_ok = TRUE;
    }
  else if (use_modified)
    {
      if (g_strcmp0 (new_name, old_name) != 0 ||
          g_strcmp0 (new_options, old_options) != 0 ||
          g_strcmp0 (new_passphrase_contents, old_passphrase_contents) != 0)
        {
          can_ok = TRUE;
        }
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (self), GTK_RESPONSE_OK, can_ok);
  gtk_widget_set_sensitive (GTK_WIDGET (self->main_grid), use_modified);
}

static void
gdu_crypttab_dialog_finalize (GObject *object)
{
  GduCrypttabDialog *self = (GduCrypttabDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_block);
  g_clear_pointer (&self->crypttab_config, g_variant_unref);

  G_OBJECT_CLASS (gdu_crypttab_dialog_parent_class)->finalize (object);
}

static void
gdu_crypttab_dialog_class_init (GduCrypttabDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_crypttab_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Disks/ui/"
                                               "edit-crypttab-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, infobar_box);
  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, use_defaults_switch);
  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, main_grid);

  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, auto_unlock_check_button);
  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, require_auth_to_unlock_check_button);
  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, options_entry);

  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, name_entry);
  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, passphrase_entry);
  gtk_widget_class_bind_template_child (widget_class, GduCrypttabDialog, passphrase_path_label);

  gtk_widget_class_bind_template_callback (widget_class, crypttab_dialog_response_cb);
  gtk_widget_class_bind_template_callback (widget_class, crypttab_dialog_property_changed_cb);
}

static void
gdu_crypttab_dialog_init (GduCrypttabDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->warning_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO,
                                                     _("Only the passphrase referenced by the <i>/etc/crypttab</i> "
                                                       "file will be changed. To change the on-disk passphrase, use "
                                                       "<i>Change Passphrase…</i>"),
                                                     NULL);
  /* gtk_box_pack_start (self->infobar_box, self->warning_infobar, TRUE, TRUE, 0); */
}

static void
crypttab_dialog_update (GduCrypttabDialog *self)
{
  const char *options, *passphrase_contents;
  g_autofree char *name = NULL;
  gboolean has_config = FALSE;

  g_assert (GDU_IS_CRYPTTAB_DIALOG (self));

  if (self->crypttab_config)
    {
      has_config = TRUE;
      g_variant_lookup (self->crypttab_config, "name", "^ay", &name);
      g_variant_lookup (self->crypttab_config, "options", "^&ay", &options);
      if (!g_variant_lookup (self->crypttab_config, "passphrase-contents", "^&ay", &passphrase_contents))
        passphrase_contents = "";
    }
  else
    {
      has_config = FALSE;
      name = g_strdup_printf ("luks-%s", udisks_block_get_id_uuid (self->udisks_block));
      options = "nofail";
      /* propose noauto if the media is removable - otherwise e.g. systemd will time out at boot */
      if (self->udisks_drive != NULL && udisks_drive_get_removable (self->udisks_drive))
        options = "nofail,noauto";
      passphrase_contents = "";
    }

  self->is_self_change = TRUE;
  gtk_entry_set_text (self->name_entry, name);
  gtk_entry_set_text (self->options_entry, options);
  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "noauto", NULL,
                                   GTK_WIDGET (self->auto_unlock_check_button), TRUE, FALSE);
  gdu_options_update_check_option (GTK_WIDGET (self->options_entry), "x-udisks-auth", NULL,
                                   GTK_WIDGET (self->require_auth_to_unlock_check_button), FALSE, FALSE);
  gtk_entry_set_text (self->passphrase_entry, passphrase_contents);
  gtk_switch_set_active (self->use_defaults_switch, !has_config);
  self->is_self_change = FALSE;

  crypttab_dialog_property_changed_cb (self);
}

static void
crypttab_dialog_on_get_secrets_cb (UDisksBlock  *block,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  GduCrypttabDialog *self = user_data;
  g_autoptr(GVariant) config_dict = NULL;
  g_autoptr(GVariant) config = NULL;
  g_autoptr(GError) error = NULL;
  GVariantIter iter;
  const char *config_type;

  if (!udisks_block_call_get_secret_configuration_finish (block, &config, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (self->window),
                            _("Error retrieving configuration data"),
                            error);
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_widget_destroy (GTK_WIDGET (self));
      return;
    }

  /* there could be multiple crypttab entries - we only consider the first one */
  g_variant_iter_init (&iter, config);
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &config_type, &config_dict))
    {
      if (g_strcmp0 (config_type, "crypttab") == 0)
        {
          g_clear_pointer (&self->crypttab_config, g_variant_unref);
          self->crypttab_config = g_steal_pointer (&config_dict);
          break;
        }
    }

  gtk_widget_show (self->warning_infobar);

  crypttab_dialog_update (self);
  gtk_window_present (GTK_WINDOW (self));
}

void
gdu_crypttab_dialog_show (GduWindow    *window,
                          UDisksObject *object)
{
  GduCrypttabDialog *self;
  g_autoptr(UDisksObject) drive = NULL;
  g_autoptr(GVariant) config = NULL;
  g_autofree char *name = NULL;
  GVariantIter iter;
  const char *conf_type;
  gboolean has_conf = FALSE;
  gboolean get_passphrase_contents = FALSE;

  g_return_if_fail (GDU_IS_WINDOW (window));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self = g_object_new (GDU_TYPE_CRYPTTAB_DIALOG,
                       "transient-for", window,
                       NULL);
  self->window = window;
  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);

  drive = (UDisksObject *)g_dbus_object_manager_get_object (udisks_client_get_object_manager (gdu_window_get_client (window)),
                                                            udisks_block_get_drive (self->udisks_block));
  if (drive)
    self->udisks_drive = udisks_object_peek_drive (drive);

  /* First check if there's an existing configuration */
  g_variant_iter_init (&iter, udisks_block_get_configuration (self->udisks_block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &conf_type, &config))
    {
      if (g_strcmp0 (conf_type, "crypttab") == 0)
        {
          const char *passphrase_path;
          has_conf = TRUE;
          g_variant_lookup (config, "passphrase-path", "^&ay", &passphrase_path);
          /* fetch contents of passphrase file, if it exists (unless special file) */
          if (passphrase_path && *passphrase_path && !g_str_has_prefix (passphrase_path, "/dev"))
            get_passphrase_contents = TRUE;
          self->crypttab_config = g_steal_pointer (&config);
          break;
        }
    }

  /* if there is an existing configuration and it has a passphrase, get the actual passphrase
   * as well (involves polkit dialog)
   */
  if (has_conf && get_passphrase_contents)
    {
      udisks_block_call_get_secret_configuration (self->udisks_block,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* cancellable */
                                                  (GAsyncReadyCallback) crypttab_dialog_on_get_secrets_cb,
                                                  self);
    }
  else
    {
      /* otherwise just set up the dialog */
      crypttab_dialog_update (self);
      gtk_window_present (GTK_WINDOW (self));
    }
}
