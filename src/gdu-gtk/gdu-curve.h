/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-curve.h
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

#if !defined (__GDU_GTK_INSIDE_GDU_GTK_H) && !defined (GDU_GTK_COMPILATION)
#error "Only <gdu-gtk/gdu-gtk.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef GDU_CURVE_H
#define GDU_CURVE_H

#include <gdu-gtk/gdu-gtk-types.h>

#define GDU_TYPE_CURVE             (gdu_curve_get_type ())
#define GDU_CURVE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_CURVE, GduCurve))
#define GDU_CURVE_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_CURVE,  GduCurveClass))
#define GDU_IS_CURVE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_CURVE))
#define GDU_IS_CURVE_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_CURVE))
#define GDU_CURVE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_CURVE, GduCurveClass))

typedef struct GduCurveClass       GduCurveClass;
typedef struct GduCurvePrivate     GduCurvePrivate;

struct GduCurve
{
        GObject parent;

        /* private */
        GduCurvePrivate *priv;
};

struct GduCurveClass
{
        GObjectClass parent_class;
};

GType          gdu_curve_get_type            (void) G_GNUC_CONST;
GduCurve      *gdu_curve_new                 (void);

GduCurveFlags  gdu_curve_get_flags           (GduCurve      *curve);
GArray        *gdu_curve_get_points          (GduCurve      *curve);
gint           gdu_curve_get_z_order         (GduCurve      *curve);
GduColor      *gdu_curve_get_color           (GduCurve      *curve);
GduColor      *gdu_curve_get_fill_color      (GduCurve      *curve);
gdouble        gdu_curve_get_width           (GduCurve      *curve);
const gchar   *gdu_curve_get_legend          (GduCurve      *curve);

void           gdu_curve_set_flags           (GduCurve      *curve,
                                              GduCurveFlags  flags);
void           gdu_curve_set_points          (GduCurve      *curve,
                                              GArray        *points);
void           gdu_curve_set_z_order         (GduCurve      *curve,
                                              gint           z_order);
void           gdu_curve_set_color           (GduCurve      *curve,
                                              GduColor      *color);
void           gdu_curve_set_fill_color      (GduCurve      *curve,
                                              GduColor      *color);
void           gdu_curve_set_width           (GduCurve      *curve,
                                              gdouble        line_width);
void           gdu_curve_set_legend          (GduCurve      *curve,
                                              const gchar   *text);

/* ----------------------------------------------------------------------------------------------------  */

struct GduPoint
{
        gdouble x;
        gdouble y;
};

#define GDU_TYPE_POINT (gdu_point_get_type ())
GType     gdu_point_get_type (void) G_GNUC_CONST;
GduPoint *gdu_point_dup      (GduPoint *point);
void      gdu_point_free     (GduPoint *point);

struct GduColor
{
        gdouble red;
        gdouble green;
        gdouble blue;
        gdouble alpha;
};

#define GDU_TYPE_COLOR (gdu_color_get_type ())
GType     gdu_color_get_type (void) G_GNUC_CONST;
GduColor *gdu_color_dup      (GduColor *color);
void      gdu_color_free     (GduColor *color);


#endif /* GDU_CURVE_H */
