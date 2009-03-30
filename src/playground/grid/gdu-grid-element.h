/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#ifndef __GDU_GRID_ELEMENT_H
#define __GDU_GRID_ELEMENT_H

#include "gdu-grid-types.h"

G_BEGIN_DECLS

#define GDU_TYPE_GRID_ELEMENT         gdu_grid_element_get_type()
#define GDU_GRID_ELEMENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_GRID_ELEMENT, GduGridElement))
#define GDU_GRID_ELEMENT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GDU_TYPE_GRID_ELEMENT, GduGridElementClass))
#define GDU_IS_GRID_ELEMENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_GRID_ELEMENT))
#define GDU_IS_GRID_ELEMENT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_GRID_ELEMENT))
#define GDU_GRID_ELEMENT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDU_TYPE_GRID_ELEMENT, GduGridElementClass))

typedef struct GduGridElementClass   GduGridElementClass;
typedef struct GduGridElementPrivate GduGridElementPrivate;

struct GduGridElement
{
        GtkDrawingArea parent;

        /*< private >*/
        GduGridElementPrivate *priv;
};

struct GduGridElementClass
{
        GtkDrawingAreaClass parent_class;
};

typedef enum
{
        GDU_GRID_ELEMENT_FLAGS_NONE        = 0,
        GDU_GRID_ELEMENT_FLAGS_EDGE_TOP    = (1<<0),
        GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM = (1<<1),
        GDU_GRID_ELEMENT_FLAGS_EDGE_LEFT   = (1<<2),
        GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT  = (1<<3)
} GduGridElementFlags;

GType           gdu_grid_element_get_type         (void) G_GNUC_CONST;
GtkWidget*      gdu_grid_element_new              (GduGridView        *view,
                                                   GduPresentable     *presentable,
                                                   guint               minimum_size,
                                                   gdouble             percent_size,
                                                   GduGridElementFlags flags);
GduGridView        *gdu_grid_element_get_view         (GduGridElement *element);
GduPresentable     *gdu_grid_element_get_presentable  (GduGridElement *element);
guint               gdu_grid_element_get_minimum_size (GduGridElement *element);
gdouble             gdu_grid_element_get_percent_size (GduGridElement *element);
GduGridElementFlags gdu_grid_element_get_flags        (GduGridElement *element);

G_END_DECLS



#endif /* __GDU_GRID_ELEMENT_H */
