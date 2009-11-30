/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-hba.c
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
#include <glib/gi18n-lib.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>

#include "gdu-private.h"
#include "gdu-util.h"
#include "gdu-pool.h"
#include "gdu-adapter.h"
#include "gdu-hba.h"
#include "gdu-presentable.h"
#include "gdu-linux-md-drive.h"

/**
 * SECTION:gdu-hba
 * @title: GduHba
 * @short_description: HBAs
 *
 * #GduHba objects are used to represent host board adapters (also
 * called disk adapters).
 *
 * See the documentation for #GduPresentable for the big picture.
 */

struct _GduHbaPrivate
{
        GduAdapter *adapter;
        GduPool *pool;
        gchar *id;
};

static GObjectClass *parent_class = NULL;

static void gdu_hba_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduHba, gdu_hba, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_hba_presentable_iface_init))

static void adapter_changed (GduAdapter *adapter, gpointer user_data);


static void
gdu_hba_finalize (GObject *object)
{
        GduHba *hba = GDU_HBA (object);

        //g_debug ("##### finalized hba '%s' %p", hba->priv->id, hba);

        if (hba->priv->adapter != NULL) {
                g_signal_handlers_disconnect_by_func (hba->priv->adapter, adapter_changed, hba);
                g_object_unref (hba->priv->adapter);
        }

        if (hba->priv->pool != NULL)
                g_object_unref (hba->priv->pool);

        g_free (hba->priv->id);

        if (G_OBJECT_CLASS (parent_class)->finalize != NULL)
                (* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
gdu_hba_class_init (GduHbaClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = gdu_hba_finalize;

        g_type_class_add_private (klass, sizeof (GduHbaPrivate));
}

static void
gdu_hba_init (GduHba *hba)
{
        hba->priv = G_TYPE_INSTANCE_GET_PRIVATE (hba, GDU_TYPE_HBA, GduHbaPrivate);
}

static void
adapter_changed (GduAdapter *adapter, gpointer user_data)
{
        GduHba *hba = GDU_HBA (user_data);
        g_signal_emit_by_name (hba, "changed");
        g_signal_emit_by_name (hba->priv->pool, "presentable-changed", hba);
}

GduHba *
_gdu_hba_new_from_adapter (GduPool *pool, GduAdapter *adapter)
{
        GduHba *hba;

        hba = GDU_HBA (g_object_new (GDU_TYPE_HBA, NULL));
        hba->priv->adapter = g_object_ref (adapter);
        hba->priv->pool = g_object_ref (pool);
        hba->priv->id = g_strdup (gdu_adapter_get_native_path (hba->priv->adapter));
        g_signal_connect (adapter, "changed", (GCallback) adapter_changed, hba);
        return hba;
}

static const gchar *
gdu_hba_get_id (GduPresentable *presentable)
{
        GduHba *hba = GDU_HBA (presentable);
        return hba->priv->id;
}

static GduDevice *
gdu_hba_get_device (GduPresentable *presentable)
{
        return NULL;
}

static GduPresentable *
gdu_hba_get_enclosing_presentable (GduPresentable *presentable)
{
        return NULL;
}

static char *
gdu_hba_get_name (GduPresentable *presentable)
{
        GduHba *hba = GDU_HBA (presentable);
        const gchar *fabric;
        gchar *fabric_str;

        fabric = gdu_adapter_get_fabric (hba->priv->adapter);

        if (g_str_has_prefix (fabric, "ata_pata")) {
                fabric_str = g_strdup ("PATA Host Adapter");
        } else if (g_str_has_prefix (fabric, "ata_sata")) {
                fabric_str = g_strdup ("SATA Host Adapter");
        } else if (g_str_has_prefix (fabric, "ata")) {
                fabric_str = g_strdup ("ATA Host Adapter");
        } else if (g_str_has_prefix (fabric, "scsi_sas")) {
                fabric_str = g_strdup ("SAS Host Adapter");
        } else if (g_str_has_prefix (fabric, "scsi")) {
                fabric_str = g_strdup ("SCSI Host Adapter");
        } else {
                fabric_str = g_strdup ("Host Adapter");
        }

        return fabric_str;
}

static gchar *
gdu_hba_get_vpd_name (GduPresentable *presentable)
{
        GduHba *hba = GDU_HBA (presentable);
        gchar *s;
        const gchar *vendor;
        const gchar *model;

        vendor = gdu_adapter_get_vendor (hba->priv->adapter);
        model = gdu_adapter_get_model (hba->priv->adapter);
        //s = g_strdup_printf ("%s %s", vendor, model);
        s = g_strdup (model);
        return s;
}

static gchar *
gdu_hba_get_description (GduPresentable *presentable)
{
        /* TODO: include number of ports, speed, receptable type etc. */
        return gdu_hba_get_vpd_name (presentable);
}

static GIcon *
gdu_hba_get_icon (GduPresentable *presentable)
{
        GIcon *icon;
        icon = g_themed_icon_new_with_default_fallbacks ("gdu-hba");
        return icon;
}

static guint64
gdu_hba_get_offset (GduPresentable *presentable)
{
        return 0;
}

static guint64
gdu_hba_get_size (GduPresentable *presentable)
{
        return 0;
}

static GduPool *
gdu_hba_get_pool (GduPresentable *presentable)
{
        GduHba *hba = GDU_HBA (presentable);
        return gdu_adapter_get_pool (hba->priv->adapter);
}

static gboolean
gdu_hba_is_allocated (GduPresentable *presentable)
{
        return FALSE;
}

static gboolean
gdu_hba_is_recognized (GduPresentable *presentable)
{
        return FALSE;
}

GduAdapter *
gdu_hba_get_adapter (GduHba *hba)
{
        return g_object_ref (hba->priv->adapter);
}

static void
gdu_hba_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_id                    = gdu_hba_get_id;
        iface->get_device                = gdu_hba_get_device;
        iface->get_enclosing_presentable = gdu_hba_get_enclosing_presentable;
        iface->get_name                  = gdu_hba_get_name;
        iface->get_description           = gdu_hba_get_description;
        iface->get_vpd_name              = gdu_hba_get_vpd_name;
        iface->get_icon                  = gdu_hba_get_icon;
        iface->get_offset                = gdu_hba_get_offset;
        iface->get_size                  = gdu_hba_get_size;
        iface->get_pool                  = gdu_hba_get_pool;
        iface->is_allocated              = gdu_hba_is_allocated;
        iface->is_recognized             = gdu_hba_is_recognized;
}
