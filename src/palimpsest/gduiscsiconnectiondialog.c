/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"

#include <stdlib.h>

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduiscsiconnectiondialog.h"
#include "gduutils.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GduWindow *window;
  GtkWidget *dialog;

  UDisksiSCSITarget  *target;
  gchar              *host;
  gint                port;
  gint                tpgt;
  gchar              *interface_name;
  GVariant           *configuration;

  gint initial_auth_combobox_active;

  GtkWidget *startup_switch;
  GtkWidget *auth_combobox;
  GtkWidget *timeout_spinbutton;
  GtkWidget *auth_vbox;
  GtkWidget *username_entry;
  GtkWidget *password_entry;
  GtkWidget *password_show_checkbutton;
  GtkWidget *auth_in_vbox;
  GtkWidget *username_in_entry;
  GtkWidget *password_in_entry;
  GtkWidget *password_in_show_checkbutton;
} DialogData;

static void
dialog_data_free (DialogData *data)
{
  g_object_unref (data->window);
  gtk_widget_hide (data->dialog);
  gtk_widget_destroy (data->dialog);

  g_object_unref (data->target);
  g_free (data->host);
  g_free (data->interface_name);
  g_variant_unref (data->configuration);

  g_free (data);
}

static void
update (DialogData *data,
        GtkWidget  *widget)
{
  const gchar *startup = NULL;
  const gchar *auth_method = NULL;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *username_in = NULL;
  const gchar *password_in = NULL;
  const gchar *timeout_str = NULL;
  gint timeout_val = 0;
  gboolean startup_automatic = FALSE;
  gboolean show_auth = FALSE;
  gboolean show_auth_in = FALSE;
  gboolean changed = FALSE;
  gboolean valid = FALSE;
  gint auth_combobox_active;

  /* Update visibility of authentication sections */
  auth_combobox_active = gtk_combo_box_get_active (GTK_COMBO_BOX (data->auth_combobox));
  switch (auth_combobox_active)
    {
    case 0: /* None */
      break;

    case 1: /* CHAP */
      show_auth = TRUE;
      break;

    case 2: /* Mutual CHAP */
      show_auth = TRUE;
      show_auth_in = TRUE;
      break;
    }
  if (show_auth)
    gtk_widget_show (data->auth_vbox);
  else
    gtk_widget_hide (data->auth_vbox);
  if (show_auth_in)
    gtk_widget_show (data->auth_in_vbox);
  else
    gtk_widget_hide (data->auth_in_vbox);

  /* compare dialog data to whatever we bootstrapped it with */
  g_variant_lookup (data->configuration, "node.startup", "&s", &startup);
  g_variant_lookup (data->configuration, "node.session.auth.authmethod", "&s", &auth_method);
  g_variant_lookup (data->configuration, "node.session.auth.username", "&s", &username);
  g_variant_lookup (data->configuration, "node.session.auth.password", "&s", &password);
  g_variant_lookup (data->configuration, "node.session.auth.username_in", "&s", &username_in);
  g_variant_lookup (data->configuration, "node.session.auth.password_in", "&s", &password_in);
  g_variant_lookup (data->configuration, "node.session.timeo.replacement_timeout", "&s", &timeout_str);

  if (g_strcmp0 (startup, "automatic") == 0)
    startup_automatic = TRUE;

  if (timeout_str != NULL)
    timeout_val = atoi (timeout_str);

  /* calculate if data in the dialog has changed from the given configuration */
  if (startup_automatic != gtk_switch_get_active (GTK_SWITCH (data->startup_switch)))
    changed = TRUE;

  if (timeout_val != gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->timeout_spinbutton)))
    changed = TRUE;

  switch (auth_combobox_active)
    {
    case 0: /* None */
      if (g_strcmp0 (auth_method, "None") != 0)
        changed = TRUE;
      break;

    case 1: /* CHAP */
      if (g_strcmp0 (auth_method, "CHAP") != 0 ||
          g_strcmp0 (username, gtk_entry_get_text (GTK_ENTRY (data->username_entry))) != 0 ||
          g_strcmp0 (password, gtk_entry_get_text (GTK_ENTRY (data->password_entry))) != 0)
        changed = TRUE;
      break;

    case 2: /* Mutual CHAP */
      if (g_strcmp0 (auth_method, "CHAP") != 0 ||
          g_strcmp0 (username,    gtk_entry_get_text (GTK_ENTRY (data->username_entry))) != 0 ||
          g_strcmp0 (password,    gtk_entry_get_text (GTK_ENTRY (data->password_entry))) != 0 ||
          g_strcmp0 (username_in, gtk_entry_get_text (GTK_ENTRY (data->username_in_entry))) != 0 ||
          g_strcmp0 (password_in, gtk_entry_get_text (GTK_ENTRY (data->password_in_entry))) != 0)
        changed = TRUE;
      break;
    }
  if (auth_combobox_active != data->initial_auth_combobox_active)
    changed = TRUE;

  /* validate data */
  valid = TRUE;
  switch (auth_combobox_active)
    {
    case 1: /* CHAP */
      if (strlen (gtk_entry_get_text (GTK_ENTRY (data->username_entry))) == 0 ||
          strlen (gtk_entry_get_text (GTK_ENTRY (data->password_entry))) == 0)
        valid = FALSE;
      break;

    case 2: /* Mutal CHAP */
      if (strlen (gtk_entry_get_text (GTK_ENTRY (data->username_entry))) == 0 ||
          strlen (gtk_entry_get_text (GTK_ENTRY (data->password_entry))) == 0 ||
          strlen (gtk_entry_get_text (GTK_ENTRY (data->username_in_entry))) == 0 ||
          strlen (gtk_entry_get_text (GTK_ENTRY (data->password_in_entry))) == 0)
        valid = FALSE;
      break;
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     changed && valid);
}

