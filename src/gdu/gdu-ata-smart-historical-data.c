/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ata-smart-historical-data.c
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
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <time.h>

#include "gdu-private.h"
#include "gdu-ata-smart-historical-data.h"
#include "gdu-ata-smart-attribute.h"

struct _GduAtaSmartHistoricalDataPrivate {
        guint64 time_collected;
        gboolean is_failing;
        gboolean is_failing_valid;
        gboolean has_bad_sectors;
        gboolean has_bad_attributes;
        gdouble temperature_kelvin;
        guint64 power_on_seconds;
        GList *attrs;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduAtaSmartHistoricalData, gdu_ata_smart_historical_data, G_TYPE_OBJECT);

static void
gdu_ata_smart_historical_data_finalize (GduAtaSmartHistoricalData *ata_smart_historical_data)
{
        g_list_foreach (ata_smart_historical_data->priv->attrs, (GFunc) g_object_unref, NULL);
        g_list_free (ata_smart_historical_data->priv->attrs);
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (ata_smart_historical_data));
}

static void
gdu_ata_smart_historical_data_class_init (GduAtaSmartHistoricalDataClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_ata_smart_historical_data_finalize;

        g_type_class_add_private (klass, sizeof (GduAtaSmartHistoricalDataPrivate));
}

static void
gdu_ata_smart_historical_data_init (GduAtaSmartHistoricalData *ata_smart_historical_data)
{
        ata_smart_historical_data->priv = G_TYPE_INSTANCE_GET_PRIVATE (ata_smart_historical_data, GDU_TYPE_ATA_SMART_HISTORICAL_DATA, GduAtaSmartHistoricalDataPrivate);
}

guint64
gdu_ata_smart_historical_data_get_time_collected (GduAtaSmartHistoricalData *data)
{
        return data->priv->time_collected;
}

gboolean
gdu_ata_smart_historical_data_get_is_failing (GduAtaSmartHistoricalData *data)
{
        return data->priv->is_failing;
}

gboolean
gdu_ata_smart_historical_data_get_is_failing_valid (GduAtaSmartHistoricalData *data)
{
        return data->priv->is_failing_valid;
}

gboolean
gdu_ata_smart_historical_data_get_has_bad_sectors (GduAtaSmartHistoricalData *data)
{
        return data->priv->has_bad_sectors;
}

gboolean
gdu_ata_smart_historical_data_get_has_bad_attributes (GduAtaSmartHistoricalData *data)
{
        return data->priv->has_bad_attributes;
}

gdouble
gdu_ata_smart_historical_data_get_temperature_kelvin (GduAtaSmartHistoricalData *data)
{
        return data->priv->temperature_kelvin;
}

guint64
gdu_ata_smart_historical_data_get_power_on_seconds (GduAtaSmartHistoricalData *data)
{
        return data->priv->power_on_seconds;
}

GList *
gdu_ata_smart_historical_data_get_attributes (GduAtaSmartHistoricalData *data)
{
        GList *ret;
        ret = g_list_copy (data->priv->attrs);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}

GduAtaSmartAttribute *
gdu_ata_smart_historical_data_get_attribute (GduAtaSmartHistoricalData *data,
                                             const gchar               *attr_name)
{
        GList *l;
        GduAtaSmartAttribute *ret;

        /* TODO: if this is slow we can do a hash table */

        ret = NULL;

        for (l = data->priv->attrs; l != NULL; l = l->next) {
                GduAtaSmartAttribute *a = GDU_ATA_SMART_ATTRIBUTE (l->data);
                if (g_strcmp0 (attr_name, gdu_ata_smart_attribute_get_name (a)) == 0) {
                        ret = g_object_ref (a);
                        goto out;
                }
        }
out:
        return ret;
}

GduAtaSmartHistoricalData *
_gdu_ata_smart_historical_data_new (gpointer data)
{
        GduAtaSmartHistoricalData *ret;
        GValue elem0 = {0};
        GPtrArray *attrs;
        int n;

        ret = GDU_ATA_SMART_HISTORICAL_DATA (g_object_new (GDU_TYPE_ATA_SMART_HISTORICAL_DATA, NULL));

        g_value_init (&elem0, ATA_SMART_HISTORICAL_DATA_STRUCT_TYPE);
        g_value_set_static_boxed (&elem0, data);
        dbus_g_type_struct_get (&elem0,
                                0, &(ret->priv->time_collected),
                                1, &(ret->priv->is_failing),
                                2, &(ret->priv->is_failing_valid),
                                3, &(ret->priv->has_bad_sectors),
                                4, &(ret->priv->has_bad_attributes),
                                5, &(ret->priv->temperature_kelvin),
                                6, &(ret->priv->power_on_seconds),
                                7, &attrs,
                                G_MAXUINT);

        ret->priv->attrs = NULL;
        if (attrs != NULL) {
                for (n = 0; n < (int) attrs->len; n++) {
                        ret->priv->attrs = g_list_prepend (ret->priv->attrs,
                                                           _gdu_ata_smart_attribute_new (attrs->pdata[n]));
                }
                ret->priv->attrs = g_list_reverse (ret->priv->attrs);
        }

        return ret;
}
