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

#include "gduapplication.h"
#include "gduwindow.h"
#include "gducrypttabdialog.h"
#include "gduvolumegrid.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksDrive *drive;
  UDisksBlock *block;

  GduWindow *window;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *infobar_vbox;
  GtkWidget *passphrase_warning_infobar;

  GtkWidget *automatic_crypt_options_switch;
  GtkWidget *grid;

  GtkWidget *name_entry;
  GtkWidget *options_entry;
  GtkWidget *neg_noauto_checkbutton;
  GtkWidget *auth_checkbutton;
  GtkWidget *passphrase_label;
  GtkWidget *passphrase_entry;
  GtkWidget *show_passphrase_checkbutton;
  GtkWidget *passphrase_path_value_label;

  GVariant *orig_crypttab_entry;
} CrypttabDialogData;

static void
crypttab_dialog_free (CrypttabDialogData *data)
{
  g_object_unref (data->window);

  if (data->orig_crypttab_entry != NULL)
    g_variant_unref (data->orig_crypttab_entry);

  if (data->dialog != NULL)
    {
      gtk_widget_hide (data->dialog);
      gtk_widget_destroy (data->dialog);
    }
  g_object_unref (data->builder);
  g_free (data);
}

static void
update (CrypttabDialogData *data,
        GtkWidget          *widget)
{
  gboolean ui_configured;
  gboolean configured;
  const gchar *ui_name;
  const gchar *ui_options;
  const gchar *ui_passphrase_contents;
  const gchar *name;
  const gchar *passphrase_path;
  const gchar *passphrase_contents;
  const gchar *options;
  gboolean can_ok;
  gchar *s;

  if (data->orig_crypttab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data->orig_crypttab_entry, "name", "^&ay", &name);
      g_variant_lookup (data->orig_crypttab_entry, "options", "^&ay", &options);
      g_variant_lookup (data->orig_crypttab_entry, "passphrase-path", "^&ay", &passphrase_path);
      if (!g_variant_lookup (data->orig_crypttab_entry, "passphrase-contents", "^&ay", &passphrase_contents))
        passphrase_contents = "";
    }
  else
    {
      configured = FALSE;
      name = "";
      options = "";
      passphrase_path = "";
      passphrase_contents = "";
    }

  ui_name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
  ui_options = gtk_entry_get_text (GTK_ENTRY (data->options_entry));
  ui_passphrase_contents = gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry));
  ui_configured = !gtk_switch_get_active (GTK_SWITCH (data->automatic_crypt_options_switch));

  if (!configured)
    {
      if (strlen (ui_passphrase_contents) > 0)
        s = g_strdup_printf ("<i>%s</i>", _("Will be created"));
      else
        s = g_strdup_printf ("<i>%s</i>", _("None"));
    }
  else
    {
      if (g_str_has_prefix (passphrase_path, "/dev"))
        {
          /* if using a random source (for e.g. setting up swap), don't offer to edit the passphrase */
          gtk_widget_hide (data->passphrase_label);
          gtk_widget_hide (data->passphrase_entry);
          gtk_widget_hide (data->show_passphrase_checkbutton);
          gtk_widget_set_no_show_all (data->passphrase_label, TRUE);
          gtk_widget_set_no_show_all (data->passphrase_entry, TRUE);
          gtk_widget_set_no_show_all (data->show_passphrase_checkbutton, TRUE);
          s = g_strdup (passphrase_path);
        }
      else if (strlen (ui_passphrase_contents) > 0)
        {
          if (strlen (passphrase_path) == 0)
            s = g_strdup_printf ("<i>%s</i>", _("Will be created"));
          else
            s = g_strdup (passphrase_path);
        }
      else
        {
          if (strlen (passphrase_path) == 0)
            s = g_strdup_printf ("<i>%s</i>", _("None"));
          else
            s = g_strdup_printf ("<i>%s</i>", _("Will be deleted"));
        }
    }
  gtk_label_set_markup (GTK_LABEL (data->passphrase_path_value_label), s);
  g_free (s);

  g_object_freeze_notify (G_OBJECT (data->options_entry));
  gdu_options_update_check_option (data->options_entry, "noauto", widget, data->neg_noauto_checkbutton, TRUE, FALSE);
  gdu_options_update_check_option (data->options_entry, "x-udisks-auth", widget, data->auth_checkbutton, FALSE, FALSE);
  g_object_thaw_notify (G_OBJECT (data->options_entry));

  can_ok = FALSE;
  if (configured != ui_configured)
    {
      can_ok = TRUE;
    }
  else if (ui_configured)
    {
      if (g_strcmp0 (ui_name, name) != 0 ||
          g_strcmp0 (ui_options, options) != 0 ||
          g_strcmp0 (ui_passphrase_contents, passphrase_contents) != 0)
        {
          can_ok = TRUE;
        }
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     can_ok);

  gtk_widget_set_sensitive (data->grid, ui_configured);
}

