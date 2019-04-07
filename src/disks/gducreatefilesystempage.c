/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gducreatefilesystempage.h"

struct _GduCreateFilesystemPage
{
  GtkGrid parent_instance;
};

typedef struct _GduCreateFilesystemPagePrivate GduCreateFilesystemPagePrivate;

struct _GduCreateFilesystemPagePrivate
{
  GtkEntry *name_entry;
  GtkSwitch *erase_switch;
  GtkRadioButton *internal_radiobutton;
  GtkCheckButton *internal_encrypt_checkbutton;
  GtkRadioButton *windows_radiobutton;
  GtkRadioButton *all_radiobutton;
  GtkRadioButton *other_radiobutton;

  UDisksClient *client;
  UDisksDrive *drive;
  UDisksObject *object;
};

enum
{
  PROP_0,
  PROP_COMPLETE
};

G_DEFINE_TYPE_WITH_PRIVATE (GduCreateFilesystemPage, gdu_create_filesystem_page, GTK_TYPE_GRID);

static void
gdu_create_filesystem_page_init (GduCreateFilesystemPage *page)
{
  gtk_widget_init_template (GTK_WIDGET (page));
}

static void
gdu_create_filesystem_page_get_property (GObject    *object,
                                         guint       property_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, TRUE);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_filesystem_page_class_init (GduCreateFilesystemPageClass *class)
{
  GObjectClass *gobject_class;

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (class),
                                               "/org/gnome/Disks/ui/create-filesystem-page.ui");
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateFilesystemPage, name_entry);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateFilesystemPage, erase_switch);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateFilesystemPage, internal_radiobutton);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateFilesystemPage, internal_encrypt_checkbutton);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateFilesystemPage, windows_radiobutton);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateFilesystemPage, all_radiobutton);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateFilesystemPage, other_radiobutton);

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = gdu_create_filesystem_page_get_property;
  g_object_class_install_property (gobject_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete", NULL, NULL,
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

const gchar *
gdu_create_filesystem_page_get_name (GduCreateFilesystemPage *page)
{
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  return gtk_entry_get_text (priv->name_entry);
}

gboolean
gdu_create_filesystem_page_is_other (GduCreateFilesystemPage *page)
{
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->other_radiobutton));
}

const gchar *
gdu_create_filesystem_page_get_fs (GduCreateFilesystemPage *page)
{
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->internal_radiobutton)))
    return "ext4";
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->windows_radiobutton)))
    return "ntfs";
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->all_radiobutton)))
    return "vfat";
  else
    return NULL;
}

gboolean
gdu_create_filesystem_page_is_encrypted (GduCreateFilesystemPage *page)
{
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->internal_radiobutton)) &&
         gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->internal_encrypt_checkbutton));
}

const gchar *
gdu_create_filesystem_page_get_erase (GduCreateFilesystemPage *page)
{
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  if (gtk_switch_get_active (priv->erase_switch))
    return "zero";
  else /* TODO: "ata-secure-erase", "ata-secure-erase-enhanced" */
    return NULL;
}

static void
on_fs_name_changed (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GduCreateFilesystemPage *page = GDU_CREATE_FILESYSTEM_PAGE (user_data);
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  _gtk_entry_buffer_truncate_bytes (gtk_entry_get_buffer (priv->name_entry),
                                    gdu_utils_get_max_label_length (gdu_create_filesystem_page_get_fs (page)));
}

static void
on_fs_type_changed (GtkToggleButton *object, gpointer user_data)
{
  GduCreateFilesystemPage *page = GDU_CREATE_FILESYSTEM_PAGE (user_data);
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  _gtk_entry_buffer_truncate_bytes (gtk_entry_get_buffer (priv->name_entry),
                                    gdu_utils_get_max_label_length (gdu_create_filesystem_page_get_fs (page)));
  g_object_notify (G_OBJECT (page), "complete");
}

