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

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <stdlib.h>

#include "gduutils.h"
#include "gduapplication.h"
#include "gduwindow.h"
#include "gducreatefilesystemwidget.h"
#include "gdupasswordstrengthwidget.h"

typedef struct _GduCreateFilesystemWidgetClass GduCreateFilesystemWidgetClass;
struct _GduCreateFilesystemWidget
{
  GtkBox parent;

  GduApplication  *application;
  UDisksDrive     *drive;
  gchar          **additional_fstypes;

  GtkBuilder *builder;
  GtkWidget *grid;
  GtkWidget *erase_combobox;
  GtkWidget *type_combobox;
  GtkWidget *name_label;
  GtkWidget *name_entry;
  GtkWidget *name_example;
  GtkWidget *filesystem_label;
  GtkWidget *filesystem_entry;
  GtkWidget *passphrase_label;
  GtkWidget *passphrase_entry;
  GtkWidget *confirm_passphrase_label;
  GtkWidget *confirm_passphrase_entry;
  GtkWidget *show_passphrase_checkbutton;
  GtkWidget *passphrase_strengh_box;
  GtkWidget *passphrase_strengh_widget;

  gchar *fstype;
  gchar *name;
  gchar *passphrase;
  gboolean encrypt;
  gboolean has_info;
};

struct _GduCreateFilesystemWidgetClass
{
  GtkBoxClass parent_class;
};

enum
{
  PROP_0,
  PROP_APPLICATION,
  PROP_DRIVE,
  PROP_ADDITIONAL_FSTYPES,
  PROP_FSTYPE,
  PROP_NAME,
  PROP_PASSPHRASE,
  PROP_ENCRYPT,
  PROP_HAS_INFO
};

enum
{
  MODEL_COLUMN_ID,
  MODEL_COLUMN_MARKUP,
  MODEL_COLUMN_SEPARATOR,
  MODEL_N_COLUMNS,
};

G_DEFINE_TYPE (GduCreateFilesystemWidget, gdu_create_filesystem_widget, GTK_TYPE_BOX)

