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

  gboolean complete;
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
  GduCreateFilesystemPage *page = GDU_CREATE_FILESYSTEM_PAGE (object);
  GduCreateFilesystemPagePrivate *priv;

  priv = gdu_create_filesystem_page_get_instance_private (page);

  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, priv->complete);
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

  priv->complete = gtk_entry_get_text_length (priv->name_entry) > 0; /* require a label */
  g_object_notify (G_OBJECT (page), "complete");

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

GduCreateFilesystemPage *
gdu_create_filesystem_page_new (gboolean show_custom, UDisksDrive *drive)
{
  GduCreateFilesystemPage *page;
  GduCreateFilesystemPagePrivate *priv;

  page = g_object_new (GDU_TYPE_CREATE_FILESYSTEM_PAGE, NULL);
  priv = gdu_create_filesystem_page_get_instance_private (page);
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
          !gdu_utils_is_ntfs_available ()
          )
        {
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->all_radiobutton), TRUE);
        }
      else
        {
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->windows_radiobutton), TRUE);
        }
    }

  gtk_widget_set_sensitive (GTK_WIDGET (priv->windows_radiobutton), gdu_utils_is_ntfs_available ());

  return page;
}
