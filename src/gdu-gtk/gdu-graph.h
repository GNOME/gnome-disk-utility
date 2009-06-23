/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-graph.h
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

#ifndef GDU_GRAPH_H
#define GDU_GRAPH_H

#include <gdu-gtk/gdu-gtk-types.h>

#define GDU_TYPE_GRAPH             (gdu_graph_get_type ())
#define GDU_GRAPH(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_GRAPH, GduGraph))
#define GDU_GRAPH_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_GRAPH,  GduGraphClass))
#define GDU_IS_GRAPH(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_GRAPH))
#define GDU_IS_GRAPH_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_GRAPH))
#define GDU_GRAPH_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_GRAPH, GduGraphClass))

typedef struct GduGraphClass       GduGraphClass;
typedef struct GduGraphPrivate     GduGraphPrivate;

struct GduGraph
{
        GtkDrawingArea parent;

        /* private */
        GduGraphPrivate *priv;
};

struct GduGraphClass
{
        GtkDrawingAreaClass parent_class;
};


GType       gdu_graph_get_type            (void);
GtkWidget  *gdu_graph_new                 (void);
gchar     **gdu_graph_get_x_markers       (GduGraph           *graph);
gchar     **gdu_graph_get_y_markers_left  (GduGraph           *graph);
gchar     **gdu_graph_get_y_markers_right (GduGraph           *graph);
void        gdu_graph_set_x_markers       (GduGraph           *graph,
                                           const gchar* const *markers);
void        gdu_graph_set_y_markers_left  (GduGraph           *graph,
                                           const gchar* const *markers);
void        gdu_graph_set_y_markers_right (GduGraph           *graph,
                                           const gchar* const *markers);

typedef struct GduGraphPoint GduGraphPoint;

struct GduGraphPoint
{
        gfloat x;
        gfloat y;
        gpointer data;
};

gboolean    gdu_graph_remove_curve        (GduGraph           *graph,
                                           const gchar        *curve_id);

void        gdu_graph_set_curve           (GduGraph           *graph,
                                           const gchar        *curve_id,
                                           GdkColor           *color,
                                           GArray             *points);

gboolean    gdu_graph_remove_band         (GduGraph           *graph,
                                           const gchar        *band_id);

void        gdu_graph_set_band            (GduGraph           *graph,
                                           const gchar        *band_id,
                                           GdkColor           *color,
                                           GArray             *points);


#endif /* GDU_GRAPH_H */
