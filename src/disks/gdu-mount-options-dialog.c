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
  AdwWindow      parent_instance;

  GtkWidget     *info_box;

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

G_DEFINE_TYPE (GduMountOptionsDialog, gdu_mount_options_dialog, ADW_TYPE_WINDOW)

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

static gboolean
gdu_mount_options_dialog_is_swap (GduMountOptionsDialog *self)
{
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

  if (gdu_mount_options_dialog_is_swap (self))
    {
      mount_point = "none";
      filesystem = "swap";
      mount_options = "sw";
    }

  gtk_editable_set_text (GTK_EDITABLE (self->mount_point_row), mount_point);
  gtk_editable_set_text (GTK_EDITABLE (self->mount_options_row), mount_options);
  gtk_editable_set_text (GTK_EDITABLE (self->filesystem_type_row), filesystem);

  adw_banner_set_revealed (ADW_BANNER (self->info_box), check_if_system_mount (mount_point));
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

  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, info_box);
  
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

  gtk_widget_class_bind_template_child (widget_class, GduMountOptionsDialog, reset_settings_button);
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

  self = g_object_new (GDU_TYPE_MOUNT_OPTIONS_DIALOG,
                       "transient-for", parent_window,
                       NULL);

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

  gtk_window_present (GTK_WINDOW (self));
}