static void
gdu_create_filesystem_widget_finalize (GObject *object)
{
  GduCreateFilesystemWidget *widget = GDU_CREATE_FILESYSTEM_WIDGET (object);

  g_object_unref (widget->application);
  g_clear_object (&widget->drive);
  g_strfreev (widget->additional_fstypes);
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

    case PROP_ADDITIONAL_FSTYPES:
      g_value_set_boxed (value, widget->additional_fstypes);
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

    case PROP_ENCRYPT:
      g_value_set_boolean (value, widget->encrypt);
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

    case PROP_ADDITIONAL_FSTYPES:
      widget->additional_fstypes = g_value_dup_boxed (value);
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
  gboolean show_name_widgets = TRUE;
  gboolean show_filesystem_widgets = FALSE;
  gboolean show_passphrase_widgets = FALSE;
  gboolean has_info = FALSE;
  gboolean encrypt = FALSE;
  const gchar *fstype = NULL;
  const gchar *name = NULL;
  const gchar *passphrase = NULL;
  const gchar *id;

  gtk_entry_set_icon_from_icon_name (GTK_ENTRY (widget->confirm_passphrase_entry),
                                     GTK_ENTRY_ICON_SECONDARY,
                                     NULL);
  gtk_entry_set_icon_tooltip_text (GTK_ENTRY (widget->confirm_passphrase_entry),
                                   GTK_ENTRY_ICON_SECONDARY,
                                   NULL);

  passphrase = gtk_entry_get_text (GTK_ENTRY (widget->passphrase_entry));

  id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (widget->type_combobox));
  if (g_strcmp0 (id, "vfat") == 0)
    {
      fstype = "vfat";
      has_info = TRUE;
    }
  else if (g_strcmp0 (id, "ntfs") == 0)
    {
      fstype = "ntfs";
      has_info = TRUE;
    }
  else if (g_strcmp0 (id, "ext4") == 0)
    {
      fstype = "ext4";
      has_info = TRUE;
    }
  else if (g_strcmp0 (id, "luks+ext4") == 0)
    {
      fstype = "ext4";
      encrypt = TRUE;
      /* Encrypted, compatible with Linux (LUKS + ext4) */
      show_passphrase_widgets = TRUE;
      if (strlen (gtk_entry_get_text (GTK_ENTRY (widget->passphrase_entry))) > 0)
        {
          if (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (widget->passphrase_entry)),
                         gtk_entry_get_text (GTK_ENTRY (widget->confirm_passphrase_entry))) == 0)
            {
              has_info = TRUE;
            }
          else if (strlen (gtk_entry_get_text (GTK_ENTRY (widget->confirm_passphrase_entry))) > 0)
            {
              gtk_entry_set_icon_from_icon_name (GTK_ENTRY (widget->confirm_passphrase_entry),
                                                 GTK_ENTRY_ICON_SECONDARY,
                                                 "dialog-warning-symbolic");
              gtk_entry_set_icon_tooltip_text (GTK_ENTRY (widget->confirm_passphrase_entry),
                                               GTK_ENTRY_ICON_SECONDARY,
                                               _("The passphrases do not match"));
            }
        }
      gdu_password_strength_widget_set_password (GDU_PASSWORD_STRENGTH_WIDGET (widget->passphrase_strengh_widget),
                                                 gtk_entry_get_text (GTK_ENTRY (widget->passphrase_entry)));
    }
  else if (g_strcmp0 (id, "custom") == 0)
    {
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
    }
  else
    {
      /* Additional FS */
      show_name_widgets = FALSE;
      fstype = id;
      has_info = TRUE;
    }

  _gtk_entry_buffer_truncate_bytes (gtk_entry_get_buffer (GTK_ENTRY (widget->name_entry)),
                                    gdu_utils_get_max_label_length (fstype));
  name = gtk_entry_get_text (GTK_ENTRY (widget->name_entry));

  if (show_name_widgets)
    {
      gtk_widget_show (widget->name_label);
      gtk_widget_show (widget->name_entry);
      gtk_widget_show (widget->name_example);
    }
  else
    {
      gtk_widget_hide (widget->name_label);
      gtk_widget_hide (widget->name_entry);
      gtk_widget_hide (widget->name_example);
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
      gtk_widget_show (widget->passphrase_strengh_box);
    }
  else
    {
      gtk_widget_hide (widget->passphrase_label);
      gtk_widget_hide (widget->passphrase_entry);
      gtk_widget_hide (widget->confirm_passphrase_label);
      gtk_widget_hide (widget->confirm_passphrase_entry);
      gtk_widget_hide (widget->show_passphrase_checkbutton);
      gtk_widget_hide (widget->passphrase_strengh_box);
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
  if (widget->encrypt != encrypt)
    {
      widget->encrypt = encrypt;
      g_object_notify (G_OBJECT (widget), "encrypt");
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
separator_func (GtkTreeModel *model,
                GtkTreeIter *iter,
                gpointer data)
{
  gboolean is_separator;
  gtk_tree_model_get (model, iter,
                      MODEL_COLUMN_SEPARATOR, &is_separator,
                      -1);
  return is_separator;
}

static void
populate (GduCreateFilesystemWidget *widget)
{
  GtkListStore *model;
  GtkCellRenderer *renderer;
  gchar *s;

  /* ---------------------------------------------------------------------------------------------------- */
  /* type combobox */

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (GTK_COMBO_BOX (widget->type_combobox), GTK_TREE_MODEL (model));
  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget->type_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget->type_combobox), renderer,
                                  "markup", MODEL_COLUMN_MARKUP,
                                  NULL);

  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (widget->type_combobox),
                                        separator_func,
                                        widget,
                                        NULL); /* GDestroyNotify */

  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Compatible with all systems and devices"),
                       _("FAT"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "vfat", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Compatible with most systems"),
                       _("NTFS"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "ntfs", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Compatible with Linux systems"),
                       _("Ext4"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "ext4", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Encrypted, compatible with Linux systems"),
                       _("LUKS + Ext4"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID,   "luks+ext4", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Custom"),
                       _("Enter filesystem type"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "custom", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);

  /* Add from additional_types */
  if (widget->additional_fstypes != NULL && widget->additional_fstypes[0] != NULL)
    {
      guint n;

      /* separator */
      gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                         MODEL_COLUMN_SEPARATOR, TRUE, -1);

      for (n = 0; widget->additional_fstypes[n] != NULL; n += 2)
        {
          const gchar *fstype = widget->additional_fstypes[n];
          const gchar *name = widget->additional_fstypes[n+1];
          gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                             MODEL_COLUMN_ID, fstype,
                                             MODEL_COLUMN_MARKUP, name, -1);
        }
    }

  /* Default to FAT or NTFS for removable drives... Ext4 otherwise */
  if (widget->drive != NULL && udisks_drive_get_removable (widget->drive))
    {
      /* default FAT for flash and disks/media smaller than 20G (assumed to be flash cards) */
      if (gdu_utils_is_flash (widget->drive) || udisks_drive_get_size (widget->drive) < (guint64)(20ULL * 1000ULL*1000ULL*1000ULL))
        {
          gtk_combo_box_set_active_id (GTK_COMBO_BOX (widget->type_combobox), "vfat");
        }
      else
        {
          if (gdu_utils_is_ntfs_available ())
            gtk_combo_box_set_active_id (GTK_COMBO_BOX (widget->type_combobox), "ntfs");
          else
            gtk_combo_box_set_active_id (GTK_COMBO_BOX (widget->type_combobox), "vfat");
        }
    }
  else
    {
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (widget->type_combobox), "ext4");
    }

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

  /* ---------------------------------------------------------------------------------------------------- */
  /* erase combobox */

  model = gtk_list_store_new (MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (GTK_COMBO_BOX (widget->erase_combobox), GTK_TREE_MODEL (model));
  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget->erase_combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget->erase_combobox), renderer,
                                  "markup", MODEL_COLUMN_MARKUP,
                                  NULL);

  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (widget->erase_combobox),
                                        separator_func,
                                        widget,
                                        NULL); /* GDestroyNotify */

  /* Quick */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Don’t overwrite existing data"),
                       _("Quick"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);

  /* Full */
  s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
                       _("Overwrite existing data with zeroes"),
                       _("Slow"));
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     MODEL_COLUMN_ID, "zero", MODEL_COLUMN_MARKUP, s, -1);
  g_free (s);

  /* TODO: include 7-pass and 35-pass (DoD 5220-22 M) */

  gtk_combo_box_set_active_id (GTK_COMBO_BOX (widget->erase_combobox), "");
}

