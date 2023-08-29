/* gdu-item.h
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum GduFeature {
  GDU_FEATURE_NONE               = 0,
  GDU_FEATURE_FORMAT             = 1 << 0,
  GDU_FEATURE_CREATE_IMAGE       = 1 << 1,
  GDU_FEATURE_RESTORE_IMAGE      = 1 << 2,
  GDU_FEATURE_CREATE_PARTITION   = 1 << 3,
  GDU_FEATURE_EDIT_PARTITION     = 1 << 4,
  GDU_FEATURE_DELETE_PARTITION   = 1 << 5,
  GDU_FEATURE_RESIZE_PARTITION   = 1 << 6,
  GDU_FEATURE_CHECK_FILESYSTEM   = 1 << 7,
  GDU_FEATURE_REPAIR_FILESYSTEM  = 1 << 8,
  GDU_FEATURE_EDIT_LABEL         = 1 << 9,
  GDU_FEATURE_CAN_SWAPON         = 1 << 10,
  GDU_FEATURE_CAN_SWAPOFF        = 1 << 11,
  GDU_FEATURE_CAN_MOUNT          = 1 << 12,
  GDU_FEATURE_CAN_UNMOUNT        = 1 << 13,
  GDU_FEATURE_CAN_LOCK           = 1 << 14,
  GDU_FEATURE_CAN_UNLOCK         = 1 << 15,
  GDU_FEATURE_CHANGE_PASSPHRASE  = 1 << 16,
  GDU_FEATURE_TAKE_OWNERSHIP     = 1 << 17,
  GDU_FEATURE_BENCHMARK          = 1 << 18,
  GDU_FEATURE_CONFIGURE_FSTAB    = 1 << 19,
  GDU_FEATURE_CONFIGURE_CRYPTTAB = 1 << 20,
  GDU_FEATURE_SMART              = 1 << 21,
  GDU_FEATURE_SETTINGS           = 1 << 22,
  GDU_FEATURE_STANDBY            = 1 << 23,
  GDU_FEATURE_WAKEUP             = 1 << 24,
  GDU_FEATURE_POWEROFF           = 1 << 25,
  GDU_FEATURE_EJECT              = 1 << 26,
} GduFeature;

#define GDU_TYPE_ITEM (gdu_item_get_type ())
G_DECLARE_DERIVABLE_TYPE (GduItem, gdu_item, GDU, ITEM, GObject)

struct _GduItemClass
{
  GObjectClass parent_class;

  const char   *(*get_description)            (GduItem              *self);
  const char   *(*get_partition_type)         (GduItem              *self);
  GIcon        *(*get_icon)                   (GduItem              *self);
  guint64       (*get_size)                   (GduItem              *self);
  GListModel   *(*get_partitions)             (GduItem              *self);
  GduItem      *(*get_parent)                 (GduItem              *self);
  GduFeature    (*get_features)               (GduItem              *self);

  void          (*changed)                    (GduItem              *self);
};

const char   *gdu_item_get_description             (GduItem              *self);
const char   *gdu_item_get_partition_type          (GduItem              *self);
GIcon        *gdu_item_get_icon                    (GduItem              *self);
guint64       gdu_item_get_size                    (GduItem              *self);
GListModel   *gdu_item_get_partitions              (GduItem              *self);
GduItem      *gdu_item_get_parent                  (GduItem              *self);
GduFeature    gdu_item_get_features                (GduItem              *self);

void          gdu_item_changed                     (GduItem              *self);

G_END_DECLS
