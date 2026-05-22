/*
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <glib-object.h>
#include <glib.h>
#include <gmodule.h>

G_BEGIN_DECLS

#define GDU_TYPE_SD_MONITOR  (gdu_sd_monitor_get_type ())
#define GDU_SD_MONITOR(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_SD_MONITOR, GduSdMonitor))
#define GDU_IS_SD_MONITOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_SD_MONITOR))

struct GduSdMonitor;
typedef struct GduSdMonitor GduSdMonitor;

GType         gdu_sd_monitor_get_type (void) G_GNUC_CONST;
GduSdMonitor *gdu_sd_monitor_new (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GduSdMonitor, g_object_unref)
G_END_DECLS
