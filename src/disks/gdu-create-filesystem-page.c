/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gdu-create-filesystem-page.h"

enum
{
  PROP_0,
  PROP_COMPLETE,
  PROP_FS_TYPE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

struct _GduCreateFilesystemPage
{
  AdwBin     parent_instance;

  GtkWidget *name_entry;
  GtkWidget *erase_switch;
  GtkWidget *ext4_checkbutton;
  GtkWidget *encrypt_switch;
  GtkWidget *ntfs_checkbutton;
  GtkWidget *fat_checkbutton;

  GduFsType  fs_type;
};

G_DEFINE_TYPE (GduCreateFilesystemPage, gdu_create_filesystem_page, ADW_TYPE_BIN);

G_DEFINE_ENUM_TYPE (GduFsType, gdu_fs_type,
                    G_DEFINE_ENUM_VALUE (GDU_FS_TYPE_EXT4,  "ext4"),
                    G_DEFINE_ENUM_VALUE (GDU_FS_TYPE_NTFS,  "ntfs"),
                    G_DEFINE_ENUM_VALUE (GDU_FS_TYPE_FAT,   "vfat"),
                    G_DEFINE_ENUM_VALUE (GDU_FS_TYPE_OTHER, "other"));

const gchar *
gdu_create_filesystem_page_get_name (GduCreateFilesystemPage *self)
{
  return gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
}

gboolean
gdu_create_filesystem_page_is_other (GduCreateFilesystemPage *self)
{
  return self->fs_type == GDU_FS_TYPE_OTHER;
}

const gchar *
gdu_create_filesystem_page_get_fs (GduCreateFilesystemPage *self)
{
  GEnumClass *eclass;
  GEnumValue *value;

  eclass = G_ENUM_CLASS (g_type_class_peek (GDU_TYPE_FS_TYPE));
  value = g_enum_get_value (eclass, self->fs_type);

  g_assert (value);

  return value->value_nick;
}

gboolean
gdu_create_filesystem_page_is_encrypted (GduCreateFilesystemPage *self)
{
  return gtk_check_button_get_active (GTK_CHECK_BUTTON (self->ext4_checkbutton))
         && adw_switch_row_get_active (ADW_SWITCH_ROW (self->encrypt_switch));
}

const gchar *
gdu_create_filesystem_page_get_erase (GduCreateFilesystemPage *self)
{
  if (gtk_switch_get_active (GTK_SWITCH (self->erase_switch)))
    return "zero";
  /* TODO: "ata-secure-erase", "ata-secure-erase-enhanced" */

  return NULL;
}

static void
update_text_entry (GduCreateFilesystemPage *self)
{
  GString *s;
  guint max_len;

  max_len = gdu_utils_get_max_label_length (gdu_create_filesystem_page_get_fs (self));
  s = g_string_new (gtk_editable_get_text (GTK_EDITABLE (self->name_entry)));

  g_string_truncate (s, max_len);
  gtk_editable_set_text (GTK_EDITABLE (self->name_entry),
                         g_string_free_and_steal (s));
}

static void
on_fs_name_changed (GduCreateFilesystemPage *self,
                    GtkWidget               *widget)
{
  update_text_entry (self);
}

static void
on_fs_type_changed (GduCreateFilesystemPage *self)
{
  update_text_entry (self);

  g_object_notify (G_OBJECT (self), "complete");
}

static void
gdu_create_filesystem_page_get_property (GObject    *object,
                                         guint       property_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  GduCreateFilesystemPage *self = GDU_CREATE_FILESYSTEM_PAGE (object);

  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, TRUE);
      break;
    case PROP_FS_TYPE:
      g_value_set_enum (value, self->fs_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_filesystem_page_set_property (GObject      *object, 
                                         guint         property_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  GduCreateFilesystemPage *self = GDU_CREATE_FILESYSTEM_PAGE (object);

  switch (property_id)
    {
    case PROP_FS_TYPE:
      self->fs_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gdu_create_filesystem_page_init (GduCreateFilesystemPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gdu_create_filesystem_page_class_init (GduCreateFilesystemPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gdu_create_filesystem_page_get_property;
  object_class->set_property = gdu_create_filesystem_page_set_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-create-filesystem-page.ui");

  gtk_widget_class_bind_template_child (widget_class, GduCreateFilesystemPage, name_entry);
  gtk_widget_class_bind_template_child (widget_class, GduCreateFilesystemPage, erase_switch);
  gtk_widget_class_bind_template_child (widget_class, GduCreateFilesystemPage, ext4_checkbutton);
  gtk_widget_class_bind_template_child (widget_class, GduCreateFilesystemPage, ntfs_checkbutton);
  gtk_widget_class_bind_template_child (widget_class, GduCreateFilesystemPage, fat_checkbutton);
  gtk_widget_class_bind_template_child (widget_class, GduCreateFilesystemPage, encrypt_switch);

  gtk_widget_class_bind_template_callback (widget_class, on_fs_name_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_fs_type_changed);

  properties[PROP_COMPLETE] =
    g_param_spec_boolean ("complete",
                          NULL, NULL, FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  properties[PROP_FS_TYPE] =
    g_param_spec_enum ("fs-type",
                        NULL,
                        NULL,
                        GDU_TYPE_FS_TYPE,
                        GDU_FS_TYPE_EXT4,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
  gtk_widget_class_install_property_action (widget_class, "update_fs_type", "fs-type");
}

GduCreateFilesystemPage *
gdu_create_filesystem_page_new (UDisksClient *client, UDisksDrive *drive)
{
  GduCreateFilesystemPage *self;
  char *s;
  char *missing_util;

  self = g_object_new (GDU_TYPE_CREATE_FILESYSTEM_PAGE, NULL);

  /* Default to FAT or NTFS for removable drives... */
  if (drive != NULL && udisks_drive_get_removable (drive))
    {
      /* default FAT for flash and disks/media smaller than 20G (assumed to be
       * flash cards) */
      if (gdu_utils_can_format (client, "vfat", FALSE, NULL)
          && (gdu_utils_is_flash (drive)
              || udisks_drive_get_size (drive) < 20UL * 1000UL * 1000UL * 1000UL
              || !gdu_utils_can_format (client, "ntfs", FALSE, NULL)))
        {
          self->fs_type = GDU_FS_TYPE_FAT;
        }
      else if (gdu_utils_can_format (client, "ntfs", FALSE, NULL))
        {
          self->fs_type = GDU_FS_TYPE_NTFS;
        }
    }

  if (!gdu_utils_can_format (client, "ntfs", FALSE, &missing_util))
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self->ntfs_checkbutton), FALSE);
      s = g_strdup_printf (_ ("The utility %s is missing."), missing_util);
      gtk_widget_set_tooltip_text (GTK_WIDGET (self->ntfs_checkbutton), s);

      g_free (s);
      g_free (missing_util);
    }

  if (!gdu_utils_can_format (client, "vfat", FALSE, &missing_util))
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self->fat_checkbutton), FALSE);
      s = g_strdup_printf (_ ("The utility %s is missing."), missing_util);
      gtk_widget_set_tooltip_text (GTK_WIDGET (self->fat_checkbutton), s);
      g_free (s);
      g_free (missing_util);
    }

  return self;
}