static void
gdu_create_filesystem_widget_constructed (GObject *object)
{
  GduCreateFilesystemWidget *widget = GDU_CREATE_FILESYSTEM_WIDGET (object);
  GtkWidget *dummy_window;

  dummy_window = GTK_WIDGET (gdu_application_new_widget (widget->application,
                                                         "filesystem-create.ui",
                                                         "filesystem-create-dummywindow",
                                                         &widget->builder));
  widget->grid = GTK_WIDGET (gtk_builder_get_object (widget->builder, "filesystem-create-grid"));
  widget->erase_combobox = GTK_WIDGET (gtk_builder_get_object (widget->builder, "erase-combobox"));
  g_signal_connect (widget->erase_combobox, "notify::active", G_CALLBACK (on_property_changed), widget);
  widget->type_combobox = GTK_WIDGET (gtk_builder_get_object (widget->builder, "type-combobox"));
  g_signal_connect (widget->type_combobox, "notify::active", G_CALLBACK (on_property_changed), widget);
  widget->name_label = GTK_WIDGET (gtk_builder_get_object (widget->builder, "name-label"));
  widget->name_entry = GTK_WIDGET (gtk_builder_get_object (widget->builder, "name-entry"));
  g_signal_connect (widget->name_entry, "notify::text", G_CALLBACK (on_property_changed), widget);
  widget->name_example = GTK_WIDGET (gtk_builder_get_object (widget->builder, "name-example"));
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
  widget->passphrase_strengh_box = GTK_WIDGET (gtk_builder_get_object (widget->builder, "passphrase-strength-box"));
  widget->passphrase_strengh_widget = gdu_password_strength_widget_new ();
  gtk_widget_set_tooltip_markup (widget->passphrase_strengh_widget,
                                 _("The strength of the passphrase"));
  gtk_box_pack_start (GTK_BOX (widget->passphrase_strengh_box), widget->passphrase_strengh_widget,
                      TRUE, TRUE, 0);

  /* reparent and nuke the dummy window */
  g_object_ref (widget->grid);
  gtk_container_remove (GTK_CONTAINER (dummy_window), widget->grid);
  gtk_container_add (GTK_CONTAINER (widget), widget->grid);
  g_object_unref (widget->grid);

  gtk_widget_destroy (dummy_window);

  populate (widget);
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

  g_object_class_install_property (gobject_class, PROP_ADDITIONAL_FSTYPES,
                                   g_param_spec_boxed ("additional-fstypes", NULL, NULL,
                                                       G_TYPE_STRV,
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

  g_object_class_install_property (gobject_class, PROP_ENCRYPT,
                                   g_param_spec_boolean ("encrypt", NULL, NULL,
                                                         FALSE,
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
gdu_create_filesystem_widget_new (GduApplication            *application,
                                  UDisksDrive               *drive,
                                  const gchar * const       *additional_fstypes)
{
  g_return_val_if_fail (GDU_IS_APPLICATION (application), NULL);
  return GTK_WIDGET (g_object_new (GDU_TYPE_CREATE_FILESYSTEM_WIDGET,
                                   "application", application,
                                   "drive", drive,
                                   "additional-fstypes", additional_fstypes,
                                   NULL));
}

const gchar *
gdu_create_filesystem_widget_get_name (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), NULL);
  return widget->name;
}

const gchar *
gdu_create_filesystem_widget_get_erase (GduCreateFilesystemWidget *widget)
{
  const gchar *ret;

  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), NULL);

  ret = gtk_combo_box_get_active_id (GTK_COMBO_BOX (widget->erase_combobox));
  if (g_strcmp0 (ret, "") == 0)
    ret = NULL;

  return ret;
}

const gchar *
gdu_create_filesystem_widget_get_fstype (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), NULL);
  return widget->fstype;
}

gboolean
gdu_create_filesystem_widget_get_encrypt (GduCreateFilesystemWidget *widget)
{
  g_return_val_if_fail (GDU_IS_CREATE_FILESYSTEM_WIDGET (widget), FALSE);
  return widget->encrypt;
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
