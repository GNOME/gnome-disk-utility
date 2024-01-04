/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_ENUMS_H__
#define __GDU_ENUMS_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum
{
  GDU_POWER_STATE_FLAGS_NONE              = 0,
  GDU_POWER_STATE_FLAGS_STANDBY           = (1<<0),
  GDU_POWER_STATE_FLAGS_CHECKING          = (1<<1),
  GDU_POWER_STATE_FLAGS_FAILED            = (1<<2)
} GduPowerStateFlags;

typedef enum
{
  GDU_DEVICE_TREE_MODEL_FLAGS_NONE                = 0,
  GDU_DEVICE_TREE_MODEL_FLAGS_FLAT                = (1<<0),
  GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_POWER_STATE  = (1<<1),
  GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_PULSE        = (1<<2),
  GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME       = (1<<3),
  GDU_DEVICE_TREE_MODEL_FLAGS_INCLUDE_DEVICE_NAME = (1<<4),
  GDU_DEVICE_TREE_MODEL_FLAGS_INCLUDE_NONE_ITEM   = (1<<5),
} GduDeviceTreeModelFlags;

G_END_DECLS

#endif /* __GDU_ENUMS_H__ */
