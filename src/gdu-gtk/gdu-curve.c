/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-curve.c
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
#include <glib/gi18n.h>
#include <string.h>
#include <math.h>

#include <gdu-gtk/gdu-gtk-enumtypes.h>

#include "gdu-curve.h"

struct GduCurvePrivate
{
        GduCurveFlags flags;
        GArray *points;
        gint z_order;
        GduColor *color;
        GduColor *fill_color;
        gdouble width;
        gchar *legend;
};

G_DEFINE_TYPE (GduCurve, gdu_curve, G_TYPE_OBJECT)

enum
{
        PROP_0,
        PROP_FLAGS,
        PROP_POINTS,
        PROP_Z_ORDER,
        PROP_COLOR,
        PROP_FILL_COLOR,
        PROP_WIDTH,
        PROP_LEGEND,
};

static void
gdu_curve_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
        GduCurve *curve = GDU_CURVE (object);

        switch (prop_id) {
        case PROP_FLAGS:
                gdu_curve_set_flags (curve, g_value_get_flags (value));
                break;

        case PROP_POINTS:
                gdu_curve_set_points (curve, g_value_get_boxed (value));
                break;

        case PROP_Z_ORDER:
                gdu_curve_set_z_order (curve, g_value_get_int (value));
                break;

        case PROP_COLOR:
                gdu_curve_set_color (curve, g_value_get_boxed (value));
                break;

        case PROP_FILL_COLOR:
                gdu_curve_set_fill_color (curve, g_value_get_boxed (value));
                break;

        case PROP_WIDTH:
                gdu_curve_set_width (curve, g_value_get_double (value));
                break;

        case PROP_LEGEND:
                gdu_curve_set_legend (curve, g_value_get_string (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_curve_get_property (GObject     *object,
                        guint        prop_id,
                        GValue      *value,
                        GParamSpec  *pspec)
{
        GduCurve *curve = GDU_CURVE (object);

        switch (prop_id) {
        case PROP_FLAGS:
                g_value_set_flags (value, gdu_curve_get_flags (curve));
                break;

        case PROP_POINTS:
                g_value_set_boxed (value, gdu_curve_get_points (curve));
                break;

        case PROP_Z_ORDER:
                g_value_set_int (value, gdu_curve_get_z_order (curve));
                break;

        case PROP_COLOR:
                g_value_set_boxed (value, gdu_curve_get_color (curve));
                break;

        case PROP_FILL_COLOR:
                g_value_set_boxed (value, gdu_curve_get_fill_color (curve));
                break;

        case PROP_WIDTH:
                g_value_set_double (value, gdu_curve_get_width (curve));
                break;

        case PROP_LEGEND:
                g_value_set_string (value, gdu_curve_get_legend (curve));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_curve_finalize (GObject *object)
{
        GduCurve *curve = GDU_CURVE (object);

        if (curve->priv->color != NULL)
                gdu_color_free (curve->priv->color);
        if (curve->priv->fill_color != NULL)
                gdu_color_free (curve->priv->fill_color);
        if (curve->priv->points != NULL)
                g_array_unref (curve->priv->points);

        g_free (curve->priv->legend);

        if (G_OBJECT_CLASS (gdu_curve_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_curve_parent_class)->finalize (object);
}

static void
gdu_curve_constructed (GObject *object)
{
        GduCurve *curve = GDU_CURVE (object);

        /* default to an orange color if not set */
        if (curve->priv->color == NULL) {
                GduColor orange = { 0.5, 0.7, 0.85, 1.0 };
                curve->priv->color = gdu_color_dup (&orange);
        }

        if (G_OBJECT_CLASS (gdu_curve_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_curve_parent_class)->constructed (object);
}

static void
gdu_curve_class_init (GduCurveClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->finalize     = gdu_curve_finalize;
        gobject_class->constructed  = gdu_curve_constructed;
        gobject_class->set_property = gdu_curve_set_property;
        gobject_class->get_property = gdu_curve_get_property;

        g_type_class_add_private (klass, sizeof (GduCurvePrivate));

        g_object_class_install_property (gobject_class,
                                         PROP_FLAGS,
                                         g_param_spec_flags ("flags",
                                                             _("Flags"),
                                                             _("The flags for the curve"),
                                                             GDU_TYPE_CURVE_FLAGS,
                                                             GDU_CURVE_FLAGS_NONE,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE |
                                                             G_PARAM_CONSTRUCT));

        g_object_class_install_property (gobject_class,
                                         PROP_POINTS,
                                         g_param_spec_boxed ("points",
                                                             _("Points"),
                                                             _("The points of the curve"),
                                                             G_TYPE_ARRAY,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE));

        g_object_class_install_property (gobject_class,
                                         PROP_Z_ORDER,
                                         g_param_spec_int ("z-order",
                                                           _("Z order"),
                                                           _("Z order of the curve"),
                                                           G_MININT,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READABLE |
                                                           G_PARAM_WRITABLE |
                                                           G_PARAM_CONSTRUCT));

        g_object_class_install_property (gobject_class,
                                         PROP_COLOR,
                                         g_param_spec_boxed ("color",
                                                             _("Color"),
                                                             _("The color of the curve"),
                                                             GDU_TYPE_COLOR,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE));

        g_object_class_install_property (gobject_class,
                                         PROP_FILL_COLOR,
                                         g_param_spec_boxed ("fill-color",
                                                             _("Fill Color"),
                                                             _("The color of the interior of the curve"),
                                                             GDU_TYPE_COLOR,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE));

        g_object_class_install_property (gobject_class,
                                         PROP_WIDTH,
                                         g_param_spec_double ("width",
                                                              _("Width"),
                                                              _("The width of the line for the curve"),
                                                              0.0,
                                                              G_MAXDOUBLE,
                                                              1.0,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT));

        g_object_class_install_property (gobject_class,
                                         PROP_LEGEND,
                                         g_param_spec_string ("legend",
                                                              _("Legend"),
                                                              _("The text to show in the legend"),
                                                              NULL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT));

}

static void
gdu_curve_init (GduCurve *curve)
{
        curve->priv = G_TYPE_INSTANCE_GET_PRIVATE (curve, GDU_TYPE_CURVE, GduCurvePrivate);
}

GduCurve *
gdu_curve_new (void)
{
        return GDU_CURVE (g_object_new (GDU_TYPE_CURVE, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

GduCurveFlags
gdu_curve_get_flags (GduCurve *curve)
{
        g_return_val_if_fail (GDU_IS_CURVE (curve), G_MAXINT);
        return curve->priv->flags;
}

GArray *
gdu_curve_get_points (GduCurve *curve)
{
        g_return_val_if_fail (GDU_IS_CURVE (curve), NULL);
        return curve->priv->points;
}

gint
gdu_curve_get_z_order (GduCurve *curve)
{
        g_return_val_if_fail (GDU_IS_CURVE (curve), G_MAXINT);
        return curve->priv->z_order;
}

GduColor *
gdu_curve_get_color (GduCurve *curve)
{
        g_return_val_if_fail (GDU_IS_CURVE (curve), NULL);
        return curve->priv->color;
}

GduColor *
gdu_curve_get_fill_color (GduCurve *curve)
{
        g_return_val_if_fail (GDU_IS_CURVE (curve), NULL);
        return curve->priv->fill_color;
}

gdouble
gdu_curve_get_width (GduCurve *curve)
{
        g_return_val_if_fail (GDU_IS_CURVE (curve), -G_MAXDOUBLE);
        return curve->priv->width;
}

const gchar *
gdu_curve_get_legend (GduCurve *curve)
{
        g_return_val_if_fail (GDU_IS_CURVE (curve), NULL);
        return curve->priv->legend;
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_curve_set_flags (GduCurve     *curve,
                    GduCurveFlags  flags)
{
        g_return_if_fail (GDU_IS_CURVE (curve));
        curve->priv->flags = flags;
}

void
gdu_curve_set_points (GduCurve *curve,
                      GArray   *points)
{
        g_return_if_fail (GDU_IS_CURVE (curve));

        if (curve->priv->points != NULL)
                g_array_unref (curve->priv->points);
        curve->priv->points = points != NULL ? g_array_ref (points) : NULL;
}

void
gdu_curve_set_z_order (GduCurve *curve,
                       gint      z_order)
{
        g_return_if_fail (GDU_IS_CURVE (curve));

        curve->priv->z_order = z_order;
}

void
gdu_curve_set_color (GduCurve *curve,
                     GduColor *color)
{
        g_return_if_fail (GDU_IS_CURVE (curve));
        g_return_if_fail (color != NULL);

        if (curve->priv->color != NULL)
                gdu_color_free (curve->priv->color);
        curve->priv->color = gdu_color_dup (color);
}

void
gdu_curve_set_fill_color (GduCurve *curve,
                          GduColor *color)
{
        g_return_if_fail (GDU_IS_CURVE (curve));
        g_return_if_fail (color != NULL);

        if (curve->priv->fill_color != NULL)
                gdu_color_free (curve->priv->fill_color);
        curve->priv->fill_color = gdu_color_dup (color);
}

void
gdu_curve_set_width (GduCurve *curve,
                          gdouble   width)
{
        g_return_if_fail (GDU_IS_CURVE (curve));
        g_return_if_fail (width >= 0);

        curve->priv->width = width;
}

void
gdu_curve_set_legend (GduCurve    *curve,
                      const gchar *text)
{
        g_return_if_fail (GDU_IS_CURVE (curve));

        g_free (curve->priv->legend);
        curve->priv->legend = g_strdup (text);
}


/* ---------------------------------------------------------------------------------------------------- */

GduColor *
gdu_color_dup (GduColor *color)
{
        GduColor *c;
        c = g_memdup (color, sizeof (GduColor));
        return c;
}

void
gdu_color_free (GduColor *color)
{
        g_free (color);
}

GType
gdu_color_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      GType g_define_type_id =
        g_boxed_type_register_static ("GduColor",
                                      (GBoxedCopyFunc) gdu_color_dup,
                                      (GBoxedFreeFunc) gdu_color_free);

      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}

GduPoint *
gdu_point_dup (GduPoint *point)
{
        GduPoint *p;
        p = g_memdup (point, sizeof (GduPoint));
        return p;
}

void
gdu_point_free (GduPoint *point)
{
        g_free (point);
}

GType
gdu_point_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;

  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      GType g_define_type_id =
        g_boxed_type_register_static ("GduPoint",
                                      (GBoxedCopyFunc) gdu_point_dup,
                                      (GBoxedFreeFunc) gdu_point_free);

      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

  return g_define_type_id__volatile;
}
