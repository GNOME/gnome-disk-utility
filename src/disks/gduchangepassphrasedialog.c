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
#include "gduchangepassphrasedialog.h"
#include "gdupasswordstrengthwidget.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GduWindow *window;

  UDisksObject *object;
  UDisksBlock *block;
  UDisksEncrypted *encrypted;

  gboolean has_passphrase_in_configuration;
  GVariant *crypttab_details;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *infobar_vbox;
  GtkWidget *existing_passphrase_entry;
  GtkWidget *passphrase_entry;
  GtkWidget *confirm_passphrase_entry;
  GtkWidget *show_passphrase_checkbutton;
  GtkWidget *passphrase_strengh_box;
  GtkWidget *passphrase_strengh_widget;

} ChangePassphraseData;

static void
change_passphrase_data_free (ChangePassphraseData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->object);
  g_object_unref (data->block);
  g_object_unref (data->encrypted);
  if (data->dialog != NULL)
    {
      gtk_widget_hide (data->dialog);
      gtk_widget_destroy (data->dialog);
    }
  if (data->builder != NULL)
    g_object_unref (data->builder);
  if (data->crypttab_details != NULL)
    g_variant_unref (data->crypttab_details);
  g_free (data);
}

static void
populate (ChangePassphraseData *data)
{
  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->existing_passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->confirm_passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
}

static void
update (ChangePassphraseData *data)
{
  gboolean can_proceed = FALSE;
  const gchar *existing_passphrase;
  const gchar *passphrase;
  const gchar *confirm_passphrase;

  existing_passphrase = gtk_entry_get_text (GTK_ENTRY (data->existing_passphrase_entry));
  passphrase = gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry));
  confirm_passphrase = gtk_entry_get_text (GTK_ENTRY (data->confirm_passphrase_entry));

  gtk_entry_set_icon_from_icon_name (GTK_ENTRY (data->confirm_passphrase_entry),
                                     GTK_ENTRY_ICON_SECONDARY,
                                     NULL);
  gtk_entry_set_icon_tooltip_text (GTK_ENTRY (data->confirm_passphrase_entry),
                                   GTK_ENTRY_ICON_SECONDARY,
                                   NULL);
  gtk_entry_set_icon_from_icon_name (GTK_ENTRY (data->passphrase_entry),
                                     GTK_ENTRY_ICON_SECONDARY,
                                     NULL);
  gtk_entry_set_icon_tooltip_text (GTK_ENTRY (data->passphrase_entry),
                                   GTK_ENTRY_ICON_SECONDARY,
                                   NULL);

  gdu_password_strength_widget_set_password (GDU_PASSWORD_STRENGTH_WIDGET (data->passphrase_strengh_widget),
                                             passphrase);

  if (strlen (passphrase) > 0 && strlen (confirm_passphrase) > 0 && g_strcmp0 (passphrase, confirm_passphrase) != 0)
    {
      gtk_entry_set_icon_from_icon_name (GTK_ENTRY (data->confirm_passphrase_entry),
                                         GTK_ENTRY_ICON_SECONDARY,
                                         "dialog-warning-symbolic");
      gtk_entry_set_icon_tooltip_text (GTK_ENTRY (data->confirm_passphrase_entry),
                                       GTK_ENTRY_ICON_SECONDARY,
                                       _("The passphrases do not match"));
    }
  if (strlen (existing_passphrase) > 0 && strlen (passphrase) > 0 && g_strcmp0 (passphrase, existing_passphrase) == 0)
    {
      gtk_entry_set_icon_from_icon_name (GTK_ENTRY (data->passphrase_entry),
                                         GTK_ENTRY_ICON_SECONDARY,
                                         "dialog-warning-symbolic");
      gtk_entry_set_icon_tooltip_text (GTK_ENTRY (data->passphrase_entry),
                                       GTK_ENTRY_ICON_SECONDARY,
                                       _("The passphrase matches the existing passphrase"));
    }

  if (strlen (existing_passphrase) > 0 && strlen (passphrase) > 0 &&
      g_strcmp0 (passphrase, confirm_passphrase) == 0 &&
      g_strcmp0 (passphrase, existing_passphrase) != 0)
    can_proceed = TRUE;
  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
on_property_changed (GObject     *object,
                                    GParamSpec  *pspec,
                                    gpointer     user_data)
{
  ChangePassphraseData *data = user_data;
  update (data);
}

static gboolean
has_passphrase_in_configuration (ChangePassphraseData *data)
{
  gboolean ret = FALSE;
  GVariantIter iter;
  const gchar *type;
  GVariant *details;

  g_variant_iter_init (&iter, udisks_block_get_configuration (data->block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &type, &details))
    {
      if (g_strcmp0 (type, "crypttab") == 0)
        {
          const gchar *passphrase_path;
          if (g_variant_lookup (details, "passphrase-path", "^&ay", &passphrase_path) &&
              strlen (passphrase_path) > 0)
            {
              g_variant_unref (details);
              ret = TRUE;
              goto out;
            }
        }
      g_variant_unref (details);
    }
 out:
  return ret;
}

static void
update_configuration_item_cb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  ChangePassphraseData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_block_call_update_configuration_item_finish (UDISKS_BLOCK (source_object),
                                                           res,
                                                           &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window), _("Error updating /etc/crypttab"), error);
      g_error_free (error);
    }
  change_passphrase_data_free (data);
}

