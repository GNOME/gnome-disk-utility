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

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdufstabdialog.h"
#include "gduvolumegrid.h"
#include "gduutils.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkWidget *dialog;
  GtkWidget *configure_checkbutton;
  GtkWidget *table;

  GtkWidget *infobar_hbox;
  GtkWidget *device_combobox;
  GtkWidget *device_explanation_label;
  GtkWidget *directory_entry;
  GtkWidget *type_entry;
  GtkWidget *options_entry;
  GtkWidget *freq_spinbutton;
  GtkWidget *passno_spinbutton;

  GVariant *orig_fstab_entry;
} FstabDialogData;

static void
fstab_dialog_update (FstabDialogData *data)
{
  gboolean ui_configured;
  gchar *ui_fsname;
  const gchar *ui_dir;
  const gchar *ui_type;
  const gchar *ui_opts;
  gint ui_freq;
  gint ui_passno;
  gboolean configured;
  const gchar *fsname;
  const gchar *dir;
  const gchar *type;
  const gchar *opts;
  gint freq;
  gint passno;
  gboolean can_apply;

  if (data->orig_fstab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data->orig_fstab_entry, "fsname", "^&ay", &fsname);
      g_variant_lookup (data->orig_fstab_entry, "dir", "^&ay", &dir);
      g_variant_lookup (data->orig_fstab_entry, "type", "^&ay", &type);
      g_variant_lookup (data->orig_fstab_entry, "opts", "^&ay", &opts);
      g_variant_lookup (data->orig_fstab_entry, "freq", "i", &freq);
      g_variant_lookup (data->orig_fstab_entry, "passno", "i", &passno);
    }
  else
    {
      configured = FALSE;
      fsname = "";
      dir = "";
      type = "";
      opts = "";
      freq = 0;
      passno = 0;
    }

  ui_configured = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->configure_checkbutton));
  ui_fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->device_combobox));
  ui_dir = gtk_entry_get_text (GTK_ENTRY (data->directory_entry));
  ui_type = gtk_entry_get_text (GTK_ENTRY (data->type_entry));
  ui_opts = gtk_entry_get_text (GTK_ENTRY (data->options_entry));
  ui_freq = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->freq_spinbutton));
  ui_passno = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->passno_spinbutton));

  can_apply = FALSE;
  if (configured != ui_configured)
    {
      can_apply = TRUE;
    }
  else if (ui_configured)
    {
      if (g_strcmp0 (ui_fsname, fsname) != 0 ||
          g_strcmp0 (ui_dir, dir) != 0 ||
          g_strcmp0 (ui_type, type) != 0 ||
          g_strcmp0 (ui_opts, opts) != 0 ||
          freq != ui_freq ||
          passno != ui_passno)
        {
          can_apply = TRUE;
        }
    }

  gtk_widget_set_sensitive (data->table, ui_configured);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_APPLY,
                                     can_apply);

  g_free (ui_fsname);
}

static void
fstab_dialog_property_changed (GObject     *object,
                               GParamSpec  *pspec,
                               gpointer     user_data)
{
  FstabDialogData *data = user_data;
  fstab_dialog_update (data);
}

static void
fstab_update_device_explanation (FstabDialogData *data)
{
  const gchar *s;
  gchar *fsname;
  gchar *str;
  gchar *explanation;
  guint part_num;

  fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->device_combobox));

  part_num = 0;
  s = g_strrstr (fsname, "-part");
  if (s != NULL)
    sscanf (s, "-part%d", &part_num);

  if (g_str_has_prefix (fsname, "/dev/disk/by-id/"))
    {
      if (part_num > 0)
        explanation = g_strdup_printf (_("Matches partition %d of the device with the given vital product data"),
                                       part_num);
      else
        explanation = g_strdup (_("Matches the whole disk of the device with the given vital product data"));
    }
  else if (g_str_has_prefix (fsname, "/dev/disk/by-path/"))
    {
      if (part_num > 0)
        explanation = g_strdup_printf (_("Matches partition %d of any device connected at the given port or address"),
                                       part_num);
      else
        explanation = g_strdup (_("Matches the whole disk of any device connected at the given port or address"));
    }
  else if (g_str_has_prefix (fsname, "/dev/disk/by-label/") || g_str_has_prefix (fsname, "LABEL="))
    {
      explanation = g_strdup (_("Matches any device with the given label"));
    }
  else if (g_str_has_prefix (fsname, "/dev/disk/by-uuid/") || g_str_has_prefix (fsname, "UUID="))
    {
      explanation = g_strdup (_("Matches the device with the given UUID"));
    }
  else
    {
      explanation = g_strdup (_("Matches the given device"));
    }

  str = g_strdup_printf ("<small><i>%s</i></small>", explanation);
  gtk_label_set_markup (GTK_LABEL (data->device_explanation_label), str);
  g_free (str);
  g_free (explanation);
  g_free (fsname);
}

static void
fstab_on_device_combobox_changed (GtkComboBox *combobox,
                                  gpointer     user_data)
{
  FstabDialogData *data = user_data;
  gchar *fsname;
  gchar *proposed_mount_point;
  const gchar *s;

  fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->device_combobox));
  s = strrchr (fsname, '/');
  if (s == NULL)
    s = strrchr (fsname, '=');
  if (s == NULL)
    s = "/disk";
  proposed_mount_point = g_strdup_printf ("/media/%s", s + 1);

  gtk_entry_set_text (GTK_ENTRY (data->directory_entry), proposed_mount_point);
  g_free (proposed_mount_point);
  g_free (fsname);

  fstab_update_device_explanation (data);
}

static void
fstab_populate_device_combo_box (GtkWidget         *device_combobox,
                                 UDisksDrive       *drive,
                                 UDisksBlock       *block,
                                 const gchar       *fstab_device)
{
  const gchar *device;
  const gchar *const *symlinks;
  guint n;
  gint selected;
  const gchar *uuid;
  const gchar *label;
  guint num_items;
  gchar *s;
  gint by_uuid = -1;
  gint by_label = -1;
  gint by_id = -1;
  gint by_path = -1;

  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (device_combobox));

  num_items = 0;
  selected = -1;

  device = udisks_block_get_device (block);
  gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox),
                             NULL,
                             device);
  if (g_strcmp0 (fstab_device, device) == 0)
    selected = num_items;
  num_items = 1;

  symlinks = udisks_block_get_symlinks (block);
  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
    {
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox), NULL, symlinks[n]);

      if (g_str_has_prefix (symlinks[n], "/dev/disk/by-uuid"))
        by_uuid = num_items;
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-label"))
        by_label = num_items;
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-id"))
        by_id = num_items;
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-path"))
        by_path = num_items;

      if (g_strcmp0 (fstab_device, symlinks[n]) == 0)
        selected = num_items;
      num_items++;
    }

  uuid = udisks_block_get_id_uuid (block);
  if (uuid != NULL && strlen (uuid) > 0)
    {
      s = g_strdup_printf ("UUID=%s", uuid);
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox), NULL, s);
      if (g_strcmp0 (fstab_device, s) == 0)
        selected = num_items;
      g_free (s);
      num_items++;
    }

  label = udisks_block_get_id_label (block);
  if (label != NULL && strlen (label) > 0)
    {
      s = g_strdup_printf ("LABEL=%s", label);
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox), NULL, s);
      if (g_strcmp0 (fstab_device, s) == 0)
        selected = num_items;
      g_free (s);
      num_items++;
    }

  /* Choose a device to default if creating a new entry */
  if (selected == -1 && fstab_device == NULL)
    {
      /* if the device is using removable media, prefer
       * by-id / by-path to by-uuid / by-label
       */
      if (drive != NULL && gdu_utils_drive_treat_as_removable (drive, block))
        {
          if (by_id != -1)
            selected = by_id;
          else if (by_path != -1)
            selected = by_path;
          else if (by_uuid != -1)
            selected = by_uuid;
          else if (by_label != -1)
            selected = by_label;
        }
      else
        {
          if (by_uuid != -1)
            selected = by_uuid;
          else if (by_label != -1)
            selected = by_label;
          else if (by_id != -1)
            selected = by_id;
          else if (by_path != -1)
            selected = by_path;
        }
    }
  /* Fall back to device name as a last resort */
  if (selected == -1)
    selected = 0;

  gtk_combo_box_set_active (GTK_COMBO_BOX (device_combobox), selected);
}

static gboolean
check_if_system_mount (const gchar *dir)
{
  guint n;
  static const gchar *dirs[] = {
    "/",
    "/boot",
    "/home",
    "/usr",
    "/usr/local",
    "/var",
    "/var/crash",
    "/var/local",
    "/var/log",
    "/var/log/audit",
    "/var/mail",
    "/var/run",
    "/var/tmp",
    "/opt",
    "/root",
    "/tmp",
    NULL
  };

  for (n = 0; dirs[n] != NULL; n++)
    if (g_strcmp0 (dir, dirs[n]) == 0)
      return TRUE;
  return FALSE;
}

void
gdu_fstab_dialog_show (GduWindow    *window,
                       UDisksObject *object)
{
  GtkBuilder *builder;
  UDisksBlock *block;
  UDisksObject *drive_object;
  UDisksDrive *drive;
  gint response;
  GtkWidget *dialog;
  FstabDialogData data;
  gboolean configured;
  gchar *fsname;
  const gchar *dir;
  const gchar *type;
  const gchar *opts;
  gint freq;
  gint passno;
  GVariantIter iter;
  const gchar *configuration_type;
  GVariant *configuration_dict;
  gboolean is_system_mount;

  block = udisks_object_peek_block (object);
  g_assert (block != NULL);

  drive = NULL;
  drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (gdu_window_get_client (window)),
                                                                    udisks_block_get_drive (block));
  if (drive_object != NULL)
    {
      drive = udisks_object_peek_drive (drive_object);
      g_object_unref (drive_object);
    }

  dialog = gdu_application_new_widget (gdu_window_get_application (window),
                                       "edit-fstab-dialog.ui",
                                       "device-fstab-dialog", &builder);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  memset (&data, '\0', sizeof (FstabDialogData));
  data.dialog = dialog;
  data.infobar_hbox = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-infobar-hbox"));
  data.configure_checkbutton = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-configure-checkbutton"));
  data.table = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-table"));
  data.device_combobox = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-device-combobox"));
  data.device_explanation_label = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-device-explanation-label"));
  data.directory_entry = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-directory-entry"));
  data.type_entry = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-type-entry"));
  data.options_entry = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-options-entry"));
  data.freq_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-freq-spinbutton"));
  data.passno_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-passno-spinbutton"));

  /* there could be multiple fstab entries - we only consider the first one */
  g_variant_iter_init (&iter, udisks_block_get_configuration (block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &configuration_type, &configuration_dict))
    {
      if (g_strcmp0 (configuration_type, "fstab") == 0)
        {
          data.orig_fstab_entry = configuration_dict;
          break;
        }
      else
        {
          g_variant_unref (configuration_dict);
        }
    }
  if (data.orig_fstab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data.orig_fstab_entry, "fsname", "^ay", &fsname);
      g_variant_lookup (data.orig_fstab_entry, "dir", "^&ay", &dir);
      g_variant_lookup (data.orig_fstab_entry, "type", "^&ay", &type);
      g_variant_lookup (data.orig_fstab_entry, "opts", "^&ay", &opts);
      g_variant_lookup (data.orig_fstab_entry, "freq", "i", &freq);
      g_variant_lookup (data.orig_fstab_entry, "passno", "i", &passno);
    }
  else
    {
      configured = FALSE;
      fsname = NULL;
      dir = "";
      type = "auto";
      opts = "defaults";
      /* propose noauto if the media is removable - otherwise e.g. systemd will time out at boot */
      if (drive != NULL && gdu_utils_drive_treat_as_removable (drive, block))
        opts = "defaults,noauto";
      freq = 0;
      passno = 0;
    }
  is_system_mount = check_if_system_mount (dir);

  fstab_populate_device_combo_box (data.device_combobox,
                                   drive,
                                   block,
                                   fsname);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data.configure_checkbutton), configured);
  gtk_entry_set_text (GTK_ENTRY (data.directory_entry), dir);
  gtk_entry_set_text (GTK_ENTRY (data.type_entry), type);
  gtk_entry_set_text (GTK_ENTRY (data.options_entry), opts);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (data.freq_spinbutton), freq);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (data.passno_spinbutton), passno);
  if (!configured)
    fstab_on_device_combobox_changed (GTK_COMBO_BOX (data.device_combobox), &data);

  g_signal_connect (data.configure_checkbutton,
                    "notify::active", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.device_combobox,
                    "notify::active", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.directory_entry,
                    "notify::text", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.type_entry,
                    "notify::text", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.options_entry,
                    "notify::text", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.freq_spinbutton,
                    "notify::value", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.passno_spinbutton,
                    "notify::value", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.device_combobox,
                    "changed", G_CALLBACK (fstab_on_device_combobox_changed), &data);

  /* Show a cluebar if the entry is considered a system mount */
  if (is_system_mount)
    {
      GtkWidget *bar;
      GtkWidget *label;
      GtkWidget *image;
      GtkWidget *hbox;

      bar = gtk_info_bar_new ();
      gtk_info_bar_set_message_type (GTK_INFO_BAR (bar), GTK_MESSAGE_WARNING);

      image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_BUTTON);

      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (label),
                            _("<b>Warning:</b> "
                              "The system may not work correctly if this entry is modified or removed."));

      hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
      gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
      gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

      gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (bar))), hbox);
      gtk_box_pack_start (GTK_BOX (data.infobar_hbox), bar, TRUE, TRUE, 0);
    }

  gtk_widget_show_all (dialog);

  fstab_update_device_explanation (&data);
  fstab_dialog_update (&data);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response == GTK_RESPONSE_APPLY)
    {
      gboolean ui_configured;
      gchar *ui_fsname;
      const gchar *ui_dir;
      const gchar *ui_type;
      const gchar *ui_opts;
      gint ui_freq;
      gint ui_passno;
      GError *error;
      GVariant *old_item;
      GVariant *new_item;

      ui_configured = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data.configure_checkbutton));
      ui_fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data.device_combobox));
      ui_dir = gtk_entry_get_text (GTK_ENTRY (data.directory_entry));
      ui_type = gtk_entry_get_text (GTK_ENTRY (data.type_entry));
      ui_opts = gtk_entry_get_text (GTK_ENTRY (data.options_entry));
      ui_freq = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data.freq_spinbutton));
      ui_passno = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data.passno_spinbutton));

      gtk_widget_hide (dialog);

      old_item = NULL;
      new_item = NULL;

      if (configured)
        {
          old_item = g_variant_new ("(s@a{sv})", "fstab", data.orig_fstab_entry);
        }

      if (ui_configured)
        {
          GVariantBuilder builder;
          g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
          g_variant_builder_add (&builder, "{sv}", "fsname", g_variant_new_bytestring (ui_fsname));
          g_variant_builder_add (&builder, "{sv}", "dir", g_variant_new_bytestring (ui_dir));
          g_variant_builder_add (&builder, "{sv}", "type", g_variant_new_bytestring (ui_type));
          g_variant_builder_add (&builder, "{sv}", "opts", g_variant_new_bytestring (ui_opts));
          g_variant_builder_add (&builder, "{sv}", "freq", g_variant_new_int32 (ui_freq));
          g_variant_builder_add (&builder, "{sv}", "passno", g_variant_new_int32 (ui_passno));
          new_item = g_variant_new ("(sa{sv})", "fstab", &builder);
        }

      if (old_item != NULL && new_item == NULL)
        {
          error = NULL;
          if (!udisks_block_call_remove_configuration_item_sync (block,
                                                                 old_item,
                                                                 g_variant_new ("a{sv}", NULL), /* options */
                                                                 NULL, /* GCancellable */
                                                                 &error))
            {
              gdu_window_show_error (window,
                                     _("Error removing old /etc/fstab entry"),
                                     error);
              g_error_free (error);
              g_free (ui_fsname);
              goto out;
            }
        }
      else if (old_item == NULL && new_item != NULL)
        {
          error = NULL;
          if (!udisks_block_call_add_configuration_item_sync (block,
                                                                     new_item,
                                                                     g_variant_new ("a{sv}", NULL), /* options */
                                                                     NULL, /* GCancellable */
                                                                     &error))
            {
              gdu_window_show_error (window,
                                     _("Error adding new /etc/fstab entry"),
                                     error);
              g_error_free (error);
              g_free (ui_fsname);
              goto out;
            }
        }
      else if (old_item != NULL && new_item != NULL)
        {
          error = NULL;
          if (!udisks_block_call_update_configuration_item_sync (block,
                                                                 old_item,
                                                                 new_item,
                                                                 g_variant_new ("a{sv}", NULL), /* options */
                                                                 NULL, /* GCancellable */
                                                                 &error))
            {
              gdu_window_show_error (window,
                                     _("Error updating /etc/fstab entry"),
                                     error);
              g_error_free (error);
              g_free (ui_fsname);
              goto out;
            }
        }
      else
        {
          g_assert_not_reached ();
        }
      g_free (ui_fsname);
    }

 out:
  if (data.orig_fstab_entry != NULL)
    g_variant_unref (data.orig_fstab_entry);
  g_free (fsname);

  gtk_widget_hide (dialog);
  gtk_widget_destroy (dialog);
  g_object_unref (builder);
}