static void
on_property_changed (GObject     *object,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  DialogData *data = user_data;
  update (data, GTK_WIDGET (object));
}

static void
initial_populate (DialogData *data)
{
  const gchar *startup = NULL;
  const gchar *auth_method = NULL;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *username_in = NULL;
  const gchar *password_in = NULL;
  const gchar *timeout_str = NULL;
  gint timeout_val = 0;
  gboolean startup_automatic = FALSE;

  g_variant_lookup (data->configuration, "node.startup", "&s", &startup);
  g_variant_lookup (data->configuration, "node.session.auth.authmethod", "&s", &auth_method);
  g_variant_lookup (data->configuration, "node.session.auth.username", "&s", &username);
  g_variant_lookup (data->configuration, "node.session.auth.password", "&s", &password);
  g_variant_lookup (data->configuration, "node.session.auth.username_in", "&s", &username_in);
  g_variant_lookup (data->configuration, "node.session.auth.password_in", "&s", &password_in);
  g_variant_lookup (data->configuration, "node.session.timeo.replacement_timeout", "&s", &timeout_str);

  if (g_strcmp0 (startup, "automatic") == 0)
    startup_automatic = TRUE;

  if (timeout_str != NULL)
    timeout_val = atoi (timeout_str);

  gtk_switch_set_active (GTK_SWITCH (data->startup_switch), startup_automatic);
  gtk_entry_set_text (GTK_ENTRY (data->username_entry), username);
  gtk_entry_set_text (GTK_ENTRY (data->password_entry), password);
  gtk_entry_set_text (GTK_ENTRY (data->username_in_entry), username_in);
  gtk_entry_set_text (GTK_ENTRY (data->password_in_entry), password_in);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (data->timeout_spinbutton), timeout_val);

  if (g_strcmp0 (auth_method, "None") == 0)
    {
      data->initial_auth_combobox_active = 0;  /* None */
    }
  else if (g_strcmp0 (auth_method, "CHAP") == 0)
    {
      if (username_in != NULL && strlen (username_in) > 0)
        data->initial_auth_combobox_active = 2; /* Mutual CHAP */
      else
        data->initial_auth_combobox_active = 1; /* CHAP */
    }
  else
    {
      g_warning ("No support for auth_method `%s'", auth_method);
        data->initial_auth_combobox_active = -1; /* No item */
    }
  gtk_combo_box_set_active (GTK_COMBO_BOX (data->auth_combobox), data->initial_auth_combobox_active);
}