static void
on_property_changed (GObject     *object,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  CrypttabDialogData *data = user_data;
  update (data, GTK_WIDGET (object));
}


static void
crypttab_dialog_present (CrypttabDialogData *data)
{
  gboolean configured;
  gchar *name;
  const gchar *options;
  const gchar *passphrase_contents;
  gint response;

  if (data->orig_crypttab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data->orig_crypttab_entry, "name", "^ay", &name);
      g_variant_lookup (data->orig_crypttab_entry, "options", "^&ay", &options);
      if (!g_variant_lookup (data->orig_crypttab_entry, "passphrase-contents", "^&ay", &passphrase_contents))
        passphrase_contents = "";
    }
  else
    {
      configured = FALSE;
      name = g_strdup_printf ("luks-%s", udisks_block_get_id_uuid (data->block));
      options = "nofail";
      /* propose noauto if the media is removable - otherwise e.g. systemd will time out at boot */
      if (data->drive != NULL && udisks_drive_get_removable (data->drive))
        options = "nofail,noauto";
      passphrase_contents = "";
    }
  gtk_entry_set_text (GTK_ENTRY (data->name_entry), name);
  gtk_entry_set_text (GTK_ENTRY (data->options_entry), options);
  gtk_entry_set_text (GTK_ENTRY (data->passphrase_entry), passphrase_contents);

  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);

  g_signal_connect (data->automatic_crypt_options_switch,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->name_entry,
                    "notify::text", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->options_entry,
                    "notify::text", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->neg_noauto_checkbutton,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->auth_checkbutton,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->passphrase_entry,
                    "notify::text", G_CALLBACK (on_property_changed), data);

  gtk_switch_set_active (GTK_SWITCH (data->automatic_crypt_options_switch), !configured);
  gtk_widget_show_all (data->dialog);

  update (data, NULL);

 again:
  response = gtk_dialog_run (GTK_DIALOG (data->dialog));

  if (response == GTK_RESPONSE_OK)
    {
      gboolean ui_configured;
      GError *error;

      configured = (data->orig_crypttab_entry != NULL);
      ui_configured = !gtk_switch_get_active (GTK_SWITCH (data->automatic_crypt_options_switch));

      if (configured && !ui_configured)
        {
          error = NULL;
          if (!udisks_block_call_remove_configuration_item_sync (data->block,
                                                                 g_variant_new ("(s@a{sv})", "crypttab",
                                                                                data->orig_crypttab_entry),
                                                                 g_variant_new ("a{sv}", NULL), /* options */
                                                                 NULL, /* GCancellable */
                                                                 &error))
            {
              if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
                {
                  g_error_free (error);
                  goto again;
                }
              gtk_widget_hide (data->dialog);
              gdu_utils_show_error (GTK_WINDOW (data->window),
                                     _("Error removing /etc/crypttab entry"),
                                     error);
              g_error_free (error);
              goto out;
            }
        }
      else
        {
          const gchar *ui_name;
          const gchar *ui_options;
          const gchar *ui_passphrase_contents;
          const gchar *old_passphrase_path;
          GVariant *old_item = NULL;
          GVariant *new_item = NULL;
          GVariantBuilder builder;
          gchar *s;

          ui_name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
          ui_options = gtk_entry_get_text (GTK_ENTRY (data->options_entry));
          ui_passphrase_contents = gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry));

          old_passphrase_path = NULL;
          if (data->orig_crypttab_entry != NULL)
            {
              const gchar *path;
              if (g_variant_lookup (data->orig_crypttab_entry, "passphrase-path", "^&ay", &path))
                {
                  if (strlen (path) > 0 && !g_str_has_prefix (path, "/dev"))
                    old_passphrase_path = path;
                }
              error = NULL;
              old_item = g_variant_new ("(s@a{sv})", "crypttab",
                                        data->orig_crypttab_entry);
            }

          g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
          s = g_strdup_printf ("UUID=%s", udisks_block_get_id_uuid (data->block));
          g_variant_builder_add (&builder, "{sv}", "device", g_variant_new_bytestring (s));
          g_free (s);
          g_variant_builder_add (&builder, "{sv}", "name", g_variant_new_bytestring (ui_name));
          g_variant_builder_add (&builder, "{sv}", "options", g_variant_new_bytestring (ui_options));
          if (strlen (ui_passphrase_contents) > 0)
            {
              /* use old/existing passphrase file, if available */
              if (old_passphrase_path != NULL)
                {
                  g_variant_builder_add (&builder, "{sv}", "passphrase-path",
                                         g_variant_new_bytestring (old_passphrase_path));
                }
              else
                {
                  /* otherwise fall back to the requested name */
                  s = g_strdup_printf ("/etc/luks-keys/%s", ui_name);
                  g_variant_builder_add (&builder, "{sv}", "passphrase-path", g_variant_new_bytestring (s));
                  g_free (s);
                }
              g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                     g_variant_new_bytestring (ui_passphrase_contents));
            }
          else
            {
              g_variant_builder_add (&builder, "{sv}", "passphrase-path",
                                     g_variant_new_bytestring (""));
              g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                     g_variant_new_bytestring (""));
            }
          new_item = g_variant_new ("(sa{sv})", "crypttab", &builder);

          if (old_item == NULL && new_item != NULL)
            {
              error = NULL;
              if (!udisks_block_call_add_configuration_item_sync (data->block,
                                                                  new_item,
                                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                                  NULL, /* GCancellable */
                                                                  &error))
                {
                  if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
                    {
                      g_error_free (error);
                      goto again;
                    }
                  gtk_widget_hide (data->dialog);
                  gdu_utils_show_error (GTK_WINDOW (data->window),
                                        _("Error adding /etc/crypttab entry"),
                                        error);
                  g_error_free (error);
                  goto out;
                }
            }
          else if (old_item != NULL && new_item != NULL)
            {
              error = NULL;
              if (!udisks_block_call_update_configuration_item_sync (data->block,
                                                                     old_item,
                                                                     new_item,
                                                                     g_variant_new ("a{sv}", NULL), /* options */
                                                                     NULL, /* GCancellable */
                                                                     &error))
                {
                  if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
                    {
                      g_error_free (error);
                      goto again;
                    }
                  gtk_widget_hide (data->dialog);
                  gdu_utils_show_error (GTK_WINDOW (data->window),
                                        _("Error updating /etc/crypttab entry"),
                                        error);
                  g_error_free (error);
                  goto out;
                }
            }
          else
            {
              g_assert_not_reached ();
            }
        }
    }

 out:
  crypttab_dialog_free (data);
  g_free (name);
}

