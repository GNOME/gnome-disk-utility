/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_TYPES_H__
#define __GDU_TYPES_H__

#include <gtk/gtk.h>
#include <udisks/udisks.h>

#include <libgdu/libgdu.h>

#include "gduenums.h"

G_BEGIN_DECLS

struct _GduApplication;
typedef struct _GduApplication GduApplication;

struct _GduDeviceTreeModel;
typedef struct _GduDeviceTreeModel GduDeviceTreeModel;

struct _GduWindow;
typedef struct _GduWindow GduWindow;

struct _GduVolumeGrid;
typedef struct _GduVolumeGrid GduVolumeGrid;

struct _GduCreateFilesystemWidget;
typedef struct _GduCreateFilesystemWidget GduCreateFilesystemWidget;

struct _GduPasswordStrengthWidget;
typedef struct _GduPasswordStrengthWidget GduPasswordStrengthWidget;

struct _GduEstimator;
typedef struct _GduEstimator GduEstimator;

struct GduDVDSupport;
typedef struct GduDVDSupport GduDVDSupport;

struct GduLocalJob;
typedef struct GduLocalJob GduLocalJob;

G_END_DECLS

#endif /* __GDU_TYPES_H__ */
