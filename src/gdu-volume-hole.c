/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-volume-hole.c
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
#include <stdlib.h>

#include "gdu-main.h"
#include "gdu-pool.h"
#include "gdu-volume-hole.h"
#include "gdu-presentable.h"

struct _GduVolumeHolePrivate
{
        guint64 offset;
        guint64 size;
        GduPresentable *enclosing_presentable;
};

static GObjectClass *parent_class = NULL;

static void gdu_volume_hole_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduVolumeHole, gdu_volume_hole, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_volume_hole_presentable_iface_init))

static void
gdu_volume_hole_finalize (GduVolumeHole *volume_hole)
{
        if (volume_hole->priv->enclosing_presentable != NULL)
                g_object_unref (volume_hole->priv->enclosing_presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (volume_hole));
}

static void
gdu_volume_hole_class_init (GduVolumeHoleClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_volume_hole_finalize;
}

static void
gdu_volume_hole_init (GduVolumeHole *volume_hole)
{
        volume_hole->priv = g_new0 (GduVolumeHolePrivate, 1);
}

GduVolumeHole *
gdu_volume_hole_new (guint64 offset, guint64 size, GduPresentable *enclosing_presentable)
{
        GduVolumeHole *volume_hole;

        volume_hole = GDU_VOLUME_HOLE (g_object_new (GDU_TYPE_VOLUME_HOLE, NULL));
        volume_hole->priv->offset = offset;
        volume_hole->priv->size = size;
        volume_hole->priv->enclosing_presentable =
                enclosing_presentable != NULL ? g_object_ref (enclosing_presentable) : NULL;

        return volume_hole;
}

static GduDevice *
gdu_volume_hole_get_device (GduPresentable *presentable)
{
        return NULL;
}

static GduPresentable *
gdu_volume_hole_get_enclosing_presentable (GduPresentable *presentable)
{
        GduVolumeHole *volume_hole = GDU_VOLUME_HOLE (presentable);
        if (volume_hole->priv->enclosing_presentable != NULL)
                return g_object_ref (volume_hole->priv->enclosing_presentable);
        return NULL;
}

static char *
gdu_volume_hole_get_name (GduPresentable *presentable)
{
        GduVolumeHole *volume_hole = GDU_VOLUME_HOLE (presentable);
        char *result;
        char *strsize;

        strsize = gdu_util_get_size_for_display (volume_hole->priv->size, FALSE);
        result = g_strdup_printf (_("%s Free Space"), strsize);
        g_free (strsize);

        return result;
}

static char *
gdu_volume_hole_get_icon_name (GduPresentable *presentable)
{
        //GduVolumeHole *volume_hole = GDU_VOLUME_HOLE (presentable);
        return g_strdup ("drive-harddisk");
}

static guint64
gdu_volume_hole_get_offset (GduPresentable *presentable)
{
        GduVolumeHole *volume_hole = GDU_VOLUME_HOLE (presentable);
        return volume_hole->priv->offset;
}

static GList *
gdu_volume_hole_get_info (GduPresentable *presentable)
{
        GduVolumeHole *volume_hole = GDU_VOLUME_HOLE (presentable);
        GList *kv_pairs = NULL;

	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Free Space")));
        kv_pairs = g_list_prepend (kv_pairs,
                                   gdu_util_get_size_for_display (volume_hole->priv->size, TRUE));

        kv_pairs = g_list_reverse (kv_pairs);
        return kv_pairs;
}

static void
gdu_volume_hole_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_device = gdu_volume_hole_get_device;
        iface->get_enclosing_presentable = gdu_volume_hole_get_enclosing_presentable;
        iface->get_name = gdu_volume_hole_get_name;
        iface->get_icon_name = gdu_volume_hole_get_icon_name;
        iface->get_offset = gdu_volume_hole_get_offset;
        iface->get_info = gdu_volume_hole_get_info;
}
