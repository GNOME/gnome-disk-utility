/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#ifndef __GDU_GRID_DETAILS_H
#define __GDU_GRID_DETAILS_H

#include "gdu-grid-types.h"

G_BEGIN_DECLS

#define GDU_TYPE_GRID_DETAILS         gdu_grid_details_get_type()
#define GDU_GRID_DETAILS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_GRID_DETAILS, GduGridDetails))
#define GDU_GRID_DETAILS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GDU_TYPE_GRID_DETAILS, GduGridDetailsClass))
#define GDU_IS_GRID_DETAILS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_GRID_DETAILS))
#define GDU_IS_GRID_DETAILS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_GRID_DETAILS))
#define GDU_GRID_DETAILS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDU_TYPE_GRID_DETAILS, GduGridDetailsClass))

typedef struct GduGridDetailsClass   GduGridDetailsClass;
typedef struct GduGridDetailsPrivate GduGridDetailsPrivate;

struct GduGridDetails
{
        GtkVBox parent;

        /*< private >*/
        GduGridDetailsPrivate *priv;
};

struct GduGridDetailsClass
{
        GtkVBoxClass parent_class;
};

GType           gdu_grid_details_get_type         (void) G_GNUC_CONST;
GtkWidget      *gdu_grid_details_new              (GduGridView *view);

G_END_DECLS

#endif /* __GDU_GRID_DETAILS_H */
