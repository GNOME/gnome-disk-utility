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

gint64      gdu_graph_get_window_end_usec  (GduGraph     *graph);
gint64      gdu_graph_get_window_size_usec (GduGraph     *graph);
void        gdu_graph_set_window_end_usec  (GduGraph     *graph,
                                            gint64        time_usec);
void        gdu_graph_set_window_size_usec (GduGraph     *graph,
                                            gint64        period_usec);

GduCurve   *gdu_graph_lookup_curve        (GduGraph           *graph,
                                           const gchar        *curve_id);

void        gdu_graph_add_curve           (GduGraph           *graph,
                                           const gchar        *curve_id,
                                           GduCurve           *curve);

gboolean    gdu_graph_remove_curve        (GduGraph           *graph,
                                           const gchar        *curve_id);

#endif /* GDU_GRAPH_H */
