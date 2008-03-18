/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-partitioning.c
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <glib-object.h>
#include <string.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>
#include <math.h>

#include "gdu-page.h"
#include "gdu-page-partitioning.h"
#include "gdu-util.h"

struct _GduPagePartitioningPrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;
        GtkWidget *drawing_area;

        GduPresentable *presentable;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_partitioning_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPagePartitioning, gdu_page_partitioning, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_partitioning_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void
gdu_page_partitioning_finalize (GduPagePartitioning *page)
{
        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);
        if (page->priv->presentable != NULL)
                g_object_unref (page->priv->presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_partitioning_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (object);

        switch (prop_id) {
        case PROP_SHELL:
                if (page->priv->shell != NULL)
                        g_object_unref (page->priv->shell);
                page->priv->shell = g_object_ref (g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_page_partitioning_get_property (GObject     *object,
                                    guint        prop_id,
                                    GValue      *value,
                                    GParamSpec  *pspec)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (object);

        switch (prop_id) {
        case PROP_SHELL:
                g_value_set_object (value, page->priv->shell);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_page_partitioning_class_init (GduPagePartitioningClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_partitioning_finalize;
        obj_class->set_property = gdu_page_partitioning_set_property;
        obj_class->get_property = gdu_page_partitioning_get_property;

        /**
         * GduPagePartitioning:shell:
         *
         * The #GduShell instance hosting this page.
         */
        g_object_class_install_property (obj_class,
                                         PROP_SHELL,
                                         g_param_spec_object ("shell",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_SHELL,
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_READABLE));
}

static void
rounded_rectangle (cairo_t *cr,
		   double x, double y,
		   double w, double h,
		   double c,
		   gboolean round_top_left, gboolean round_top_right,
		   gboolean round_bottom_left, gboolean round_bottom_right)
{
	double r0, r1, r2, r3;

	/* r0              r1
	 *   ______________
	 *  /              \
	 *  |              |
	 *  |              |
	 *  |              |
	 *  \______________/
         * r3              r2
	 */

	if (w < 0 || h < 0)
		return;

	if (round_top_left) {
		r0 = c;
	} else {
		r0 = 0;
	}

	if (round_bottom_left) {
		r3 = c;
	} else {
		r3 = 0;
	}

	if (round_top_right) {
		r1 = c;
	} else {
		r1 = 0;
	}

	if (round_bottom_right) {
		r2 = c;
	} else {
		r2 = 0;
	}


	cairo_translate (cr, x, y);

	cairo_move_to (cr, r0, 0);
	cairo_line_to (cr, w - r1, 0);
	if (r1 > 0) {
		cairo_arc (cr,
			   w - r1,
			   r1,
			   r1,
			   -M_PI / 2,
			   0);
	}
	cairo_line_to (cr, w, h - r2);
	if (r2 > 0) {
		cairo_arc (cr,
			   w - r2,
			   h - r2,
			   r2,
			   0,
			   M_PI / 2);
	}
	cairo_line_to (cr, r3, h);
	if (r3 > 0) {
		cairo_arc (cr,
			   c,
			   h - r3,
			   r3,
			   M_PI / 2,
			   M_PI);
	}
	cairo_line_to (cr, 0, r0);
	if (r0 > 0) {
		cairo_arc (cr,
			   r0,
			   r0,
			   r0,
			   M_PI,
			   3 * M_PI / 2);
	}
	cairo_translate (cr, -x, -y);
}

static gint
gdu_presentable_compare_offset_func (gconstpointer a, gconstpointer b)
{
        GduPresentable *pa = GDU_PRESENTABLE (a);
        GduPresentable *pb = GDU_PRESENTABLE (b);
        guint64 oa;
        guint64 ob;

        oa = gdu_presentable_get_offset (pa);
        ob = gdu_presentable_get_offset (pb);

        if (oa > ob)
                return 1;
        else if (oa < ob)
                return -1;
        else
                return 0;
}

typedef struct
{
        double size;
        GList *children;
        GduPresentable *presentable;
} Node;

static void nodes_free (GList *nodes);

static void
node_free (Node *node)
{
        nodes_free (node->children);
        g_object_unref (node->presentable);
        g_free (node);
}

static void
nodes_free (GList *nodes)
{
        GList *l;
        for (l = nodes; l != NULL; l = l->next) {
                Node *node = l->data;
                node_free (node);
        }
}

static gboolean
squeeze_nodes (GList *nodes, double min_size)
{
        GList *l;
        GList *j;
        gboolean did_squeze;

        did_squeze = FALSE;

        for (l = nodes; l != NULL; l = l->next) {
                Node *node = l->data;

                if (node->size < min_size) {
                        Node *victim;
                        double amount_to_steal;

                        /* this node is too small; steal from the biggest
                         * node that can afford it
                         *
                         * TODO: we could steal evenly from wealth nodes
                         */
                        amount_to_steal = min_size - node->size;

                        victim = NULL;
                        for (j = nodes; j != NULL; j = j->next) {
                                Node *candidate = j->data;

                                if (candidate == node)
                                        continue;

                                if (candidate->size - amount_to_steal < min_size)
                                        continue;

                                if (victim == NULL) {
                                        victim = candidate;
                                        continue;
                                }

                                if (candidate->size > victim->size)
                                        victim = candidate;
                        }

                        if (victim != NULL) {
                                victim->size -= amount_to_steal;
                                node->size += amount_to_steal;
                                did_squeze = TRUE;
                        }
                }
        }

        return did_squeze;
}

static GList *
generate_nodes (GduPagePartitioning *page,
                GduPresentable *parent,
                double at_pos,
                double at_size)
{
        GList *enclosed;
        GList *l;
        guint64 part_offset;
        guint64 part_size;
        GList *nodes;

        nodes = NULL;

        enclosed = gdu_pool_get_enclosed_presentables (gdu_shell_get_pool (page->priv->shell), parent);
        if (g_list_length (enclosed) == 0)
                goto out;

        enclosed = g_list_sort (enclosed, gdu_presentable_compare_offset_func);

        part_offset = gdu_presentable_get_offset (parent);
        part_size = gdu_presentable_get_size (parent);

        for (l = enclosed; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                guint64 offset;
                guint64 size;
                GList *embedded_nodes;
                double g_pos;
                double g_size;

                offset = gdu_presentable_get_offset (p);
                size = gdu_presentable_get_size (p);

                g_pos = at_pos + at_size * (offset - part_offset) / part_size;
                g_size = at_size * size / part_size;

                /* this presentable may have enclosed presentables itself.. use them instead */
                embedded_nodes = generate_nodes (page, p, g_pos, g_size);
                if (embedded_nodes != NULL) {
                        nodes = g_list_concat (nodes, embedded_nodes);
                } else {
                        Node *node;
                        node = g_new0 (Node, 1);
                        node->size = g_size;
                        node->presentable = g_object_ref (p);
                        nodes = g_list_append (nodes, node);
                }
        }

out:
        g_list_foreach (enclosed, (GFunc) g_object_unref, NULL);
        g_list_free (enclosed);
        return nodes;
}

static void
draw_nodes (cairo_t *cr,
            GduPagePartitioning *page,
            GList *nodes,
            double at_xpos,
            double at_ypos,
            double at_size,
            gboolean draw_vertical)
{
        GList *l;
        double x;
        double y;
        double w;
        double h;

        x = at_xpos;
        y = at_ypos;

        for (l = nodes; l != NULL; l = l->next) {
                Node *node = l->data;
                gboolean is_first;
                gboolean is_last;

                is_first = (l == nodes);
                is_last = (l->next == NULL);

                if (draw_vertical) {
                        w = at_size;
                        h = node->size;
                } else {
                        w = node->size;
                        h = at_size;
                }

                if (draw_vertical) {
                        rounded_rectangle (cr, x, y, w, h, 8,
                                           is_first, is_first, is_last, is_last);
                } else {
                        rounded_rectangle (cr, x, y, w, h, 8,
                                           is_first, is_last, is_first, is_last);
                }

                if (node->presentable == page->priv->presentable)
                        cairo_set_source_rgb (cr, 0.40, 0.40, 0.80);
                else
                        cairo_set_source_rgb (cr, 0.30, 0.30, 0.30);

                cairo_set_line_width (cr, 1.5);
                cairo_fill_preserve (cr);
                cairo_set_source_rgb (cr, 0, 0, 0);
                cairo_stroke (cr);

                if (draw_vertical) {
                        y += h;
                } else {
                        x += w;
                }
        }
}

static gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (user_data);
        GduPresentable *parent;
        GduPresentable *maybe_parent;
        int width;
        int height;
        GList *nodes;
        gboolean not_to_scale;
        cairo_t *cr;

        width = widget->allocation.width;
        height = widget->allocation.height,

        /* first go up to the top-level parent */
        parent = page->priv->presentable;
        do {
                maybe_parent = gdu_presentable_get_enclosing_presentable (parent);
                if (maybe_parent != NULL) {
                        g_object_unref (maybe_parent);
                        parent = maybe_parent;
                }
        } while (maybe_parent != NULL);

        cr = gdk_cairo_create (widget->window);
        cairo_rectangle (cr,
			 event->area.x, event->area.y,
			 event->area.width, event->area.height);
        cairo_clip (cr);


        /* generate nodes for each presentable */
        //nodes = generate_nodes (page, parent, 4, width - 2*4);
        nodes = generate_nodes (page, parent, 4, height - 2*4);

        /* make sure each node is at least 40 pixels */
        //not_to_scale = squeeze_nodes (nodes, 40);
        not_to_scale = squeeze_nodes (nodes, 30);

        /* draw the nodes */
        //draw_nodes (cr, page, nodes, 4, 4, height - 4*4, FALSE);
        draw_nodes (cr, page, nodes, 4, 4, width - 4*4, TRUE);
        nodes_free (nodes);

        /* TODO: print disclaimer if we're not to scale */

        cairo_destroy (cr);
        return TRUE;
}

static void
gdu_page_partitioning_init (GduPagePartitioning *page)
{
        page->priv = g_new0 (GduPagePartitioningPrivate, 1);

        page->priv->main_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        GtkWidget *hbox;
        hbox = gtk_hbox_new (FALSE, 10);

        page->priv->drawing_area = gtk_drawing_area_new ();
        gtk_widget_set_size_request (page->priv->drawing_area, 180, 150);
        g_signal_connect (G_OBJECT (page->priv->drawing_area), "expose-event",
                          G_CALLBACK (expose_event_callback), page);

        gtk_box_pack_start (GTK_BOX (hbox), page->priv->drawing_area, FALSE, FALSE, 0);

        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), hbox, TRUE, TRUE, 0);
}


GduPagePartitioning *
gdu_page_partitioning_new (GduShell *shell)
{
        return GDU_PAGE_PARTITIONING (g_object_new (GDU_TYPE_PAGE_PARTITIONING, "shell", shell, NULL));
}

static gboolean
gdu_page_partitioning_update (GduPage *_page, GduPresentable *presentable)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (_page);

        if (page->priv->presentable != NULL)
                g_object_unref (page->priv->presentable);
        page->priv->presentable = g_object_ref (presentable);

        gtk_widget_queue_draw_area (page->priv->drawing_area,
                                    0, 0,
                                    page->priv->drawing_area->allocation.width,
                                    page->priv->drawing_area->allocation.height);
        return TRUE;
}

static GtkWidget *
gdu_page_partitioning_get_widget (GduPage *_page)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (_page);
        return page->priv->main_vbox;
}

static char *
gdu_page_partitioning_get_name (GduPage *page)
{
        return g_strdup (_("Partitioning"));
}

static void
gdu_page_partitioning_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_partitioning_get_widget;
        iface->get_name = gdu_page_partitioning_get_name;
        iface->update = gdu_page_partitioning_update;
}
