/*
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_SD_PLUGIN_H__
#define __GDU_SD_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>

G_BEGIN_DECLS

#define GDU_TYPE_SD_MANAGER  (gdu_sd_manager_get_type ())
#define GDU_SD_MANAGER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_SD_MANAGER, GduSdManager))
#define GDU_IS_SD_MANAGER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_SD_MANAGER))

struct GduSdManager;
typedef struct GduSdManager GduSdManager;

GType gdu_sd_manager_get_type (void) G_GNUC_CONST;

/* All the plugins must implement this function */
G_MODULE_EXPORT GType register_gnome_settings_plugin (GTypeModule *module);

G_END_DECLS

#endif /* __GDU_SD_PLUGIN_H__ */