/* ---------------------------------------------------------------------------------------------------- */

static void show_no_populate (DialogData *data);

static void
update_conf_cb (UDisksiSCSITarget *target,
                GAsyncResult      *res,
                gpointer           user_data)
{
  DialogData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_iscsi_target_call_update_configuration_finish (target,
                                                             res,
                                                             &error))
    {
      if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
        {
          g_error_free (error);
          show_no_populate (data);
          goto out;
        }
      gtk_widget_hide (data->dialog);
      gdu_window_show_error (data->window,
                             _("Error updating configuration"),
                             error);
      g_error_free (error);
      dialog_data_free (data);
      goto out;
    }

  dialog_data_free (data);

 out:
  ;
}


static void
show_no_populate (DialogData *data)
{
  gint response;
  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      GVariantBuilder builder;
      gchar timeout_str[16];

      g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));

      snprintf (timeout_str, sizeof timeout_str, "%d",
                (gint) gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->timeout_spinbutton)));
      g_variant_builder_add (&builder, "{ss}", "node.session.timeo.replacement_timeout", timeout_str);
      g_variant_builder_add (&builder, "{ss}", "node.startup",
                             gtk_switch_get_active (GTK_SWITCH (data->startup_switch)) ? "automatic" : "manual");
      switch (gtk_combo_box_get_active (GTK_COMBO_BOX (data->auth_combobox)))
        {
        case 0: /* None */
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.authmethod", "None");
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.username", "");
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.password", "");
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.username_in", "");
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.password_in", "");
          break;

        case 1: /* CHAP */
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.authmethod", "CHAP");
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.username",
                                 gtk_entry_get_text (GTK_ENTRY (data->username_entry)));
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.password",
                                 gtk_entry_get_text (GTK_ENTRY (data->password_entry)));
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.username_in", "");
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.password_in", "");
          break;

        case 2: /* Mutual CHAP */
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.authmethod", "CHAP");
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.username",
                                 gtk_entry_get_text (GTK_ENTRY (data->username_entry)));
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.password",
                                 gtk_entry_get_text (GTK_ENTRY (data->password_entry)));
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.username_in",
                                 gtk_entry_get_text (GTK_ENTRY (data->username_in_entry)));
          g_variant_builder_add (&builder, "{ss}", "node.session.auth.password_in",
                                 gtk_entry_get_text (GTK_ENTRY (data->password_in_entry)));
          break;
        }


      udisks_iscsi_target_call_update_configuration (data->target,
                                                     data->host,
                                                     data->port,
                                                     data->tpgt,
                                                     data->interface_name,
                                                     g_variant_builder_end (&builder),
                                                     g_variant_new ("a{sv}", NULL), /* options */
                                                     NULL,  /* GCancellable* */
                                                     (GAsyncReadyCallback) update_conf_cb,
                                                     data);
    }
  else
    {
      dialog_data_free (data);
    }
}

