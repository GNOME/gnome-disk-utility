/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#ifndef __GDU_GRID_VIEW_H
#define __GDU_GRID_VIEW_H

#include "gdu-grid-types.h"

G_BEGIN_DECLS

#define GDU_TYPE_GRID_VIEW         gdu_grid_view_get_type()
#define GDU_GRID_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_GRID_VIEW, GduGridView))
#define GDU_GRID_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GDU_TYPE_GRID_VIEW, GduGridViewClass))
#define GDU_IS_GRID_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_GRID_VIEW))
#define GDU_IS_GRID_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_GRID_VIEW))
#define GDU_GRID_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDU_TYPE_GRID_VIEW, GduGridViewClass))

typedef struct GduGridViewClass   GduGridViewClass;
typedef struct GduGridViewPrivate GduGridViewPrivate;

struct GduGridView
{
        GtkVBox parent;

        /*< private >*/
        GduGridViewPrivate *priv;
};

struct GduGridViewClass
{
        GtkVBoxClass parent_class;

        /*< public >*/
        /* signals */
        void (*selection_changed) (GduGridView *view);
};

GType           gdu_grid_view_get_type         (void) G_GNUC_CONST;
GtkWidget      *gdu_grid_view_new              (GduPool        *pool);

gboolean        gdu_grid_view_is_selected      (GduGridView    *view,
                                                GduPresentable *presentable);
GList          *gdu_grid_view_selection_get    (GduGridView    *view);
void            gdu_grid_view_selection_add    (GduGridView    *view,
                                                GduPresentable *presentable);
void            gdu_grid_view_selection_remove (GduGridView    *view,
                                                GduPresentable *presentable);
void            gdu_grid_view_selection_clear  (GduGridView    *view);

G_END_DECLS

#endif /* __GDU_GRID_VIEW_H */
