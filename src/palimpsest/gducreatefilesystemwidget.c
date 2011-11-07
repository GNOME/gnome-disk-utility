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
#include <glib/gi18n-lib.h>

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <stdlib.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gducreatefilesystemwidget.h"

typedef struct _GduCreateFilesystemWidgetClass GduCreateFilesystemWidgetClass;
struct _GduCreateFilesystemWidget
{
  GtkVBox parent;

  GduApplication *application;
  UDisksDrive *drive;

  GtkBuilder *builder;
  GtkWidget *grid;
  GtkWidget *type_combobox;
  GtkWidget *name_entry;
  GtkWidget *filesystem_label;
  GtkWidget *filesystem_entry;
  GtkWidget *passphrase_label;
  GtkWidget *passphrase_entry;
  GtkWidget *confirm_passphrase_label;
  GtkWidget *confirm_passphrase_entry;
  GtkWidget *show_passphrase_checkbutton;

  gchar *fstype;
  gchar *name;
  gchar *passphrase;
  gboolean has_info;
};

struct _GduCreateFilesystemWidgetClass
{
  GtkVBoxClass parent_class;
};

enum
{
  PROP_0,
  PROP_APPLICATION,
  PROP_DRIVE,
  PROP_FSTYPE,
  PROP_NAME,
  PROP_PASSPHRASE,
  PROP_HAS_INFO
};

G_DEFINE_TYPE (GduCreateFilesystemWidget, gdu_create_filesystem_widget, GTK_TYPE_VBOX)

static void
gdu_create_filesystem_widget_finalize (GObject *object)
{
  GduCreateFilesystemWidget *widget = GDU_CREATE_FILESYSTEM_WIDGET (object);

  g_object_unref (widget->application);
  g_clear_object (&widget->drive);
  g_free (widget->fstype);
  g_free (widget->name);
  g_free (widget->passphrase);

  G_OBJECT_CLASS (gdu_create_filesystem_widget_parent_class)->finalize (object);
}

