/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>
#include <math.h>

#include "gdu-create-partition-page.h"

enum
{
  PROP_0,
  PROP_COMPLETE
};

struct _GduCreatePartitionPage
{
  AdwBin                 parent_instance;

  GtkWidget             *top_banner;

  GtkWidget             *dos_extended_row;
  GtkWidget             *size_unit_combo;

  GtkAdjustment         *size_adjustment;
  GtkAdjustment         *free_following_adjustment;

  UDisksClient          *client;
  UDisksPartitionTable  *table;
  guint64                max_size;
  guint64                offset;
  gint                   cur_unit_num;
  gboolean               complete;
};

G_DEFINE_TYPE (GduCreatePartitionPage, gdu_create_partition_page, ADW_TYPE_BIN);

gboolean
gdu_create_partition_page_is_extended (GduCreatePartitionPage *self)
{
  return adw_switch_row_get_active (ADW_SWITCH_ROW (self->dos_extended_row));
}

guint64
gdu_create_partition_page_get_size (GduCreatePartitionPage *self)
{
  return gtk_adjustment_get_value (self->size_adjustment) * unit_sizes[self->cur_unit_num];
}

guint64
gdu_create_partition_page_get_offset (GduCreatePartitionPage *self)
{
  return self->offset;
}

static void
create_partition_update (GduCreatePartitionPage *self)
{
  gboolean can_proceed = FALSE;
  gboolean dos_warn = FALSE;
  gboolean dos_error = FALSE;

  /* Show WARNING if trying to create 4th primary partition
   * Show ERROR if there are already 4 primary partitions
   */
  if (g_strcmp0 (udisks_partition_table_get_type_ (self->table), "dos") == 0)
    {
      if (!gdu_utils_is_inside_dos_extended (self->client,
                                             self->table,
                                             self->offset))
        {
          guint num_primary;
          num_primary = gdu_utils_count_primary_dos_partitions (self->client, self->table);
          if (num_primary == 4)
            {
              dos_error = TRUE;
              adw_banner_set_title (ADW_BANNER (self->top_banner),
                                    _("Cannot create a new partition. There are already four primary partitions."));
            }
          else if (num_primary == 3)
            {
              dos_warn = TRUE;
              adw_banner_set_title (ADW_BANNER (self->top_banner),
                                    _("This is the last primary partition that can be created."));
            }
        }
    }

  adw_banner_set_revealed (ADW_BANNER (self->top_banner), dos_error || dos_warn);

  if (gtk_adjustment_get_value (self->size_adjustment) > 0 && !dos_error)
    can_proceed = TRUE;

  self->complete = can_proceed;
  g_object_notify (G_OBJECT (self), "complete");
}

static void
on_size_changed_cb (GduCreatePartitionPage *self)
{
  create_partition_update (self);
}

static gboolean
set_size_entry_unit_cb (AdwSpinRow *spin_row,
                        gpointer   *user_data)
{
  GduCreatePartitionPage *self = GDU_CREATE_PARTITION_PAGE (user_data);
  GtkAdjustment *adjustment;
  GObject *object = NULL;
  g_autofree char *s = NULL;
  const char *unit = NULL;

  adjustment = adw_spin_row_get_adjustment (spin_row);

  object = adw_combo_row_get_selected_item (ADW_COMBO_ROW (self->size_unit_combo));
  unit = gtk_string_object_get_string (GTK_STRING_OBJECT (object));

  s = g_strdup_printf ("%.2f %s", gtk_adjustment_get_value (adjustment), unit);
  gtk_editable_set_text (GTK_EDITABLE (spin_row), s);

  return TRUE;
}

