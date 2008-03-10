/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-volume.c
 *
 * Copyright (C) 2007 David Zeuthen
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
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "gdu-pool.h"
#include "gdu-volume.h"
#include "gdu-presentable.h"

struct _GduVolumePrivate
{
        GduDevice *device;
        GduPresentable *enclosing_presentable;
};

static GObjectClass *parent_class = NULL;

static void gdu_volume_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduVolume, gdu_volume, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_volume_presentable_iface_init))

static void
gdu_volume_finalize (GduVolume *volume)
{
        if (volume->priv->device != NULL)
                g_object_unref (volume->priv->device);

        if (volume->priv->enclosing_presentable != NULL)
                g_object_unref (volume->priv->enclosing_presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (volume));
}

static void
gdu_volume_class_init (GduVolumeClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_volume_finalize;
}

static void
gdu_volume_init (GduVolume *volume)
{
        volume->priv = g_new0 (GduVolumePrivate, 1);
}

static void
device_changed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "changed");
}

static void
device_removed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "removed");
}

GduVolume *
gdu_volume_new_from_device (GduDevice *device, GduPresentable *enclosing_presentable)
{
        GduVolume *volume;

        volume = GDU_VOLUME (g_object_new (GDU_TYPE_VOLUME, NULL));
        volume->priv->device = g_object_ref (device);
        volume->priv->enclosing_presentable = 
                enclosing_presentable != NULL ? g_object_ref (enclosing_presentable) : NULL;

        g_signal_connect (device, "changed", (GCallback) device_changed, volume);
        g_signal_connect (device, "removed", (GCallback) device_removed, volume);

        return volume;
}

static GduDevice *
gdu_volume_get_device (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        return g_object_ref (volume->priv->device);
}

static GduPresentable *
gdu_volume_get_enclosing_presentable (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        if (volume->priv->enclosing_presentable != NULL)
                return g_object_ref (volume->priv->enclosing_presentable);
        return NULL;
}

static void
gdu_volume_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_device = gdu_volume_get_device;
        iface->get_enclosing_presentable = gdu_volume_get_enclosing_presentable;
}
