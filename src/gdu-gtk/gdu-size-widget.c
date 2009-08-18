/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#define _GNU_SOURCE

#include "config.h"

#include <glib/gi18n-lib.h>
#include <math.h>

#include "gdu-size-widget.h"

struct GduSizeWidgetPrivate
{
        guint64 size;
        guint64 min_size;
        guint64 max_size;
        GtkWidget *hscale;
};

enum
{
        PROP_0,
        PROP_SIZE,
        PROP_MIN_SIZE,
        PROP_MAX_SIZE,
};

enum
{
        CHANGED_SIGNAL,
        LAST_SIGNAL,
};

guint signals[LAST_SIGNAL] = {0,};

G_DEFINE_TYPE (GduSizeWidget, gdu_size_widget, GTK_TYPE_HBOX)

static void update_stepping (GduSizeWidget *widget);

static void
gdu_size_widget_finalize (GObject *object)
{
        //GduSizeWidget *widget = GDU_SIZE_WIDGET (object);

        if (G_OBJECT_CLASS (gdu_size_widget_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_size_widget_parent_class)->finalize (object);
}

static void
gdu_size_widget_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        GduSizeWidget *widget = GDU_SIZE_WIDGET (object);

        switch (property_id) {
        case PROP_SIZE:
                g_value_set_uint64 (value, widget->priv->size);
                break;

        case PROP_MIN_SIZE:
                g_value_set_uint64 (value, widget->priv->min_size);
                break;

        case PROP_MAX_SIZE:
                g_value_set_uint64 (value, widget->priv->max_size);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
gdu_size_widget_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
        GduSizeWidget *widget = GDU_SIZE_WIDGET (object);

        switch (property_id) {
        case PROP_SIZE:
                gdu_size_widget_set_size (widget, g_value_get_uint64 (value));
                break;

        case PROP_MIN_SIZE:
                gdu_size_widget_set_min_size (widget, g_value_get_uint64 (value));
                break;

        case PROP_MAX_SIZE:
                gdu_size_widget_set_max_size (widget, g_value_get_uint64 (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static gchar *
on_hscale_format_value (GtkScale *scale,
                        gdouble   value,
                        gpointer  user_data)
{
        gchar *ret;

        ret = gdu_util_get_size_for_display ((guint64 ) value, FALSE);

        return ret;
}

static void
on_hscale_value_changed (GtkRange  *range,
                         gpointer   user_data)
{
        GduSizeWidget *widget = GDU_SIZE_WIDGET (user_data);
        guint64 old_size;

        old_size = widget->priv->size;
        widget->priv->size = (guint64) gtk_range_get_value (range);

        if (old_size != widget->priv->size) {
                g_signal_emit (widget,
                               signals[CHANGED_SIGNAL],
                               0);
                g_object_notify (G_OBJECT (widget), "size");
        }
}

static void
gdu_size_widget_init (GduSizeWidget *widget)
{
        widget->priv = G_TYPE_INSTANCE_GET_PRIVATE (widget,
                                                    GDU_TYPE_SIZE_WIDGET,
                                                    GduSizeWidgetPrivate);

        widget->priv->hscale = gtk_hscale_new_with_range (0,
                                                          10,
                                                          1);
        gtk_scale_set_draw_value (GTK_SCALE (widget->priv->hscale), TRUE);
}

static gboolean
on_query_tooltip (GtkWidget  *w,
                  gint        x,
                  gint        y,
                  gboolean    keyboard_mode,
                  GtkTooltip *tooltip,
                  gpointer    user_data)
{
        GduSizeWidget *widget = GDU_SIZE_WIDGET (w);
        gchar *s;
        gchar *s1;

        s1 = gdu_util_get_size_for_display (widget->priv->size, TRUE);
        /* TODO: handle this use-case
        s = g_strdup_printf ("<b>%s</b>\n"
                             "\n"
                             "%s",
                             s1,
                             _("Right click to specify an exact size."));*/
        s = g_strdup_printf ("%s", s1);
        g_free (s1);
        gtk_tooltip_set_markup (tooltip, s);
        g_free (s);

        return TRUE;
}

static void
gdu_size_widget_constructed (GObject *object)
{
        GduSizeWidget *widget = GDU_SIZE_WIDGET (object);

        gtk_widget_show (widget->priv->hscale);
        gtk_box_pack_start (GTK_BOX (widget),
                            widget->priv->hscale,
                            TRUE,
                            TRUE,
                            0);

        g_signal_connect (widget->priv->hscale,
                          "format-value",
                          G_CALLBACK (on_hscale_format_value),
                          widget);
        g_signal_connect (widget->priv->hscale,
                          "value-changed",
                          G_CALLBACK (on_hscale_value_changed),
                          widget);

        gtk_widget_set_has_tooltip (GTK_WIDGET (widget),
                                    TRUE);
        g_signal_connect (widget,
                          "query-tooltip",
                          G_CALLBACK (on_query_tooltip),
                          widget);

        update_stepping (widget);

        if (G_OBJECT_CLASS (gdu_size_widget_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_size_widget_parent_class)->constructed (object);
}

static void
gdu_size_widget_class_init (GduSizeWidgetClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduSizeWidgetPrivate));

        gobject_class->get_property        = gdu_size_widget_get_property;
        gobject_class->set_property        = gdu_size_widget_set_property;
        gobject_class->constructed         = gdu_size_widget_constructed;
        gobject_class->finalize            = gdu_size_widget_finalize;

        g_object_class_install_property (gobject_class,
                                         PROP_SIZE,
                                         g_param_spec_uint64 ("size",
                                                              _("Size"),
                                                              _("The currently selected size"),
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READWRITE |
                                                              G_PARAM_CONSTRUCT |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (gobject_class,
                                         PROP_MIN_SIZE,
                                         g_param_spec_uint64 ("min-size",
                                                              _("Minimum Size"),
                                                              _("The minimum size that can be selected"),
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READWRITE |
                                                              G_PARAM_CONSTRUCT |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (gobject_class,
                                         PROP_MAX_SIZE,
                                         g_param_spec_uint64 ("max-size",
                                                              _("Maximum Size"),
                                                              _("The maximum size that can be selected"),
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READWRITE |
                                                              G_PARAM_CONSTRUCT |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                                G_TYPE_FROM_CLASS (klass),
                                                G_SIGNAL_RUN_LAST,
                                                G_STRUCT_OFFSET (GduSizeWidgetClass, changed),
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_VOID__VOID,
                                                G_TYPE_NONE,
                                                0);
}

GtkWidget *
gdu_size_widget_new (guint64 size,
                     guint64 min_size,
                     guint64 max_size)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SIZE_WIDGET,
                                         "size", size,
                                         "min-size", min_size,
                                         "max-size", max_size,
                                         NULL));
}

static void
update_stepping (GduSizeWidget *widget)
{
        gdouble extent;

        extent = widget->priv->max_size - widget->priv->min_size;

        /* set steps in hscale according to magnitude of extent und so weiter */
        if (extent > 0) {
                gdouble increment;
                increment = exp10 (floor (log10 (extent))) / 10.0;
                gtk_range_set_increments (GTK_RANGE (widget->priv->hscale),
                                          increment,
                                          increment * 10.0);
        }


        /* add markers at 0, 25%, 50%, 75% and 100% */
        gtk_scale_clear_marks (GTK_SCALE (widget->priv->hscale));
        gtk_scale_add_mark (GTK_SCALE (widget->priv->hscale),
                            widget->priv->min_size,
                            GTK_POS_BOTTOM,
                            NULL);
        gtk_scale_add_mark (GTK_SCALE (widget->priv->hscale),
                            widget->priv->min_size + extent * 0.25,
                            GTK_POS_BOTTOM,
                            NULL);
        gtk_scale_add_mark (GTK_SCALE (widget->priv->hscale),
                            widget->priv->min_size + extent * 0.50,
                            GTK_POS_BOTTOM,
                            NULL);
        gtk_scale_add_mark (GTK_SCALE (widget->priv->hscale),
                            widget->priv->min_size + extent * 0.75,
                            GTK_POS_BOTTOM,
                            NULL);
        gtk_scale_add_mark (GTK_SCALE (widget->priv->hscale),
                            widget->priv->min_size + extent * 1.0,
                            GTK_POS_BOTTOM,
                            NULL);
}

void
gdu_size_widget_set_size (GduSizeWidget *widget,
                          guint64        size)
{
        g_return_if_fail (GDU_IS_SIZE_WIDGET (widget));
        if (widget->priv->size != size) {
                gtk_range_set_value (GTK_RANGE (widget->priv->hscale),
                                     size);
        }
}

void
gdu_size_widget_set_min_size (GduSizeWidget *widget,
                              guint64        min_size)
{
        g_return_if_fail (GDU_IS_SIZE_WIDGET (widget));
        if (widget->priv->min_size != min_size) {
                widget->priv->min_size = min_size;
                gtk_range_set_range (GTK_RANGE (widget->priv->hscale),
                                     widget->priv->min_size,
                                     widget->priv->max_size);
                update_stepping (widget);
                g_signal_emit (widget,
                               signals[CHANGED_SIGNAL],
                               0);
                g_object_notify (G_OBJECT (widget), "min-size");
        }
}

void
gdu_size_widget_set_max_size (GduSizeWidget *widget,
                              guint64        max_size)
{
        g_return_if_fail (GDU_IS_SIZE_WIDGET (widget));
        if (widget->priv->max_size != max_size) {
                widget->priv->max_size = max_size;
                gtk_range_set_range (GTK_RANGE (widget->priv->hscale),
                                     widget->priv->min_size,
                                     widget->priv->max_size);
                update_stepping (widget);
                g_signal_emit (widget,
                               signals[CHANGED_SIGNAL],
                               0);
                g_object_notify (G_OBJECT (widget), "max-size");
        }
}

guint64
gdu_size_widget_get_size     (GduSizeWidget *widget)
{
        g_return_val_if_fail (GDU_IS_SIZE_WIDGET (widget), 0);
        return widget->priv->size;
}

guint64
gdu_size_widget_get_min_size (GduSizeWidget *widget)
{
        g_return_val_if_fail (GDU_IS_SIZE_WIDGET (widget), 0);
        return widget->priv->min_size;
}

guint64
gdu_size_widget_get_max_size (GduSizeWidget *widget)
{
        g_return_val_if_fail (GDU_IS_SIZE_WIDGET (widget), 0);
        return widget->priv->max_size;
}