static void
set_unit_num (GduCreatePartitionPage *self, gint unit_num)
{
  gdouble unit_size;
  gdouble value;
  gdouble value_units;
  gdouble max_size_units;

  g_assert (unit_num < NUM_UNITS);

  adw_combo_row_set_selected (ADW_COMBO_ROW (self->size_unit_combo), unit_num);

  if (self->cur_unit_num == -1)
    {
      value = self->max_size;
    }
  else
    {
      value = gtk_adjustment_get_value (self->size_adjustment) * ((gdouble)unit_sizes[self->cur_unit_num]);
    }

  unit_size = unit_sizes[unit_num];
  value_units = value / unit_size;
  max_size_units = ((gdouble)self->max_size) / unit_size;

  self->cur_unit_num = unit_num;

  gtk_adjustment_configure (self->size_adjustment, value_units,
                            0.0,            /* lower */
                            max_size_units, /* upper */
                            1,              /* step increment */
                            100,            /* page increment */
                            0.0);           /* page_size */
}

static void
on_size_unit_changed_cb (GduCreatePartitionPage *self)
{
  gint unit_num;

  unit_num = adw_combo_row_get_selected (ADW_COMBO_ROW (self->size_unit_combo));
  set_unit_num (self, unit_num);

  create_partition_update (self);
}

static gboolean
size_binding_func (GBinding     *binding,
                   const GValue *source_value,
                   GValue       *target_value,
                   gpointer      user_data)
{
  GduCreatePartitionPage *self = GDU_CREATE_PARTITION_PAGE (user_data);
  gdouble max_size_units;

  max_size_units = ((gdouble)self->max_size) / unit_sizes[self->cur_unit_num];
  g_value_set_double (target_value, max_size_units - g_value_get_double (source_value));

  return TRUE;
}

static void
on_part_type_changed (GduCreatePartitionPage *self)
{
  g_object_notify (G_OBJECT (self), "complete");
}

static void
gdu_create_partition_page_get_property (GObject     *object,
                                        guint        property_id,
                                        GValue      *value,
                                        GParamSpec  *pspec)
{
  GduCreatePartitionPage *self = GDU_CREATE_PARTITION_PAGE (object);

  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, self->complete);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_partition_page_init (GduCreatePartitionPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gdu_create_partition_page_class_init (GduCreatePartitionPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gdu_create_partition_page_get_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-create-partition-page.ui");

  gtk_widget_class_bind_template_child (widget_class, GduCreatePartitionPage, top_banner);
  gtk_widget_class_bind_template_child (widget_class, GduCreatePartitionPage, dos_extended_row);
  gtk_widget_class_bind_template_child (widget_class, GduCreatePartitionPage, size_adjustment);

  gtk_widget_class_bind_template_child (widget_class, GduCreatePartitionPage, free_following_adjustment);
  gtk_widget_class_bind_template_child (widget_class, GduCreatePartitionPage, size_unit_combo);

  gtk_widget_class_bind_template_callback (widget_class, on_part_type_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_size_unit_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, set_size_entry_unit_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_size_changed_cb);

  g_object_class_install_property (object_class , PROP_COMPLETE,
                                   g_param_spec_boolean ("complete", NULL, NULL,
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

GduCreatePartitionPage *
gdu_create_partition_page_new (UDisksClient         *client,
                               UDisksPartitionTable *table,
                               guint64               max_size,
                               guint64               offset)
{
  GduCreatePartitionPage *self;

  self = g_object_new (GDU_TYPE_CREATE_PARTITION_PAGE, NULL);
  self->client = client;
  self->table = table;
  self->max_size = max_size;
  self->offset = offset;
  self->cur_unit_num = -1;

  if (g_strcmp0 (udisks_partition_table_get_type_ (self->table), "dos") == 0
      && !gdu_utils_have_dos_extended (self->client, self->table))
    {
      gtk_widget_set_visible (GTK_WIDGET (self->dos_extended_row), TRUE);
    }

  set_unit_num (self, gdu_utils_get_default_unit (self->max_size));
  create_partition_update (self);

  g_object_bind_property_full (self->size_adjustment, "value",
                               self->free_following_adjustment, "value",
                               G_BINDING_BIDIRECTIONAL,
                               size_binding_func, size_binding_func,
                               self, NULL);

  return self;
}