static void
change_passphrase_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  ChangePassphraseData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_encrypted_call_change_passphrase_finish (UDISKS_ENCRYPTED (source_object),
                                                       res,
                                                       &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window), _("Error changing passphrase"), error);
      g_error_free (error);
    }

  /* Update the system-level configuration, if applicable */
  if (data->has_passphrase_in_configuration)
    {
      GVariantBuilder builder;
      GVariantIter iter;
      const gchar *key;
      GVariant *value;

      g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
      g_variant_iter_init (&iter, data->crypttab_details);
      while (g_variant_iter_next (&iter, "{sv}", &key, &value))
        {
          if (g_strcmp0 (key, "passphrase-contents") == 0)
            {
              g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                     g_variant_new_bytestring (gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry))));
            }
          else
            {
              g_variant_builder_add (&builder, "{sv}", key, value);
            }
          g_variant_unref (value);
        }

      udisks_block_call_update_configuration_item (data->block,
                                                   g_variant_new ("(s@a{sv})", "crypttab", data->crypttab_details),
                                                   g_variant_new ("(sa{sv})", "crypttab", &builder),
                                                   g_variant_new ("a{sv}", NULL), /* options */
                                                   NULL, /* cancellable */
                                                   update_configuration_item_cb,
                                                   data);

    }
  else
    {
      change_passphrase_data_free (data);
    }
}

static void
run_dialog (ChangePassphraseData *data)
{
  gint response;

  gtk_widget_show_all (data->dialog);
  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  gtk_widget_hide (data->dialog);
  if (response == GTK_RESPONSE_OK)
    {
      udisks_encrypted_call_change_passphrase (data->encrypted,
                                               gtk_entry_get_text (GTK_ENTRY (data->existing_passphrase_entry)),
                                               gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry)),
                                               g_variant_new ("a{sv}", NULL), /* options */
                                               NULL, /* GCancellable */
                                               change_passphrase_cb,
                                               data);
    }
  else
    {
      change_passphrase_data_free (data);
    }
}

static void
on_get_secret_configuration_cb (GObject      *source_object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  ChangePassphraseData *data = user_data;
  GVariantIter iter;
  const gchar *type;
  GVariant *details;
  GVariant *configuration = NULL;
  GError *error;

  configuration = NULL;
  error = NULL;
  if (!udisks_block_call_get_secret_configuration_finish (UDISKS_BLOCK (source_object),
                                                          &configuration,
                                                          res,
                                                          &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error retrieving configuration data"),
                            error);
      g_error_free (error);
      change_passphrase_data_free (data);
      goto out;
    }

  g_variant_iter_init (&iter, configuration);
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &type, &details))
    {
      if (g_strcmp0 (type, "crypttab") == 0)
        {
          const gchar *passphrase_contents;
          data->crypttab_details = g_variant_ref (details);
          if (g_variant_lookup (details, "passphrase-contents", "^&ay", &passphrase_contents))
            {
              gtk_entry_set_text (GTK_ENTRY (data->existing_passphrase_entry), passphrase_contents);
              /* Don't focus on the "Existing passphrase" entry */
              gtk_editable_select_region (GTK_EDITABLE (data->existing_passphrase_entry), 0, 0);
              gtk_widget_grab_focus (data->passphrase_entry);
              run_dialog (data);
              goto out;
            }
        }
    }

  gdu_utils_show_error (GTK_WINDOW (data->window), _("/etc/crypttab configuration data is malformed"), NULL);
  change_passphrase_data_free (data);

 out:
  if (configuration != NULL)
    g_variant_unref (configuration);
}

void
gdu_change_passphrase_dialog_show (GduWindow    *window,
                                   UDisksObject *object)
{
  ChangePassphraseData *data;

  data = g_new0 (ChangePassphraseData, 1);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->encrypted = udisks_object_get_encrypted (object);
  g_assert (data->encrypted != NULL);
  data->has_passphrase_in_configuration = has_passphrase_in_configuration (data);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "change-passphrase-dialog.ui",
                                                         "change-passphrase-dialog",
                                                         &data->builder));

  data->infobar_vbox = GTK_WIDGET (gtk_builder_get_object (data->builder, "infobar-vbox"));
  if (data->has_passphrase_in_configuration)
    {
      GtkWidget *infobar;
      infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO,
                                           _("Changing the passphrase for this device, will also update the passphrase referenced by the <i>/etc/crypttab</i> file"),
                                           NULL);
      gtk_box_pack_start (GTK_BOX (data->infobar_vbox), infobar, TRUE, TRUE, 0);
    }

  data->existing_passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "existing-passphrase-entry"));
  g_signal_connect (data->existing_passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), data);

  data->passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "passphrase-entry"));
  g_signal_connect (data->passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), data);

  data->confirm_passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "confirm-passphrase-entry"));
  g_signal_connect (data->confirm_passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), data);
  data->show_passphrase_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "show-passphrase-checkbutton"));
  g_signal_connect (data->show_passphrase_checkbutton, "notify::active", G_CALLBACK (on_property_changed), data);

  data->passphrase_strengh_box = GTK_WIDGET (gtk_builder_get_object (data->builder, "passphrase-strength-box"));
  data->passphrase_strengh_widget = gdu_password_strength_widget_new ();
  gtk_widget_set_tooltip_markup (data->passphrase_strengh_widget,
                                 _("The strength of the passphrase"));
  gtk_box_pack_start (GTK_BOX (data->passphrase_strengh_box), data->passphrase_strengh_widget,
                      TRUE, TRUE, 0);


  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  populate (data);
  update (data);

  /* Retrieve the passphrase from system-level configuration, if applicable */
  if (data->has_passphrase_in_configuration)
    {
      udisks_block_call_get_secret_configuration (data->block,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* cancellable */
                                                  on_get_secret_configuration_cb,
                                                  data);
    }
  else
    {
      run_dialog (data);
    }
}