static void
show (DialogData *data)
{
  initial_populate (data);
  update (data, NULL);
  gtk_widget_show_all (data->dialog);
  show_no_populate (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
get_secret_cb (UDisksiSCSITarget *target,
               GAsyncResult      *res,
               gpointer           user_data)
{
  DialogData *data = user_data;
  GError *error;
  GVariant *configuration;

  configuration = NULL;
  error = NULL;
  if (!udisks_iscsi_target_call_get_secret_configuration_finish (target,
                                                                 &configuration,
                                                                 res,
                                                                 &error))
    {
      gdu_window_show_error (data->window,
                             _("Error retrieving configuration data with passwords"),
                             error);
      g_error_free (error);
      dialog_data_free (data);
      goto out;
    }

  g_variant_unref (data->configuration);
  data->configuration = configuration;

  show (data);

 out:
  ;
}

void
gdu_iscsi_connection_dialog_show (GduWindow          *window,
                                  UDisksiSCSITarget  *target,
                                  const gchar        *host,
                                  gint                port,
                                  gint                tpgt,
                                  const gchar        *interface_name,
                                  GVariant           *configuration)
{
  GtkBuilder *builder;
  DialogData *data;
  const gchar *auth_method;

  data = g_new0 (DialogData, 1);

  data->window = g_object_ref (window);
  data->target = g_object_ref (target);
  data->host = g_strdup (host);
  data->port = port;
  data->tpgt = tpgt;
  data->interface_name = g_strdup (interface_name);
  data->configuration = g_variant_ref (configuration);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "iscsi-connection-dialog.ui",
                                                         "iscsi-connection-dialog",
                                                         &builder));

  data->startup_switch               = GTK_WIDGET (gtk_builder_get_object (builder, "automatic-startup-switch"));
  data->auth_combobox                = GTK_WIDGET (gtk_builder_get_object (builder, "auth-combobox"));
  data->timeout_spinbutton           = GTK_WIDGET (gtk_builder_get_object (builder, "timeout-spinbutton"));
  data->username_entry               = GTK_WIDGET (gtk_builder_get_object (builder, "initiator-auth-username-entry"));
  data->password_entry               = GTK_WIDGET (gtk_builder_get_object (builder, "initiator-auth-password-entry"));
  data->username_in_entry            = GTK_WIDGET (gtk_builder_get_object (builder, "target-auth-username-entry"));
  data->password_in_entry            = GTK_WIDGET (gtk_builder_get_object (builder, "target-auth-password-entry"));
  data->auth_vbox                    = GTK_WIDGET (gtk_builder_get_object (builder, "initiator-auth-vbox"));
  data->password_show_checkbutton    = GTK_WIDGET (gtk_builder_get_object (builder, "initiator-auth-show-password-checkbutton"));
  data->auth_in_vbox                 = GTK_WIDGET (gtk_builder_get_object (builder, "target-auth-vbox"));
  data->password_in_show_checkbutton = GTK_WIDGET (gtk_builder_get_object (builder, "target-auth-show-password-checkbutton"));

  g_object_bind_property (data->password_show_checkbutton,
                          "active",
                          data->password_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (data->password_in_show_checkbutton,
                          "active",
                          data->password_in_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);

  g_signal_connect (data->startup_switch, "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->auth_combobox, "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->timeout_spinbutton, "notify::value", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->username_entry, "notify::text", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->password_entry, "notify::text", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->username_in_entry, "notify::text", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->password_in_entry, "notify::text", G_CALLBACK (on_property_changed), data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  /* visibility of these depends on auth_combobox value, see update() */
  gtk_widget_set_no_show_all (data->auth_vbox, TRUE);
  gtk_widget_set_no_show_all (data->auth_in_vbox, TRUE);

  /* see if we need to retrieve passwords (will cause polkit dialog to be shown) */
  g_variant_lookup (data->configuration, "node.session.auth.authmethod", "&s", &auth_method);
  if (g_strcmp0 (auth_method, "None") == 0)
    {
      show (data);
    }
  else
    {
      udisks_iscsi_target_call_get_secret_configuration (data->target,
                                                         data->host,
                                                         data->port,
                                                         data->tpgt,
                                                         data->interface_name,
                                                         g_variant_new ("a{sv}", NULL), /* options */
                                                         NULL,  /* GCancellable* */
                                                         (GAsyncReadyCallback) get_secret_cb,
                                                         data);
    }
  g_object_unref (builder);
}
