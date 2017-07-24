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

#include "gducreatepartitionpage.h"

struct _GduCreatePartitionPage
{
  GtkBox parent_instance;
};

typedef struct _GduCreatePartitionPagePrivate GduCreatePartitionPagePrivate;

struct _GduCreatePartitionPagePrivate
{
  GtkWidget *infobar_vbox;
  GtkWidget *dos_error_infobar;
  GtkWidget *dos_warning_infobar;
  GtkWidget *size_spinbutton;
  GtkWidget *free_following_spinbutton;
  GtkAdjustment *size_adjustment;
  GtkAdjustment *free_following_adjustment;
  GtkWidget *size_unit_combobox;
  GtkWidget *size_unit_following_label;
  GtkCheckButton *part_dos_extended_checkbutton;

  UDisksClient *client;
  UDisksPartitionTable *table;
  guint64 max_size;
  guint64 offset;
  gint cur_unit_num;
  gboolean complete;
};

enum
{
  PROP_0,
  PROP_COMPLETE
};

G_DEFINE_TYPE_WITH_PRIVATE (GduCreatePartitionPage, gdu_create_partition_page, GTK_TYPE_BOX);

static void
gdu_create_partition_page_init (GduCreatePartitionPage *page)
{
  gtk_widget_init_template (GTK_WIDGET (page));
}

static void
gdu_create_partition_page_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  GduCreatePartitionPage *page = GDU_CREATE_PARTITION_PAGE (object);
  GduCreatePartitionPagePrivate *priv;

  priv = gdu_create_partition_page_get_instance_private (page);

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
gdu_create_partition_page_class_init (GduCreatePartitionPageClass *class)
{
  GObjectClass *gobject_class;

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (class),
                                               "/org/gnome/Disks/ui/create-partition-page.ui");
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, infobar_vbox);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, size_spinbutton);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, free_following_spinbutton);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, size_adjustment);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, free_following_adjustment);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, size_unit_combobox);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, size_unit_following_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePartitionPage, part_dos_extended_checkbutton);

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = gdu_create_partition_page_get_property;
  g_object_class_install_property (gobject_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete", NULL, NULL,
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

gboolean
gdu_create_partition_page_is_extended (GduCreatePartitionPage *page)
{
  GduCreatePartitionPagePrivate *priv;

  priv = gdu_create_partition_page_get_instance_private (page);

  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->part_dos_extended_checkbutton));
}

guint64
gdu_create_partition_page_get_size (GduCreatePartitionPage *page)
{
  GduCreatePartitionPagePrivate *priv;

  priv = gdu_create_partition_page_get_instance_private (page);

  return gtk_adjustment_get_value (priv->size_adjustment) * unit_sizes[priv->cur_unit_num];
}

guint64
gdu_create_partition_page_get_offset (GduCreatePartitionPage *page)
{
  GduCreatePartitionPagePrivate *priv;

  priv = gdu_create_partition_page_get_instance_private (page);

  return priv->offset;
}

static void
create_partition_update (GduCreatePartitionPage *page)
{
  GduCreatePartitionPagePrivate *priv;
  gboolean can_proceed = FALSE;
  gboolean show_dos_error = FALSE;
  gboolean show_dos_warning = FALSE;
  gchar *s;

  priv = gdu_create_partition_page_get_instance_private (page);

  /* Show WARNING if trying to create 4th primary partition
   * Show ERROR if there are already 4 primary partitions
   */
  if (g_strcmp0 (udisks_partition_table_get_type_ (priv->table), "dos") == 0)
    {
      if (!gdu_utils_is_inside_dos_extended (priv->client, priv->table, priv->offset))
        {
          guint num_primary;
          num_primary = gdu_utils_count_primary_dos_partitions (priv->client, priv->table);
          if (num_primary == 4)
            show_dos_error = TRUE;
          else if (num_primary == 3)
            show_dos_warning = TRUE;
        }
    }

  if (gtk_adjustment_get_value (priv->size_adjustment) > 0 && !show_dos_error)
    can_proceed = TRUE;

  priv->complete = can_proceed;
  g_object_notify (G_OBJECT (page), "complete");

  gtk_widget_set_visible (priv->dos_warning_infobar, show_dos_warning);
  gtk_widget_set_visible (priv->dos_error_infobar, show_dos_error);

  s = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (priv->size_unit_combobox));
  gtk_label_set_text (GTK_LABEL (priv->size_unit_following_label), s);
  g_free (s);
}

static void
create_partition_property_changed (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GduCreatePartitionPage *page = GDU_CREATE_PARTITION_PAGE (user_data);

  create_partition_update (page);
}