void
gdu_create_filesystem_page_fill_name (GduCreateFilesystemPage *page, guint64 size_info)
{
  GduCreateFilesystemPagePrivate *priv;
  gchar *size_display;
  UDisksObjectInfo *info;
  gchar *identification;
  gchar *name;

  /* Suggest an appropriate name for the filesystem, combining an identifier and the size.
   * Start with the vendor, and if not available, fallback to the model or the UDisks object name.
   * Take only the first part of the model. If no model information is available, look at
   * the UDisks object name, which might be a path to a device mapper mount point or a loopback
   * file, and just take the last part (only until a delimiter is observed).
   */
  priv = gdu_create_filesystem_page_get_instance_private (page);
  size_display = udisks_client_get_size_for_display (priv->client, size_info, FALSE, FALSE);
  info = udisks_client_get_object_info (priv->client, priv->object);
  identification = g_strdup (priv->drive != NULL ? udisks_drive_get_vendor (priv->drive) : "");
  if (identification == NULL || strlen (identification) == 0)
    {
      gchar **maybe_long_name;

      maybe_long_name = g_strsplit_set (priv->drive != NULL ? udisks_drive_get_model (priv->drive) : "", "_- /", 0);
      if (maybe_long_name != NULL)
        {
          g_free (identification);
          identification = g_strdup (maybe_long_name[0]);
        }

      g_strfreev (maybe_long_name);
    }
  if (identification == NULL || strlen (identification) == 0)
    {
      gchar **maybe_path_name;

      maybe_path_name = g_strsplit (udisks_object_info_get_name (info), "/", 0);
      if (maybe_path_name != NULL)
        {
          gchar **maybe_uuid;

          maybe_uuid = g_strsplit_set (maybe_path_name[g_strv_length (maybe_path_name) - 1], "-_ ", 0);
          if (maybe_uuid != NULL)
            {
              g_free (identification);
              identification = g_strdup (maybe_uuid[0]);
            }

          g_strfreev (maybe_uuid);
        }

      g_strfreev (maybe_path_name);
    }

  name = g_strdup_printf ("%s %s", identification, size_display); /* No translation needed. */
  gtk_entry_set_text (priv->name_entry, name);
  _gtk_entry_buffer_truncate_bytes (gtk_entry_get_buffer (priv->name_entry),
                                    gdu_utils_get_max_label_length (gdu_create_filesystem_page_get_fs (page)));

  g_free (size_display);
  g_free (identification);
  g_free (name);
  g_object_unref (info);
}

GduCreateFilesystemPage *
gdu_create_filesystem_page_new (UDisksClient *client, gboolean show_custom, UDisksDrive *drive, UDisksObject *object)
{
  GduCreateFilesystemPage *page;
  GduCreateFilesystemPagePrivate *priv;

  page = g_object_new (GDU_TYPE_CREATE_FILESYSTEM_PAGE, NULL);
  priv = gdu_create_filesystem_page_get_instance_private (page);
  priv->client = client;
  priv->drive = drive;
  priv->object = object;
  g_signal_connect (priv->name_entry, "notify::text", G_CALLBACK (on_fs_name_changed), page);
  g_signal_connect (priv->internal_encrypt_checkbutton, "toggled", G_CALLBACK (on_fs_type_changed), page);
  g_signal_connect (priv->internal_radiobutton, "toggled", G_CALLBACK (on_fs_type_changed), page);
  g_signal_connect (priv->windows_radiobutton, "toggled", G_CALLBACK (on_fs_type_changed), page);
  g_signal_connect (priv->all_radiobutton, "toggled", G_CALLBACK (on_fs_type_changed), page);
  g_signal_connect (priv->other_radiobutton, "toggled", G_CALLBACK (on_fs_type_changed), page);

  g_object_bind_property (priv->internal_radiobutton, "active", priv->internal_encrypt_checkbutton, "sensitive", G_BINDING_SYNC_CREATE);

  if (!show_custom)
    gtk_widget_hide (GTK_WIDGET (priv->other_radiobutton));

  /* Default to FAT or NTFS for removable drives... */
  if (drive != NULL && udisks_drive_get_removable (drive))
    {
      /* default FAT for flash and disks/media smaller than 20G (assumed to be flash cards) */
      if (gdu_utils_is_flash (drive) ||
          udisks_drive_get_size (drive) < 20UL * 1000UL*1000UL*1000UL ||
          !gdu_utils_is_ntfs_available (client)
          )
        {
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->all_radiobutton), TRUE);
        }
      else
        {
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->windows_radiobutton), TRUE);
        }
    }

  gtk_widget_set_sensitive (GTK_WIDGET (priv->windows_radiobutton), gdu_utils_is_ntfs_available (client));

  return page;
}
