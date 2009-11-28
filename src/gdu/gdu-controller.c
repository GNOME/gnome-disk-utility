/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-controller.c
 *
 * Copyright (C) 2009 David Zeuthen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <dbus/dbus-glib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "gdu-private.h"
#include "gdu-pool.h"
#include "gdu-controller.h"
#include "devkit-disks-controller-glue.h"

/* --- SUCKY CODE BEGIN --- */

/* This totally sucks; dbus-bindings-tool and dbus-glib should be able
 * to do this for us.
 *
 * TODO: keep in sync with code in tools/devkit-disks in DeviceKit-disks.
 */

typedef struct
{
        gchar *native_path;

        gchar *vendor;
        gchar *model;
        gchar *driver;
} ControllerProperties;

static void
collect_props (const char *key, const GValue *value, ControllerProperties *props)
{
        gboolean handled = TRUE;

        if (strcmp (key, "NativePath") == 0)
                props->native_path = g_strdup (g_value_get_string (value));

        else if (strcmp (key, "Vendor") == 0)
                props->vendor = g_value_dup_string (value);
        else if (strcmp (key, "Model") == 0)
                props->model = g_value_dup_string (value);
        else if (strcmp (key, "Driver") == 0)
                props->driver = g_value_dup_string (value);
        else
                handled = FALSE;

        if (!handled)
                g_warning ("unhandled property '%s'", key);
}

static void
controller_properties_free (ControllerProperties *props)
{
        g_free (props->native_path);
        g_free (props->vendor);
        g_free (props->model);
        g_free (props->driver);
        g_free (props);
}

static ControllerProperties *
controller_properties_get (DBusGConnection *bus,
                           const char *object_path)
{
        ControllerProperties *props;
        GError *error;
        GHashTable *hash_table;
        DBusGProxy *prop_proxy;
        const char *ifname = "org.freedesktop.DeviceKit.Disks.Controller";

        props = g_new0 (ControllerProperties, 1);

	prop_proxy = dbus_g_proxy_new_for_name (bus,
                                                "org.freedesktop.DeviceKit.Disks",
                                                object_path,
                                                "org.freedesktop.DBus.Properties");
        error = NULL;
        if (!dbus_g_proxy_call (prop_proxy,
                                "GetAll",
                                &error,
                                G_TYPE_STRING,
                                ifname,
                                G_TYPE_INVALID,
                                dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
                                &hash_table,
                                G_TYPE_INVALID)) {
                g_warning ("Couldn't call GetAll() to get properties for %s: %s", object_path, error->message);
                g_error_free (error);

                controller_properties_free (props);
                props = NULL;
                goto out;
        }

        g_hash_table_foreach (hash_table, (GHFunc) collect_props, props);

        g_hash_table_unref (hash_table);

#if 0
        g_print ("----------------------------------------------------------------------\n");
        g_print ("native_path: %s\n", props->native_path);
        g_print ("vendor:      %s\n", props->vendor);
        g_print ("model:       %s\n", props->model);
        g_print ("driver:      %s\n", props->driver);
#endif

out:
        g_object_unref (prop_proxy);
        return props;
}

/* --- SUCKY CODE END --- */

struct _GduControllerPrivate
{
        DBusGConnection *bus;
        DBusGProxy *proxy;
        GduPool *pool;

        char *object_path;

        ControllerProperties *props;
};

enum {
        CHANGED,
        REMOVED,
        LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GduController, gdu_controller, G_TYPE_OBJECT);

GduPool *
gdu_controller_get_pool (GduController *controller)
{
        return g_object_ref (controller->priv->pool);
}

static void
gdu_controller_finalize (GduController *controller)
{
        g_debug ("##### finalized controller %s",
                 controller->priv->props != NULL ? controller->priv->props->native_path : controller->priv->object_path);

        dbus_g_connection_unref (controller->priv->bus);
        g_free (controller->priv->object_path);
        if (controller->priv->proxy != NULL)
                g_object_unref (controller->priv->proxy);
        if (controller->priv->pool != NULL)
                g_object_unref (controller->priv->pool);
        if (controller->priv->props != NULL)
                controller_properties_free (controller->priv->props);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (controller));
}

static void
gdu_controller_class_init (GduControllerClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_controller_finalize;

        g_type_class_add_private (klass, sizeof (GduControllerPrivate));

        signals[CHANGED] =
                g_signal_new ("changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduControllerClass, changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals[REMOVED] =
                g_signal_new ("removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduControllerClass, removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

static void
gdu_controller_init (GduController *controller)
{
        controller->priv = G_TYPE_INSTANCE_GET_PRIVATE (controller, GDU_TYPE_CONTROLLER, GduControllerPrivate);
}

static gboolean
update_info (GduController *controller)
{
        ControllerProperties *new_properties;

        new_properties = controller_properties_get (controller->priv->bus, controller->priv->object_path);
        if (new_properties != NULL) {
                if (controller->priv->props != NULL)
                        controller_properties_free (controller->priv->props);
                controller->priv->props = new_properties;
                return TRUE;
        } else {
                return FALSE;
        }
}


GduController *
_gdu_controller_new_from_object_path (GduPool *pool, const char *object_path)
{
        GError *error;
        GduController *controller;

        controller = GDU_CONTROLLER (g_object_new (GDU_TYPE_CONTROLLER, NULL));
        controller->priv->object_path = g_strdup (object_path);
        controller->priv->pool = g_object_ref (pool);

        error = NULL;
        controller->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (controller->priv->bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                goto error;
        }

	controller->priv->proxy = dbus_g_proxy_new_for_name (controller->priv->bus,
                                                         "org.freedesktop.DeviceKit.Disks",
                                                         controller->priv->object_path,
                                                         "org.freedesktop.DeviceKit.Disks.Controller");
        dbus_g_proxy_set_default_timeout (controller->priv->proxy, INT_MAX);
        dbus_g_proxy_add_signal (controller->priv->proxy, "Changed", G_TYPE_INVALID);

        /* TODO: connect signals */

        if (!update_info (controller))
                goto error;

        g_debug ("_gdu_controller_new_from_object_path: %s", controller->priv->props->native_path);

        return controller;
error:
        g_object_unref (controller);
        return NULL;
}

gboolean
_gdu_controller_changed (GduController *controller)
{
        g_debug ("_gdu_controller_changed: %s", controller->priv->props->native_path);
        if (update_info (controller)) {
                g_signal_emit (controller, signals[CHANGED], 0);
                return TRUE;
        } else {
                return FALSE;
        }
}

const gchar *
gdu_controller_get_object_path (GduController *controller)
{
        return controller->priv->object_path;
}


const gchar *
gdu_controller_get_native_path (GduController *controller)
{
        return controller->priv->props->native_path;
}

const gchar *
gdu_controller_get_vendor (GduController *controller)
{
        return controller->priv->props->vendor;
}

const gchar *
gdu_controller_get_model (GduController *controller)
{
        return controller->priv->props->model;
}

const gchar *
gdu_controller_get_driver (GduController *controller)
{
        return controller->priv->props->driver;
}
