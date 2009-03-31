/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#include <glib/gi18n.h>

#include "gdu-grid-hbox.h"
#include "gdu-grid-element.h"

struct GduGridHBoxPrivate
{
        guint dummy;
};

G_DEFINE_TYPE (GduGridHBox, gdu_grid_hbox, GTK_TYPE_HBOX)

static void
gdu_grid_hbox_finalize (GObject *object)
{
        //GduGridHBox *hbox = GDU_GRID_HBOX (object);

        if (G_OBJECT_CLASS (gdu_grid_hbox_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_grid_hbox_parent_class)->finalize (object);
}

static guint
get_desired_width (GduGridHBox *hbox)
{
        guint width;
        GList *children;
        GList *l;

        width = 0;

        children = GTK_BOX (hbox)->children;
        if (children == NULL)
                goto out;

        for (l = children; l != NULL; l = l->next) {
                GtkBoxChild *child = l->data;
                GduGridElement *e;

                if (GTK_IS_VBOX (child->widget)) {
                        e = GDU_GRID_ELEMENT (((GtkBoxChild *) ((GTK_BOX (child->widget)->children)->data))->widget);
                } else {
                        e = GDU_GRID_ELEMENT (child->widget);
                }

                width += gdu_grid_element_get_minimum_size (e);
        }

 out:
        return width;
}

static void
gdu_grid_hbox_size_request (GtkWidget      *widget,
                            GtkRequisition *requisition)
{
        requisition->width = get_desired_width (GDU_GRID_HBOX (widget));
        requisition->height = 80;
}

static void
gdu_grid_hbox_size_allocate (GtkWidget      *widget,
                             GtkAllocation  *allocation)
{
        GList *children;
        GList *l;
        guint n;
        guint num_children;
        gint x;
        guint *children_sizes;
        guint used_size;
        guint extra_space;
        guint desired_width;

        children = GTK_BOX (widget)->children;
        if (children == NULL)
                goto out;

        num_children = g_list_length (children);

        children_sizes = g_new0 (guint, num_children);

        /* distribute size.. give at least minimum_width (since that is guaranteed to work) and
         * then assign extra space based on the percentage
         */
        desired_width = get_desired_width (GDU_GRID_HBOX (widget));
        if (desired_width < (guint) allocation->width)
                extra_space = allocation->width - desired_width;
        else
                extra_space = 0;

        used_size = 0;
        for (l = children, n = 0; l != NULL; l = l->next, n++) {
                GtkBoxChild *child = l->data;
                GduGridElement *e;
                guint width;
                guint e_minimum;
                gdouble e_percent;

                if (GTK_IS_VBOX (child->widget)) {
                        e = GDU_GRID_ELEMENT (((GtkBoxChild *) ((GTK_BOX (child->widget)->children)->data))->widget);
                } else {
                        e = GDU_GRID_ELEMENT (child->widget);
                }

                e_minimum = gdu_grid_element_get_minimum_size (e);
                e_percent = gdu_grid_element_get_percent_size (e);

                width = e_minimum + e_percent * extra_space;

                /* fix up last child so it's aligned with the right border */
                if (l->next == NULL) {
                        if (e_percent != 0.0) {
                                width = allocation->width - used_size;
                        }
                }

                children_sizes[n] = width;
                used_size += width;
        }

        x = 0;
        for (l = children, n = 0; l != NULL; l = l->next, n++) {
                GtkBoxChild *child = l->data;
                GtkAllocation child_allocation;

                child_allocation.x = allocation->x + x;
                child_allocation.y = allocation->y;
                child_allocation.width = children_sizes[n];
                child_allocation.height = allocation->height;
                x += children_sizes[n];

                gtk_widget_size_allocate (child->widget, &child_allocation);
        }

        g_free (children_sizes);

 out:
        ;
}

static void
gdu_grid_hbox_class_init (GduGridHBoxClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduGridHBoxPrivate));

        object_class->finalize     = gdu_grid_hbox_finalize;

        widget_class->size_request = gdu_grid_hbox_size_request;
        widget_class->size_allocate = gdu_grid_hbox_size_allocate;
}

static void
gdu_grid_hbox_init (GduGridHBox *box)
{
}

GtkWidget *
gdu_grid_hbox_new (void)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_GRID_HBOX,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */
