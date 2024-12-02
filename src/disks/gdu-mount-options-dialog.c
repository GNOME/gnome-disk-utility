/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gdu-application.h"
#include "gdu-window.h"
#include "gdu-mount-options-dialog.h"

/* ---------------------------------------------------------------------------------------------------- */

struct _GduMountOptionsDialog
{
  AdwDialog      parent_instance;

  GtkWidget     *info_banner;
  GtkWidget     *overlay;
  GtkWidget     *spinner;
  GtkWidget     *save_button;

  GtkWidget     *automount_switch_row;

  GtkWidget     *startup_mount_switch;
  GtkWidget     *show_in_files_switch;
  GtkWidget     *require_auth_switch;

  GtkWidget     *name_row;
  GtkWidget     *icon_row;
  GtkWidget     *symbolic_icon_row;

  GtkWidget     *mount_point_row;
  GtkWidget     *mount_options_row;
  GtkWidget     *filesystem_type_row;

  GtkWidget     *device_combo_row;

  GtkWidget     *reset_settings_button;

  UDisksClient  *client;
  UDisksObject  *object;
  UDisksBlock   *block;
  GListModel    *model;
  GVariant      *orig_fstab_entry;
};

G_DEFINE_TYPE (GduMountOptionsDialog, gdu_mount_options_dialog, ADW_TYPE_DIALOG)

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
    {
      if (g_strcmp0 (dir, dirs[n]) == 0)
        {
          return TRUE;
        }
    }

  return FALSE;
}

static gpointer
gdu_mount_options_dialog_get_window (GduMountOptionsDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static gboolean
gdu_mount_options_dialog_is_swap (GduMountOptionsDialog *self)
{
  if (!self->object)
    return FALSE;

  return (udisks_object_peek_swapspace (self->object) != NULL);
}

static gboolean
gdu_mount_options_dialog_is_removable (GduMountOptionsDialog *self)
{
  g_autoptr(UDisksDrive) drive = NULL;
  g_autoptr(UDisksObject) drive_object = NULL;

  drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (self->client),
                                                                    udisks_block_get_drive (self->block));
  if (drive_object != NULL)
    {
      drive = udisks_object_peek_drive (drive_object);
    }

  return (drive != NULL && udisks_drive_get_removable (drive));
}

static GVariant *
gdu_mount_options_dialog_get_fstab (GduMountOptionsDialog *self)
{
  GVariantIter iter;
  GVariant *configuration_dict;
  const gchar *configuration_type;

  /* there could be multiple fstab entries - we only consider the first one */
  g_variant_iter_init (&iter, udisks_block_get_configuration (self->block));
  while (g_variant_iter_next (&iter,
                              "(&s@a{sv})",
                              &configuration_type,
                              &configuration_dict))
    {
      if (g_strcmp0 (configuration_type, "fstab") == 0)
        {
          return configuration_dict;
        }

      g_variant_unref (configuration_dict);
    }

  return NULL;
}

static GVariant *
gdu_mount_options_dialog_get_new_fstab (GduMountOptionsDialog *self)
{
  const gchar *fsname;
  const gchar *mountpoint;
  const gchar *fstype;
  const gchar *mountopts;
  gint freq = 0;
  gint passno = 0;
  GVariantBuilder variant_builder;
  gint selected;

  selected = adw_combo_row_get_selected (ADW_COMBO_ROW (self->device_combo_row));
  fsname = gtk_string_list_get_string (GTK_STRING_LIST (self->model), selected);
  mountpoint = gtk_editable_get_text (GTK_EDITABLE (self->mount_point_row));
  fstype = gtk_editable_get_text (GTK_EDITABLE (self->filesystem_type_row));
  mountopts = gtk_editable_get_text (GTK_EDITABLE (self->mount_options_row));

  if (self->orig_fstab_entry != NULL)
    {
      g_variant_lookup (self->orig_fstab_entry, "freq", "i", &freq);
      g_variant_lookup (self->orig_fstab_entry, "passno", "i", &passno);
    }

  g_variant_builder_init (&variant_builder, G_VARIANT_TYPE_VARDICT);
  if (fsname)
    g_variant_builder_add (&variant_builder, "{sv}", "fsname", g_variant_new_bytestring (fsname));
  if (mountpoint)
    g_variant_builder_add (&variant_builder, "{sv}", "dir", g_variant_new_bytestring (mountpoint));
  if (fstype)
    g_variant_builder_add (&variant_builder, "{sv}", "type", g_variant_new_bytestring (fstype));
  if (mountopts)
    g_variant_builder_add (&variant_builder, "{sv}", "opts", g_variant_new_bytestring (mountopts));
  g_variant_builder_add (&variant_builder, "{sv}", "freq", g_variant_new_int32 (freq));
  g_variant_builder_add (&variant_builder, "{sv}", "passno", g_variant_new_int32 (passno));

  return g_variant_new ("(sa{sv})", "fstab", &variant_builder);
}

static void
gdu_mount_options_dialog_populate_device_combo_row (GduMountOptionsDialog  *self)
{
  gchar *fstab_device = NULL;
  const gchar *item;
  const gchar *const *symlinks;
  guint n;
  guint is_removable;
  gint by_id = -1;
  gint by_uuid = -1;
  gint by_path = -1;
  gint by_label = -1;
  gint selected = -1;

  item = udisks_block_get_device (self->block);
  gtk_string_list_append (GTK_STRING_LIST (self->model),
                          item);

  symlinks = udisks_block_get_symlinks (self->block);
  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
    {
      if (g_str_has_prefix (symlinks[n], "/dev/disk/by-uuid"))
        by_uuid = g_list_model_get_n_items (G_LIST_MODEL (self->model));
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-label"))
        by_label = g_list_model_get_n_items (G_LIST_MODEL (self->model));
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-id"))
        by_id = g_list_model_get_n_items (G_LIST_MODEL (self->model));
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-path"))
        by_path = g_list_model_get_n_items (G_LIST_MODEL (self->model));

      gtk_string_list_append (GTK_STRING_LIST (self->model),
                              symlinks[n]);
    }

  item = udisks_block_get_id_uuid (self->block);
  if (item != NULL && strlen (item) > 0)
    {
      gtk_string_list_append (GTK_STRING_LIST (self->model),
                              g_strdup_printf ("UUID=%s", item));
    }

  item = udisks_block_get_id_label (self->block);
  if (item != NULL && strlen (item) > 0)
    {
      gtk_string_list_append (GTK_STRING_LIST (self->model),
                              g_strdup_printf ("LABEL=%s", item));
    }

  if (self->orig_fstab_entry != NULL)
    {
      g_variant_lookup (self->orig_fstab_entry, "fsname", "^ay", &fstab_device);
      for (n = g_list_model_get_n_items (self->model) - 1; n >= 0; n--)
        {
          item = gtk_string_list_get_string (GTK_STRING_LIST (self->model), n);
          if (g_strcmp0 (fstab_device, item) == 0)
            {
              selected = n;
              break;
            }
        }
    }
  else
    {
      /* Choose a device to default if creating a new entry */
      /* if the device is using removable media, prefer
       * by-id / by-path to by-uuid / by-label
       */
      int order[2][4] = {
        {by_id, by_path, by_uuid, by_label},  /* for removable */
        {by_uuid, by_label, by_id, by_path}   /* for non-removable */
      };
      is_removable = gdu_mount_options_dialog_is_removable(self) ? 0 : 1;

      for (n = 0; n < G_N_ELEMENTS (order[is_removable]); n++)
        {
          if (order[is_removable][n] != -1)
            {
              selected = order[is_removable][n];
              break;
            }
        }
    }

  /* Fall back to device name as a last resort */
  if (selected == -1)
    {
      selected = 0;
    }

  adw_combo_row_set_selected (ADW_COMBO_ROW (self->device_combo_row), selected);
}

static void
gdu_mount_options_dialog_populate (GduMountOptionsDialog *self)
{
  const gchar *mount_point;
  const gchar *filesystem;
  const gchar *mount_options;

  if (self->orig_fstab_entry != NULL)
    {
      g_variant_lookup (self->orig_fstab_entry, "dir", "^&ay", &mount_point);
      g_variant_lookup (self->orig_fstab_entry, "type", "^&ay", &filesystem);
      g_variant_lookup (self->orig_fstab_entry, "opts", "^&ay", &mount_options);
    }
  else if (gdu_mount_options_dialog_is_swap (self))
    {
      mount_point = "none";
      filesystem = "swap";
      mount_options = "sw";
    }
  else
    {
      mount_point = "";
      filesystem = "auto";
      mount_options = "nosuid,nodev,nofail,x-gvfs-show";
      /* propose noauto if the media is removable - otherwise e.g. systemd will time out at boot */
      if (gdu_mount_options_dialog_is_removable (self))
        {
          mount_options = "nosuid,nodev,nofail,noauto,x-gvfs-show";
        }
    }

  gtk_editable_set_text (GTK_EDITABLE (self->mount_point_row), mount_point);
  gtk_editable_set_text (GTK_EDITABLE (self->mount_options_row), mount_options);
  gtk_editable_set_text (GTK_EDITABLE (self->filesystem_type_row), filesystem);

  adw_banner_set_revealed (ADW_BANNER (self->info_banner), check_if_system_mount (mount_point));
}

static void
on_update_configuration_item_cb (GObject      *source_object,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  GduMountOptionsDialog *self = GDU_MOUNT_OPTIONS_DIALOG (user_data);
  UDisksBlock *block = UDISKS_BLOCK (source_object);
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_update_configuration_item_finish(block, res, &error))
    {
      if (!g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
        gdu_utils_show_error (gdu_mount_options_dialog_get_window (self),
                              _("Error updating /etc/fstab entry"),
                              error);
      gtk_widget_set_visible (self->spinner, FALSE);
    }
  else
    {
      adw_dialog_close (ADW_DIALOG (self));
    }
}

static void
on_add_configuration_item_cb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  GduMountOptionsDialog *self = GDU_MOUNT_OPTIONS_DIALOG (user_data);
  UDisksBlock *block = UDISKS_BLOCK (source_object);
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_add_configuration_item_finish(block, res, &error))
    {
      if (!g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
        gdu_utils_show_error (gdu_mount_options_dialog_get_window (self),
                              _("Error adding new /etc/fstab entry"),
                              error);
      gtk_widget_set_visible (self->spinner, FALSE);
    }
  else
    {
      adw_dialog_close (ADW_DIALOG (self));
    }
}

static void
on_remove_configuration_item_cb (GObject      *source_object,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  GduMountOptionsDialog *self = GDU_MOUNT_OPTIONS_DIALOG (user_data);
  UDisksBlock *block = UDISKS_BLOCK (source_object);
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_remove_configuration_item_finish(block, res, &error))
    {
      if (!g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
        gdu_utils_show_error (gdu_mount_options_dialog_get_window (self),
                              _("Error removing old /etc/fstab entry"),
                              error);
      gtk_widget_set_visible (self->spinner, FALSE);
    }
  else
    {
      adw_dialog_close (ADW_DIALOG (self));
    }
}

static void
on_done_clicked_cb (GduMountOptionsDialog *self)
{
  gboolean configured;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) old_fstab = NULL;
  g_autoptr(GVariant) new_fstab = NULL;

  configured = !adw_switch_row_get_active (ADW_SWITCH_ROW (self->automount_switch_row));
  gtk_widget_set_visible (self->spinner, TRUE);

  if (self->orig_fstab_entry != NULL && !configured)
    {
      udisks_block_call_remove_configuration_item(self->block,
                                                  g_variant_new ("(s@a{sv})", "fstab", self->orig_fstab_entry),
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* GCancellable */
                                                  on_remove_configuration_item_cb,
                                                  self);
      return;
    }

    new_fstab = gdu_mount_options_dialog_get_new_fstab (self);
    if (self->orig_fstab_entry != NULL)
      old_fstab = g_variant_new ("(s@a{sv})", "fstab", self->orig_fstab_entry);

    if (old_fstab == NULL && new_fstab != NULL)
      {
        udisks_block_call_add_configuration_item(self->block,
                                                 new_fstab,
                                                 g_variant_new ("a{sv}", NULL), /* options */
                                                 NULL, /* GCancellable */
                                                 on_add_configuration_item_cb,
                                                 self);
        return;
      }

    udisks_block_call_update_configuration_item (self->block,
                                                old_fstab,
                                                new_fstab,
                                                g_variant_new ("a{sv}", NULL), /* options */
                                                NULL, /* GCancellable */
                                                on_update_configuration_item_cb,
                                                self);
}

static void
on_restore_default_clicked_cb (GduMountOptionsDialog *self)
{
  adw_switch_row_set_active (ADW_SWITCH_ROW (self->automount_switch_row), TRUE);
}

static gboolean
update_warning_css (GtkWidget *widget)
{
  AdwEntryRow *row = ADW_ENTRY_ROW (widget);
  gboolean is_empty;

  is_empty = adw_entry_row_get_text_length (row) == 0;
  if (is_empty)
    gtk_widget_add_css_class (widget, "warning");
  else
    gtk_widget_remove_css_class (widget, "warning");

  return !is_empty;
}

static void
on_property_changed (GtkWidget   *widget,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  GduMountOptionsDialog *self = user_data;
  gboolean use_manual_mount_options;
  gboolean has_fstab_entry;
  gboolean can_ok;
  g_autoptr(GVariant) new_fstab = NULL;
  g_autoptr(GVariant) new_fstab_dict = NULL;

  has_fstab_entry = (self->orig_fstab_entry != NULL);
  use_manual_mount_options = !adw_switch_row_get_active (ADW_SWITCH_ROW (self->automount_switch_row));

  g_object_freeze_notify (G_OBJECT (self->mount_options_row));
  gdu_options_update_check_option (self->mount_options_row, "noauto", widget, self->startup_mount_switch, TRUE, FALSE);
  gdu_options_update_check_option (self->mount_options_row, "x-udisks-auth", widget, self->require_auth_switch, FALSE, FALSE);
  gdu_options_update_check_option (self->mount_options_row, "x-gvfs-show", widget, self->show_in_files_switch, FALSE, FALSE);
  gdu_options_update_entry_option (self->mount_options_row, "x-gvfs-name=", widget, self->name_row);
  gdu_options_update_entry_option (self->mount_options_row, "x-gvfs-icon=", widget, self->icon_row);
  gdu_options_update_entry_option (self->mount_options_row, "x-gvfs-symbolic-icon=", widget, self->symbolic_icon_row);
  g_object_thaw_notify (G_OBJECT (self->mount_options_row));

  can_ok = (has_fstab_entry && !use_manual_mount_options)
        || (!has_fstab_entry && use_manual_mount_options);

  if (has_fstab_entry && use_manual_mount_options)
    {
      new_fstab = gdu_mount_options_dialog_get_new_fstab (self);
      new_fstab_dict = g_variant_get_child_value(new_fstab, 1);
      can_ok = !g_variant_equal (self->orig_fstab_entry, new_fstab_dict);
    }

  /* These three rows are required fields */
  if (use_manual_mount_options)
    {
      can_ok &= update_warning_css (self->mount_options_row)
             && update_warning_css (self->mount_point_row)
             && update_warning_css (self->filesystem_type_row);
    }

  gtk_widget_set_sensitive (self->save_button, can_ok);
}

static void
fstab_on_device_combo_row_changed (GtkWidget             *widget,
                                   GParamSpec            *pspec,
                                   GduMountOptionsDialog *self)
{
  if (!gdu_mount_options_dialog_is_swap(self))
    {
      GtkStringObject *fsname_obj;
      const gchar *fsname;
      g_autofree gchar *proposed_mount_point = NULL;
      const gchar *s;

      fsname_obj = adw_combo_row_get_selected_item (ADW_COMBO_ROW (self->device_combo_row));
      if (!fsname_obj)
        return;

      fsname = gtk_string_object_get_string (fsname_obj);
      s = strrchr (fsname, '/');
      if (s == NULL)
        s = strrchr (fsname, '=');
      if (s == NULL)
        s = "/disk";
      proposed_mount_point = g_strdup_printf ("/mnt/%s", s + 1);

      gtk_editable_set_text (GTK_EDITABLE (self->mount_point_row), proposed_mount_point);
  }

  on_property_changed (widget, pspec, self);
}

static void
gdu_mount_options_dialog_finalize (GObject *object)
{
  G_OBJECT_CLASS (gdu_mount_options_dialog_parent_class)->finalize (object);
}

void
gdu_mount_options_dialog_class_init (GduMountOptionsDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_mount_options_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-mount-options-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, info_banner);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, overlay);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, spinner);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, save_button);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, automount_switch_row);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, startup_mount_switch);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, show_in_files_switch);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, require_auth_switch);

  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, name_row);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, icon_row);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, symbolic_icon_row);

  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, mount_point_row);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, mount_options_row);
  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, filesystem_type_row);

  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, device_combo_row);

  gtk_widget_class_bind_template_callback (widget_class, on_done_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_property_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_restore_default_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, fstab_on_device_combo_row_changed);
}

void
gdu_mount_options_dialog_init (GduMountOptionsDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->model = G_LIST_MODEL (gtk_string_list_new (NULL));
  adw_combo_row_set_model (ADW_COMBO_ROW (self->device_combo_row),
                           G_LIST_MODEL (self->model));
}

void
gdu_mount_options_dialog_show (GtkWindow    *parent_window,
                               UDisksObject *object,
                               UDisksClient *client)
{
  GduMountOptionsDialog *self;

  self = g_object_new (GDU_TYPE_MOUNT_OPTIONS_DIALOG, NULL);

  self->client = client;
  self->object = g_object_ref (object);
  self->block = udisks_object_get_block (self->object);
  g_assert (self->block != NULL);

  self->orig_fstab_entry = gdu_mount_options_dialog_get_fstab (self);

  adw_switch_row_set_active (ADW_SWITCH_ROW (self->automount_switch_row), self->orig_fstab_entry == NULL);

  if (gdu_mount_options_dialog_is_swap (self))
    {
      gtk_widget_set_sensitive (self->mount_options_row, FALSE);
      gtk_widget_set_sensitive (self->show_in_files_switch, FALSE);
      gtk_widget_set_sensitive (self->name_row, FALSE);
      gtk_widget_set_sensitive (self->icon_row, FALSE);
      gtk_widget_set_sensitive (self->symbolic_icon_row, FALSE);
    }

  gdu_mount_options_dialog_populate (self);
  gdu_mount_options_dialog_populate_device_combo_row (self);

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
