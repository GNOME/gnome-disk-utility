/* gdu-space-allocation-bar.c
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright 2024 Inam Ul Haq <inam123451@gmail.com>
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   Inam Ul Haq <inam123451@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "gdu-space-allocation-bar"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n.h>

#include "gdu-item.h"
#include "gdu-block.h"
#include "gdu-drive.h"
#include "gdu-space-allocation-bar.h"

struct _GduSpaceAllocationBar
{
  GtkWidget    parent_instance;

  GduDrive    *drive;
};

G_DEFINE_TYPE (GduSpaceAllocationBar, gdu_space_allocation_bar, GTK_TYPE_WIDGET)

static void
update_space_allocation_bar (GduSpaceAllocationBar *self) {
  GtkWidget *child;
  GListModel *partitions;

  partitions = gdu_item_get_partitions (GDU_ITEM (self->drive));

  child = gtk_widget_get_first_child (GTK_WIDGET (self));
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling (child);
    gtk_widget_unparent (child);
    child = next;
  }

  for (guint i = 0; i < g_list_model_get_n_items (partitions); i++)
    {
      GtkWidget *partition_bin;

      partition_bin = adw_bin_new();

      gtk_widget_set_parent (partition_bin, GTK_WIDGET (self));
    }
}

static void
gdu_space_allocation_bar_measure (GtkWidget *widget,
                                  GtkOrientation orientation,
                                  int for_size,
                                  int *minimum,
                                  int *natural,
                                  int *minimum_baseline,
                                  int *natural_baseline)
{
  GtkWidget *child;

  *natural = 50;

  for (child = gtk_widget_get_first_child (widget); child;
       child = gtk_widget_get_next_sibling (child))
    {
      int child_min, child_nat;

      gtk_widget_measure (child, orientation, for_size, &child_min, &child_nat,
                          NULL, NULL);

      *minimum = MAX (*minimum, child_min);
      *natural = MAX (*natural, child_nat);
    }
}

static void
gdu_space_allocation_bar_size_allocate (GtkWidget *widget,
                                        int width,
                                        int height,
                                        int baseline)
{
  guint i;
  GtkWidget *child;
  guint64 total_size;
  GListModel *partitions;

  total_size = gdu_item_get_size (GDU_ITEM (GDU_SPACE_ALLOCATION_BAR (widget)->drive));
  partitions = gdu_item_get_partitions (GDU_ITEM (GDU_SPACE_ALLOCATION_BAR (widget)->drive));
  int pos = 0;

  for (i = 0, child = gtk_widget_get_first_child (widget);
       child && i < g_list_model_get_n_items (partitions);
       child = gtk_widget_get_next_sibling (child), i++)
    {
      GduBlock *block;
      guint64 block_size;
      gdouble fraction;

      block = g_list_model_get_item (partitions, i);
      block_size = gdu_item_get_size (GDU_ITEM (block));
      fraction = (gdouble) block_size / (gdouble) total_size;

      gtk_widget_allocate (child,
                           width * fraction,
                           height,
                           baseline,
                           gsk_transform_translate (NULL, &GRAPHENE_POINT_INIT(pos, 0)));
      pos += width * fraction;
    }
}

static void
gdu_space_allocation_bar_finalize (GObject *object)
{
  GduSpaceAllocationBar *self = GDU_SPACE_ALLOCATION_BAR (object);
  GtkWidget *child;

  child = gtk_widget_get_first_child (GTK_WIDGET (self));
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling (child);
    gtk_widget_unparent (child);
    child = next;
  }

  g_clear_object (&self->drive);

  G_OBJECT_CLASS (gdu_space_allocation_bar_parent_class)->finalize (object);
}

static void
gdu_space_allocation_bar_class_init (GduSpaceAllocationBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_space_allocation_bar_finalize;

  widget_class->measure = gdu_space_allocation_bar_measure;
  widget_class->size_allocate = gdu_space_allocation_bar_size_allocate;
}

static void
gdu_space_allocation_bar_init (GduSpaceAllocationBar *self)
{
  gtk_widget_set_size_request (GTK_WIDGET (self), -1, 25);
  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
}

void
gdu_space_allocation_bar_set_drive (GduSpaceAllocationBar *self,
                                    GduDrive *drive)
{
  g_return_if_fail (GDU_IS_SPACE_ALLOCATION_BAR (self));
  g_return_if_fail (!drive || GDU_IS_DRIVE (drive));

  if (self->drive == drive)
    return;

  g_set_object (&self->drive, drive);

  update_space_allocation_bar (self);
}
