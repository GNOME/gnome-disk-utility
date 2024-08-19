/* gdu-item.c
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gdu-item"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "gdu-item.h"

G_DEFINE_ABSTRACT_TYPE (GduItem, gdu_item, G_TYPE_OBJECT)

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static const char *
gdu_item_real_get_description (GduItem *self)
{
  g_assert (GDU_IS_ITEM (self));

  return "—";
}

static const char *
gdu_item_real_get_partition_type (GduItem *self)
{
  g_assert (GDU_IS_ITEM (self));

  return "—";
}

static GIcon *
gdu_item_real_get_icon (GduItem *self)
{
  g_assert_not_reached ();
}

static guint64
gdu_item_real_get_size (GduItem *self)
{
  return 0;
}

static GduItem *
gdu_item_real_get_parent (GduItem *self)
{
  return NULL;
}

static GListModel *
gdu_item_real_get_partitions (GduItem *self)
{
  return NULL;
}

static GduFeature
gdu_item_real_get_features (GduItem *self)
{
  return GDU_FEATURE_NONE;
}

static void
gdu_item_real_changed (GduItem *self)
{
  g_signal_emit_by_name (self, "changed", 0);
}

static void
gdu_item_class_init (GduItemClass *klass)
{
  GduItemClass *item_class = GDU_ITEM_CLASS (klass);

  item_class->get_description = gdu_item_real_get_description;
  item_class->get_partition_type = gdu_item_real_get_partition_type;
  item_class->get_icon = gdu_item_real_get_icon;
  item_class->get_size = gdu_item_real_get_size;
  item_class->get_parent = gdu_item_real_get_parent;
  item_class->get_partitions = gdu_item_real_get_partitions;
  item_class->get_features = gdu_item_real_get_features;
  item_class->changed = gdu_item_real_changed;

  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
gdu_item_init (GduItem *self)
{
}

const char *
gdu_item_get_description (GduItem *self)
{
  g_return_val_if_fail (GDU_IS_ITEM (self), NULL);

  return GDU_ITEM_GET_CLASS (self)->get_description (self);
}

const char *
gdu_item_get_partition_type (GduItem *self)
{
  g_return_val_if_fail (GDU_IS_ITEM (self), NULL);

  return GDU_ITEM_GET_CLASS (self)->get_partition_type (self);
}

GIcon *
gdu_item_get_icon (GduItem *self)
{
  g_return_val_if_fail (GDU_IS_ITEM (self), NULL);

  return GDU_ITEM_GET_CLASS (self)->get_icon (self);
}

guint64
gdu_item_get_size (GduItem *self)
{
  g_return_val_if_fail (GDU_IS_ITEM (self), 0);

  return GDU_ITEM_GET_CLASS (self)->get_size (self);
}

GduItem *
gdu_item_get_parent (GduItem *self)
{
  g_return_val_if_fail (GDU_IS_ITEM (self), NULL);

  return GDU_ITEM_GET_CLASS (self)->get_parent (self);
}

GListModel *
gdu_item_get_partitions (GduItem *self)
{
  g_return_val_if_fail (GDU_IS_ITEM (self), NULL);

  return GDU_ITEM_GET_CLASS (self)->get_partitions (self);
}

GduFeature
gdu_item_get_features (GduItem *self)
{
  g_return_val_if_fail (GDU_IS_ITEM (self), GDU_FEATURE_NONE);

  return GDU_ITEM_GET_CLASS (self)->get_features (self);
}

void
gdu_item_changed (GduItem *self)
{
  GDU_ITEM_GET_CLASS (self)->changed (self);
}