static void
gdu_create_filesystem_widget_get_property (GObject    *object,
                                           guint       property_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  GduCreateFilesystemWidget *widget = GDU_CREATE_FILESYSTEM_WIDGET (object);

  switch (property_id)
    {
    case PROP_APPLICATION:
      g_value_set_object (value, widget->application);
      break;

    case PROP_DRIVE:
      g_value_set_object (value, widget->drive);
      break;

    case PROP_FSTYPE:
      g_value_set_string (value, widget->fstype);
      break;

    case PROP_NAME:
      g_value_set_string (value, widget->name);
      break;

    case PROP_PASSPHRASE:
      g_value_set_string (value, widget->passphrase);
      break;

    case PROP_HAS_INFO:
      g_value_set_boolean (value, widget->has_info);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_filesystem_widget_set_property (GObject      *object,
                                           guint         property_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  GduCreateFilesystemWidget *widget = GDU_CREATE_FILESYSTEM_WIDGET (object);

  switch (property_id)
    {
    case PROP_APPLICATION:
      widget->application = g_value_dup_object (value);
      break;

    case PROP_DRIVE:
      widget->drive = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update (GduCreateFilesystemWidget *widget)
{
  gboolean show_filesystem_widgets = FALSE;
  gboolean show_passphrase_widgets = FALSE;
  gboolean has_info = FALSE;
  const gchar *fstype = NULL;
  const gchar *name = NULL;
  const gchar *passphrase = NULL;

  name = gtk_entry_get_text (GTK_ENTRY (widget->name_entry));
  passphrase = gtk_entry_get_text (GTK_ENTRY (widget->passphrase_entry));

  switch (gtk_combo_box_get_active (GTK_COMBO_BOX (widget->type_combobox)))
    {
    case 0:
      fstype = "vfat";
      has_info = TRUE;
      break;

    case 1:
      fstype = "ntfs";
      has_info = TRUE;
      break;

    case 2:
      fstype = "ext4";
      has_info = TRUE;
      break;

    case 3:
      fstype = "ext4";
      /* Encrypted, compatible with Linux (LUKS + ext4) */
      show_passphrase_widgets = TRUE;
      if (strlen (gtk_entry_get_text (GTK_ENTRY (widget->passphrase_entry))) > 0)
        {
          if (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (widget->passphrase_entry)),
                         gtk_entry_get_text (GTK_ENTRY (widget->confirm_passphrase_entry))) == 0)
            {
              has_info = TRUE;
            }
        }
      break;

    case 4:
      /* Custom */
      show_filesystem_widgets = TRUE;
      if (strlen (gtk_entry_get_text (GTK_ENTRY (widget->filesystem_entry))) > 0)
        {
          fstype = gtk_entry_get_text (GTK_ENTRY (widget->filesystem_entry));
          /* TODO: maybe validate we know how to create this FS?
           * And also make "Name" + its entry insensitive if it doesn't support labels?
           */
          has_info = TRUE;
        }
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  if (show_filesystem_widgets)
    {
      gtk_widget_show (widget->filesystem_label);
      gtk_widget_show (widget->filesystem_entry);
    }
  else
    {
      gtk_widget_hide (widget->filesystem_label);
      gtk_widget_hide (widget->filesystem_entry);
    }

  if (show_passphrase_widgets)
    {
      gtk_widget_show (widget->passphrase_label);
      gtk_widget_show (widget->passphrase_entry);
      gtk_widget_show (widget->confirm_passphrase_label);
      gtk_widget_show (widget->confirm_passphrase_entry);
      gtk_widget_show (widget->show_passphrase_checkbutton);
    }
  else
    {
      gtk_widget_hide (widget->passphrase_label);
      gtk_widget_hide (widget->passphrase_entry);
      gtk_widget_hide (widget->confirm_passphrase_label);
      gtk_widget_hide (widget->confirm_passphrase_entry);
      gtk_widget_hide (widget->show_passphrase_checkbutton);
    }

  /* update local widget state for our users */
  g_object_freeze_notify (G_OBJECT (widget));
  if (g_strcmp0 (widget->fstype, fstype) != 0)
    {
      g_free (widget->fstype);
      widget->fstype = g_strdup (fstype);
      g_object_notify (G_OBJECT (widget), "fstype");
    }
  if (g_strcmp0 (widget->name, name) != 0)
    {
      g_free (widget->name);
      widget->name = g_strdup (name);
      g_object_notify (G_OBJECT (widget), "name");
    }
  if (g_strcmp0 (widget->passphrase, passphrase) != 0)
    {
      g_free (widget->passphrase);
      widget->passphrase = g_strdup (passphrase);
      g_object_notify (G_OBJECT (widget), "passphrase");
    }
  if (widget->has_info != has_info)
    {
      widget->has_info = has_info;
      g_object_notify (G_OBJECT (widget), "has-info");
    }
  g_object_thaw_notify (G_OBJECT (widget));
}

static void
on_property_changed (GObject     *object,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  GduCreateFilesystemWidget *widget = GDU_CREATE_FILESYSTEM_WIDGET (user_data);
  update (widget);
}


static gboolean
is_flash (UDisksDrive *drive)
{
  gboolean ret = FALSE;
  guint n;
  const gchar *const *media_compat;

  media_compat = udisks_drive_get_media_compatibility (drive);
  for (n = 0; media_compat != NULL && media_compat[n] != NULL; n++)
    {
      if (g_str_has_prefix (media_compat[n], "flash"))
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

static void
set_defaults (GduCreateFilesystemWidget *widget)
{
  /* Default to FAT or NTFS for removable drives... Ext4 otherwise */
  if (widget->drive != NULL && udisks_drive_get_removable (widget->drive))
    {
      /* default FAT for flash and disks/media smaller than 20G (assumed to be flash cards) */
      if (is_flash (widget->drive) || udisks_drive_get_size (widget->drive) < 20L * 1000L*1000L*1000L)
        {
          gtk_combo_box_set_active (GTK_COMBO_BOX (widget->type_combobox), 0); /* FAT */
        }
      else
        {
          gtk_combo_box_set_active (GTK_COMBO_BOX (widget->type_combobox), 1); /* NTFS */
        }
    }
  else
    {
      gtk_combo_box_set_active (GTK_COMBO_BOX (widget->type_combobox), 2); /* Ext4 */
    }

  /* Translators: this is the default name for the filesystem */
  gtk_entry_set_text (GTK_ENTRY (widget->name_entry), _("New Volume"));

  /* Set 'btrfs' for the custom filesystem */
  gtk_entry_set_text (GTK_ENTRY (widget->filesystem_entry), "btrfs");

  g_object_bind_property (widget->show_passphrase_checkbutton,
                          "active",
                          widget->passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (widget->show_passphrase_checkbutton,
                          "active",
                          widget->confirm_passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);
}

static void
gdu_create_filesystem_widget_constructed (GObject *object)
{
  GduCreateFilesystemWidget *widget = GDU_CREATE_FILESYSTEM_WIDGET (object);
  GtkWidget *dummy_window;

  dummy_window = gdu_application_new_widget (widget->application,
                                             "filesystem-create.ui",
                                             "filesystem-create-dummywindow",
                                             &widget->builder);
  widget->grid = GTK_WIDGET (gtk_builder_get_object (widget->builder, "filesystem-create-grid"));
  widget->type_combobox = GTK_WIDGET (gtk_builder_get_object (widget->builder, "type-combobox"));
  g_signal_connect (widget->type_combobox, "notify::active", G_CALLBACK (on_property_changed), widget);
  widget->name_entry = GTK_WIDGET (gtk_builder_get_object (widget->builder, "name-entry"));
  g_signal_connect (widget->name_entry, "notify::text", G_CALLBACK (on_property_changed), widget);
  widget->filesystem_label = GTK_WIDGET (gtk_builder_get_object (widget->builder, "filesystem-label"));
  widget->filesystem_entry = GTK_WIDGET (gtk_builder_get_object (widget->builder, "filesystem-entry"));
  g_signal_connect (widget->filesystem_entry, "notify::text", G_CALLBACK (on_property_changed), widget);
  widget->passphrase_label = GTK_WIDGET (gtk_builder_get_object (widget->builder, "passphrase-label"));
  widget->passphrase_entry = GTK_WIDGET (gtk_builder_get_object (widget->builder, "passphrase-entry"));
  g_signal_connect (widget->passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), widget);
  widget->confirm_passphrase_label = GTK_WIDGET (gtk_builder_get_object (widget->builder, "confirm-passphrase-label"));
  widget->confirm_passphrase_entry = GTK_WIDGET (gtk_builder_get_object (widget->builder, "confirm-passphrase-entry"));
  g_signal_connect (widget->confirm_passphrase_entry, "notify::text", G_CALLBACK (on_property_changed), widget);
  widget->show_passphrase_checkbutton = GTK_WIDGET (gtk_builder_get_object (widget->builder, "show-passphrase-checkbutton"));
  g_signal_connect (widget->show_passphrase_checkbutton, "notify::active", G_CALLBACK (on_property_changed), widget);

  /* reparent and nuke the dummy window */
  gtk_widget_reparent (widget->grid, GTK_WIDGET (widget));
  gtk_widget_destroy (dummy_window);

  set_defaults (widget);
  update (widget);

  if (G_OBJECT_CLASS (gdu_create_filesystem_widget_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_create_filesystem_widget_parent_class)->constructed (object);
}

static void
gdu_create_filesystem_widget_class_init (GduCreateFilesystemWidgetClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = gdu_create_filesystem_widget_get_property;
  gobject_class->set_property = gdu_create_filesystem_widget_set_property;
  gobject_class->finalize     = gdu_create_filesystem_widget_finalize;
  gobject_class->constructed  = gdu_create_filesystem_widget_constructed;

  g_object_class_install_property (gobject_class, PROP_APPLICATION,
                                   g_param_spec_object ("application", NULL, NULL,
                                                        GDU_TYPE_APPLICATION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DRIVE,
                                   g_param_spec_object ("drive", NULL, NULL,
                                                        UDISKS_TYPE_DRIVE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FSTYPE,
                                   g_param_spec_string ("fstype", NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NAME,
                                   g_param_spec_string ("name", NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PASSPHRASE,
                                   g_param_spec_string ("passphrase", NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HAS_INFO,
                                   g_param_spec_boolean ("has-info", NULL, NULL,
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

static void
gdu_create_filesystem_widget_init (GduCreateFilesystemWidget *widget)
{
}

GtkWidget *
gdu_create_filesystem_widget_new (GduApplication *application,
                                  UDisksDrive    *drive)
{
  g_return_val_if_fail (GDU_IS_APPLICATION (application), NULL);
  return GTK_WIDGET (g_object_new (GDU_TYPE_CREATE_FILESYSTEM_WIDGET,
                                   "application", application,
                                   "drive", drive,
                                   NULL));
}

const gchar *
gdu_create_filesystem_widget_get_name (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), NULL);
  return widget->name;
}

const gchar *
gdu_create_filesystem_widget_get_fstype (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), NULL);
  return widget->fstype;
}

const gchar *
gdu_create_filesystem_widget_get_passphrase (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), NULL);
  return widget->passphrase;
}

gboolean
gdu_create_filesystem_widget_get_has_info (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), FALSE);
  return widget->has_info;
}

GtkWidget *
gdu_create_filesystem_widget_get_name_entry (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), NULL);
  return widget->name_entry;
}
