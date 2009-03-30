/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#ifndef __GDU_GRID_HBOX_H
#define __GDU_GRID_HBOX_H

#include "gdu-grid-types.h"

G_BEGIN_DECLS

#define GDU_TYPE_GRID_HBOX         gdu_grid_hbox_get_type()
#define GDU_GRID_HBOX(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_GRID_HBOX, GduGridHBox))
#define GDU_GRID_HBOX_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GDU_TYPE_GRID_HBOX, GduGridHBoxClass))
#define GDU_IS_GRID_HBOX(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_GRID_HBOX))
#define GDU_IS_GRID_HBOX_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_GRID_HBOX))
#define GDU_GRID_HBOX_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDU_TYPE_GRID_HBOX, GduGridHBoxClass))

typedef struct GduGridHBoxClass   GduGridHBoxClass;
typedef struct GduGridHBoxPrivate GduGridHBoxPrivate;

struct GduGridHBox
{
        GtkHBox parent;

        /*< private >*/
        GduGridHBoxPrivate *priv;
};

struct GduGridHBoxClass
{
        GtkHBoxClass parent_class;
};

GType       gdu_grid_hbox_get_type (void) G_GNUC_CONST;
GtkWidget*  gdu_grid_hbox_new      (void);

G_END_DECLS

#endif /* __GDU_GRID_HBOX_H */