static void
set_unit_num (GduCreatePartitionPage *page, gint unit_num)
{
  GduCreatePartitionPagePrivate *priv;
  gdouble unit_size;
  gdouble value;
  gdouble value_units;
  gdouble max_size_units;
  gint num_digits;

  priv = gdu_create_partition_page_get_instance_private (page);

  g_assert (unit_num < NUM_UNITS);

  gtk_combo_box_set_active (GTK_COMBO_BOX (priv->size_unit_combobox), unit_num);

  if (priv->cur_unit_num == -1)
    {
      value = priv->max_size;
    }
  else
    {
      value = gtk_adjustment_get_value (priv->size_adjustment) * ((gdouble) unit_sizes[priv->cur_unit_num]);
    }

  unit_size = unit_sizes[unit_num];
  value_units = value / unit_size;
  max_size_units = ((gdouble) priv->max_size) / unit_size;

  /* show at least three digits in the spin buttons */
  num_digits = 3.0 - ceil (log10 (max_size_units));
  if (num_digits < 0)
    num_digits = 0;

  g_object_freeze_notify (G_OBJECT (priv->size_adjustment));
  g_object_freeze_notify (G_OBJECT (priv->free_following_adjustment));

  priv->cur_unit_num = unit_num;

  gtk_adjustment_configure (priv->size_adjustment,
                            value_units,
                            0.0,                    /* lower */
                            max_size_units,         /* upper */
                            1,                      /* step increment */
                            100,                    /* page increment */
                            0.0);                   /* page_size */
  gtk_adjustment_configure (priv->free_following_adjustment,
                            max_size_units - value_units,
                            0.0,                    /* lower */
                            max_size_units,         /* upper */
                            1,                      /* step increment */
                            100,                    /* page increment */
                            0.0);                   /* page_size */

  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (priv->size_spinbutton), num_digits);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (priv->free_following_spinbutton), num_digits);

  gtk_adjustment_set_value (priv->size_adjustment, value_units);
  gtk_adjustment_set_value (priv->free_following_adjustment, max_size_units - value_units);

  g_object_thaw_notify (G_OBJECT (priv->size_adjustment));
  g_object_thaw_notify (G_OBJECT (priv->free_following_adjustment));
}

static void
create_partition_populate (GduCreatePartitionPage *page)
{
  GduCreatePartitionPagePrivate *priv;

  priv = gdu_create_partition_page_get_instance_private (page);

  set_unit_num (page, gdu_utils_get_default_unit (priv->max_size));
}

static void
on_size_unit_combobox_changed (GtkComboBox *combobox, GduCreatePartitionPage *page)
{
  GduCreatePartitionPagePrivate *priv;
  gint unit_num;

  priv = gdu_create_partition_page_get_instance_private (page);

  unit_num = gtk_combo_box_get_active (GTK_COMBO_BOX (priv->size_unit_combobox));
  set_unit_num (page, unit_num);

  create_partition_update (page);
}

static gboolean
size_binding_func (GBinding *binding, const GValue *source_value, GValue *target_value, gpointer user_data)
{
  GduCreatePartitionPage *page = GDU_CREATE_PARTITION_PAGE (user_data);
  GduCreatePartitionPagePrivate *priv;
  gdouble max_size_units;

  priv = gdu_create_partition_page_get_instance_private (page);

  max_size_units = ((gdouble) priv->max_size) / unit_sizes[priv->cur_unit_num];
  g_value_set_double (target_value, max_size_units - g_value_get_double (source_value));

  return TRUE;
}

static void
on_part_type_changed (GtkToggleButton *object, gpointer user_data)
{
  GduCreatePartitionPage *page = GDU_CREATE_PARTITION_PAGE (user_data);

  g_object_notify (G_OBJECT (page), "complete");
}

GduCreatePartitionPage *
gdu_create_partition_page_new (UDisksClient *client, UDisksPartitionTable *table,
                               guint64 max_size, guint64 offset)
{
  GduCreatePartitionPage *page;
  GduCreatePartitionPagePrivate *priv;

  page = g_object_new (GDU_TYPE_CREATE_PARTITION_PAGE, NULL);
  priv = gdu_create_partition_page_get_instance_private (page);
  priv->client = client;
  priv->table = table;
  priv->max_size = max_size;
  priv->offset = offset;
  priv->cur_unit_num = -1;

  if (g_strcmp0 (udisks_partition_table_get_type_ (priv->table), "dos") != 0 ||
      gdu_utils_have_dos_extended (priv->client, priv->table))
    {
      /* hide if already present or GPT */
      gtk_widget_hide (GTK_WIDGET (priv->part_dos_extended_checkbutton));
    }

  priv->dos_error_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_ERROR,
                                                       _("Cannot create a new partition. There are already four primary partitions."),
                                                       NULL);
  gtk_box_pack_start (GTK_BOX (priv->infobar_vbox), priv->dos_error_infobar, TRUE, TRUE, 0);
  priv->dos_warning_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_WARNING,
                                                         _("This is the last primary partition that can be created."),
                                                         NULL);
  gtk_box_pack_start (GTK_BOX (priv->infobar_vbox), priv->dos_warning_infobar, TRUE, TRUE, 0);
  g_signal_connect (priv->size_adjustment, "notify::value", G_CALLBACK (create_partition_property_changed), page);
  g_signal_connect (priv->size_unit_combobox, "changed", G_CALLBACK (on_size_unit_combobox_changed), page);

  g_signal_connect (priv->part_dos_extended_checkbutton, "toggled", G_CALLBACK (on_part_type_changed), page);

  create_partition_populate (page);
  create_partition_update (page);

  g_object_bind_property_full (priv->size_adjustment, "value", priv->free_following_adjustment, "value",
                               G_BINDING_BIDIRECTIONAL, size_binding_func, size_binding_func, page, NULL);

  return page;
}