static void
crypttab_dialog_on_get_secrets_cb (UDisksBlock       *block,
                                   GAsyncResult      *res,
                                   gpointer           user_data)
{
  CrypttabDialogData *data = user_data;
  GError *error;
  GVariant *configuration;
  GVariantIter iter;
  const gchar *configuration_type;
  GVariant *configuration_dict;

  configuration = NULL;
  error = NULL;
  if (!udisks_block_call_get_secret_configuration_finish (block,
                                                          &configuration,
                                                          res,
                                                          &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error retrieving configuration data"),
                            error);
      g_error_free (error);
      crypttab_dialog_free (data);
      goto out;
    }

  /* there could be multiple crypttab entries - we only consider the first one */
  g_variant_iter_init (&iter, configuration);
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &configuration_type, &configuration_dict))
    {
      if (g_strcmp0 (configuration_type, "crypttab") == 0)
        {
          if (data->orig_crypttab_entry != NULL)
            g_variant_unref (data->orig_crypttab_entry);
          data->orig_crypttab_entry = configuration_dict;
          break;
        }
      else
        {
          g_variant_unref (configuration_dict);
        }
    }
  g_variant_unref (configuration);

  /* Do show the warning "Passphrase isn't changed on-disk" warning */
  gtk_widget_set_no_show_all (data->passphrase_warning_infobar, FALSE);
  gtk_widget_show (data->passphrase_warning_infobar);

  crypttab_dialog_present (data);

 out:
  ;
}

