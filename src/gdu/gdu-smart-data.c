/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-smart-data.c
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
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <time.h>

#include "gdu-private.h"
#include "gdu-smart-data.h"
#include "gdu-smart-data-attribute.h"

struct _GduSmartDataPrivate {
        guint64 time_collected;
        double temperature;
        guint64 time_powered_on;
        char *last_self_test_result;
        gboolean is_failing;
        GList *attrs;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSmartData, gdu_smart_data, G_TYPE_OBJECT);

static void
gdu_smart_data_finalize (GduSmartData *smart_data)
{
        g_free (smart_data->priv->last_self_test_result);
        g_list_foreach (smart_data->priv->attrs, (GFunc) g_object_unref, NULL);
        g_list_free (smart_data->priv->attrs);
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (smart_data));
}

static void
gdu_smart_data_class_init (GduSmartDataClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_smart_data_finalize;

        g_type_class_add_private (klass, sizeof (GduSmartDataPrivate));
}

static void
gdu_smart_data_init (GduSmartData *smart_data)
{
        smart_data->priv = G_TYPE_INSTANCE_GET_PRIVATE (smart_data, GDU_TYPE_SMART_DATA, GduSmartDataPrivate);
}

guint64
gdu_smart_data_get_time_collected (GduSmartData *smart_data)
{
        return smart_data->priv->time_collected;
}

double
gdu_smart_data_get_temperature (GduSmartData *smart_data)
{
        return smart_data->priv->temperature;
}

guint64
gdu_smart_data_get_time_powered_on (GduSmartData *smart_data)
{
        return smart_data->priv->time_powered_on;
}

char *
gdu_smart_data_get_last_self_test_result (GduSmartData *smart_data)
{
        return g_strdup (smart_data->priv->last_self_test_result);
}

gboolean
gdu_smart_data_get_attribute_warning (GduSmartData *smart_data)
{
        GList *l;
        gboolean ret;

        ret = FALSE;
        for (l = smart_data->priv->attrs; l != NULL; l = l->next) {
                GduSmartDataAttribute *attr = GDU_SMART_DATA_ATTRIBUTE (l->data);
                if (gdu_smart_data_attribute_is_warning (attr)) {
                        ret = TRUE;
                        goto out;
                }
        }
out:
        return ret;
}

gboolean
gdu_smart_data_get_attribute_failing (GduSmartData *smart_data)
{
        GList *l;
        gboolean ret;

        ret = FALSE;
        for (l = smart_data->priv->attrs; l != NULL; l = l->next) {
                GduSmartDataAttribute *attr = GDU_SMART_DATA_ATTRIBUTE (l->data);
                if (gdu_smart_data_attribute_is_failing (attr)) {
                        ret = TRUE;
                        goto out;
                }
        }
out:
        return ret;
}

gboolean
gdu_smart_data_get_is_failing (GduSmartData *smart_data)
{
        return smart_data->priv->is_failing;
}

GList *
gdu_smart_data_get_attributes (GduSmartData *smart_data)
{
        GList *ret;
        ret = g_list_copy (smart_data->priv->attrs);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}

GduSmartDataAttribute *
gdu_smart_data_get_attribute (GduSmartData *smart_data,
                              int id)
{
        GList *l;
        GduSmartDataAttribute *ret;

        /* TODO: if this is slow we can do a hash table */

        ret = NULL;

        for (l = smart_data->priv->attrs; l != NULL; l = l->next) {
                GduSmartDataAttribute *a = l->data;
                if (gdu_smart_data_attribute_get_id (a) == id) {
                        ret = g_object_ref (a);
                        goto out;
                }
        }
out:
        return ret;
}

GduSmartData *
_gdu_smart_data_new (gpointer data)
{
        GduSmartData *smart_data;
        GValue elem0 = {0};
        GPtrArray *attrs;
        int n;

        smart_data = GDU_SMART_DATA (g_object_new (GDU_TYPE_SMART_DATA, NULL));

        g_value_init (&elem0, HISTORICAL_SMART_DATA_STRUCT_TYPE);
        g_value_set_static_boxed (&elem0, data);
        dbus_g_type_struct_get (&elem0,
                                0, &(smart_data->priv->time_collected),
                                1, &(smart_data->priv->temperature),
                                2, &(smart_data->priv->time_powered_on),
                                3, &(smart_data->priv->last_self_test_result),
                                4, &(smart_data->priv->is_failing),
                                5, &attrs,
                                G_MAXUINT);

        smart_data->priv->attrs = NULL;
        if (attrs != NULL) {
                for (n = 0; n < (int) attrs->len; n++) {
                        smart_data->priv->attrs = g_list_prepend (smart_data->priv->attrs,
                                                                  _gdu_smart_data_attribute_new (attrs->pdata[n]));
                }
                smart_data->priv->attrs = g_list_reverse (smart_data->priv->attrs);
        }

        return smart_data;
}

GduSmartData *
_gdu_smart_data_new_from_values (guint64 time_collected,
                                 double temperature,
                                 guint64 time_powered_on,
                                 const char *last_self_test_result,
                                 gboolean is_failing,
                                 GPtrArray *attrs)
{
        GduSmartData *smart_data;
        int n;

        smart_data = GDU_SMART_DATA (g_object_new (GDU_TYPE_SMART_DATA, NULL));
        smart_data->priv->time_collected = time_collected;
        smart_data->priv->temperature = temperature;
        smart_data->priv->time_powered_on = time_powered_on;
        smart_data->priv->last_self_test_result = g_strdup (last_self_test_result);
        smart_data->priv->is_failing = is_failing;

        smart_data->priv->attrs = NULL;
        if (attrs != NULL) {
                for (n = 0; n < (int) attrs->len; n++) {
                        smart_data->priv->attrs = g_list_prepend (smart_data->priv->attrs,
                                                                  _gdu_smart_data_attribute_new (attrs->pdata[n]));
                }
                smart_data->priv->attrs = g_list_reverse (smart_data->priv->attrs);
        }
        return smart_data;
}