void
gdu_crypttab_dialog_show (GduWindow    *window,
                          UDisksObject *object)
{
  UDisksObject *drive_object;
  GtkWidget *dialog;
  CrypttabDialogData *data;
  GVariantIter iter;
  const gchar *configuration_type;
  GVariant *configuration_dict;
  gboolean configured;
  gboolean get_passphrase_contents;

  data = g_new0 (CrypttabDialogData, 1);
  data->window = g_object_ref (window);

  data->block = udisks_object_peek_block (object);
  g_assert (data->block != NULL);

  data->drive = NULL;
  drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (gdu_window_get_client (window)),
                                                                    udisks_block_get_drive (data->block));
  if (drive_object != NULL)
    {
      data->drive = udisks_object_peek_drive (drive_object);
      g_object_unref (drive_object);
    }

  dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                   "edit-crypttab-dialog.ui",
                                                   "crypttab-dialog",
                                                   &data->builder));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  data->dialog = dialog;
  data->infobar_vbox = GTK_WIDGET (gtk_builder_get_object (data->builder, "infobar-vbox"));
  data->automatic_crypt_options_switch = GTK_WIDGET (gtk_builder_get_object (data->builder, "automatic-crypt-options-switch"));
  data->grid = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-grid"));
  data->name_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-name-entry"));
  data->options_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-options-entry"));
  data->neg_noauto_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-neg-noauto-checkbutton"));
  data->auth_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-auth-checkbutton"));
  data->passphrase_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-passphrase-label"));
  data->passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-passphrase-entry"));
  data->show_passphrase_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-show-passphrase-checkbutton"));
  data->passphrase_path_value_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-passphrase-path-value-label"));

  /* do infobar stuff manually because of glade-hate !@#$ :-/ */
  data->passphrase_warning_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO,
                                                                _("Only the passphrase referenced by the <i>/etc/crypttab</i> file will be changed. To change the on-disk passphrase, use <i>Change Passphrase…</i>"),
                                                                NULL);
  /* don't show by default (see crypttab_dialog_on_get_secrets_cb()) */
  gtk_widget_set_no_show_all (data->passphrase_warning_infobar, TRUE);
  gtk_box_pack_start (GTK_BOX (data->infobar_vbox), data->passphrase_warning_infobar, TRUE, TRUE, 0);

  /* First check if there's an existing configuration */
  configured = FALSE;
  get_passphrase_contents = FALSE;
  g_variant_iter_init (&iter, udisks_block_get_configuration (data->block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &configuration_type, &configuration_dict))
    {
      if (g_strcmp0 (configuration_type, "crypttab") == 0)
        {
          const gchar *passphrase_path;
          configured = TRUE;
          g_variant_lookup (configuration_dict, "passphrase-path", "^&ay", &passphrase_path);
          if (g_strcmp0 (passphrase_path, "") != 0)
            {
              /* fetch contents of passphrase file, if it exists (unless special file) */
              if (!g_str_has_prefix (passphrase_path, "/dev"))
                {
                  get_passphrase_contents = TRUE;
                }
            }
          data->orig_crypttab_entry = configuration_dict;
          break;
        }
      else
        {
          g_variant_unref (configuration_dict);
        }
    }

  /* if there is an existing configuration and it has a passphrase, get the actual passphrase
   * as well (involves polkit dialog)
   */
  if (configured && get_passphrase_contents)
    {
      udisks_block_call_get_secret_configuration (data->block,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* cancellable */
                                                  (GAsyncReadyCallback) crypttab_dialog_on_get_secrets_cb,
                                                  data);
    }
  else
    {
      /* otherwise just set up the dialog */
      crypttab_dialog_present (data);
    }
}
