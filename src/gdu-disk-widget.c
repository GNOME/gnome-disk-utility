/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-disk-widget.c
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

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>

#include "gdu-pool.h"
#include "gdu-util.h"
#include "gdu-disk-widget.h"

struct _GduDiskWidgetPrivate
{
        GduPresentable *presentable;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduDiskWidget, gdu_disk_widget, GTK_TYPE_DRAWING_AREA)

static gboolean expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer user_data);

enum {
        PROP_0,
        PROP_PRESENTABLE,
};

static void
gdu_disk_widget_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
        GduDiskWidget *disk_widget = GDU_DISK_WIDGET (object);
        gpointer obj;

        switch (prop_id) {
        case PROP_PRESENTABLE:
                if (disk_widget->priv->presentable != NULL)
                        g_object_unref (disk_widget->priv->presentable);
                obj = g_value_get_object (value);
                disk_widget->priv->presentable = (obj == NULL ? NULL : g_object_ref (obj));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_disk_widget_get_property (GObject     *object,
                                    guint        prop_id,
                                    GValue      *value,
                                    GParamSpec  *pspec)
{
        GduDiskWidget *disk_widget = GDU_DISK_WIDGET (object);

        switch (prop_id) {
        case PROP_PRESENTABLE:
                g_value_set_object (value, disk_widget->priv->presentable);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_disk_widget_finalize (GduDiskWidget *disk_widget)
{
        if (disk_widget->priv->presentable != NULL)
                g_object_unref (disk_widget->priv->presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (disk_widget));
}

static void
gdu_disk_widget_class_init (GduDiskWidgetClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_disk_widget_finalize;
        obj_class->set_property = gdu_disk_widget_set_property;
        obj_class->get_property = gdu_disk_widget_get_property;

        /**
         * GduDiskWidget:presentable:
         *
         * The #GduPresentable instance we are displaying. May be
         * #NULL.
         */
        g_object_class_install_property (obj_class,
                                         PROP_PRESENTABLE,
                                         g_param_spec_object ("presentable",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_PRESENTABLE,
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_READABLE));
}

static void
gdu_disk_widget_init (GduDiskWidget *disk_widget)
{
        disk_widget->priv = g_new0 (GduDiskWidgetPrivate, 1);

        g_signal_connect (G_OBJECT (disk_widget), "expose-event",
                          G_CALLBACK (expose_event_callback), disk_widget);
}

GtkWidget *
gdu_disk_widget_new (GduPresentable *presentable)
{
        GduDiskWidget *disk_widget;

        disk_widget = GDU_DISK_WIDGET (g_object_new (GDU_TYPE_DISK_WIDGET,
                                                     "presentable", presentable,
                                                     NULL));

        return GTK_WIDGET (disk_widget);
}

void
gdu_disk_widget_set_presentable (GduDiskWidget *disk_widget, GduPresentable *presentable)
{
        g_object_set (disk_widget, "presentable", presentable, NULL);
}

GduPresentable *
gdu_disk_widget_get_presentable (GduDiskWidget *disk_widget)
{
        GduPresentable *ret;
        g_object_get (disk_widget, "presentable", &ret, NULL);
        return ret;
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
                         * TODO: we could steal evenly from wealthy nodes
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
generate_nodes (GduDiskWidget *disk_widget,
                GduPresentable *parent,
                double at_pos,
                double at_size)
{
        GList *enclosed;
        GList *l;
        guint64 part_offset;
        guint64 part_size;
        GList *nodes;
        GduPool *pool;

        nodes = NULL;

        pool = gdu_presentable_get_pool (parent);

        enclosed = gdu_pool_get_enclosed_presentables (pool, parent);
        if (g_list_length (enclosed) == 0)
                goto out;

        enclosed = g_list_sort (enclosed, gdu_presentable_compare_offset_func);

        part_offset = gdu_presentable_get_offset (parent);
        part_size = gdu_presentable_get_size (parent);

        for (l = enclosed; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GduDevice *d;
                guint64 offset;
                guint64 size;
                GList *embedded_nodes;
                double g_pos;
                double g_size;

                d = gdu_presentable_get_device (p);
                if (d != NULL) {
                        if (!gdu_device_is_partition (d)) {
                                g_object_unref (d);
                                continue;
                        }
                }

                offset = gdu_presentable_get_offset (p);
                size = gdu_presentable_get_size (p);

                g_pos = at_pos + at_size * (offset - part_offset) / part_size;
                g_size = at_size * size / part_size;

                /* This presentable may have enclosed presentables itself.. use them instead
                 * if they are partitions or holes.
                 */
                embedded_nodes = generate_nodes (disk_widget, p, g_pos, g_size);
                if (embedded_nodes != NULL) {
                        nodes = g_list_concat (nodes, embedded_nodes);
                } else {
                        Node *node;
                        node = g_new0 (Node, 1);
                        node->size = g_size;
                        node->presentable = g_object_ref (p);
                        nodes = g_list_append (nodes, node);
                }

                if (d != NULL)
                        g_object_unref (d);
        }

out:
        g_list_foreach (enclosed, (GFunc) g_object_unref, NULL);
        g_list_free (enclosed);
        g_object_unref (pool);
        return nodes;
}

static void
draw_nodes (cairo_t *cr,
            GduDiskWidget *disk_widget,
            GList *nodes,
            double at_xpos,
            double at_ypos,
            double at_size)
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
                cairo_text_extents_t ext_s1;
                cairo_text_extents_t ext_s2;
                double ty;
                char *s1;
                char *s2;
                GduDevice *device;

                is_first = (l == nodes);
                is_last = (l->next == NULL);

                w = at_size;
                h = node->size;

                rounded_rectangle (cr, x, y, w, h, 4,
                                   is_first, is_first, is_last, is_last);

                if (node->presentable == disk_widget->priv->presentable)
                        cairo_set_source_rgb (cr, 0.40, 0.40, 0.80);
                else
                        cairo_set_source_rgb (cr, 0.30, 0.30, 0.30);

                cairo_set_line_width (cr, 1.5);
                cairo_fill_preserve (cr);
                cairo_set_source_rgb (cr, 0, 0, 0);
                cairo_stroke (cr);

                cairo_save (cr);

		cairo_rectangle (cr, x, y, w, h);
		cairo_clip (cr);

		cairo_set_source_rgb (cr, 1, 1, 1);

                device = gdu_presentable_get_device (node->presentable);
                if (device == NULL) {
                        /* empty space; e.g. hole */
                        s1 = g_strdup (_("Unallocated Space"));
                        s2 = gdu_util_get_size_for_display (gdu_presentable_get_size (node->presentable), FALSE);
                } else {
                        char *t1;
                        char *t2;
                        s1 = gdu_presentable_get_name (node->presentable);
                        t1 = gdu_util_get_size_for_display (gdu_presentable_get_size (node->presentable), FALSE);
                        t2 = gdu_util_get_fstype_for_display (gdu_device_id_get_type (device),
                                                              gdu_device_id_get_version (device),
                                                              FALSE);
                        s2 = g_strdup_printf ("%s %s", t1, t2);
                        g_free (t1);
                        g_free (t2);
                        g_object_unref (device);
                }

		cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size (cr, 10);
		cairo_text_extents (cr, s1, &ext_s1);
		cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_text_extents (cr, s2, &ext_s2);

                ty = y + h / 2.0 - (ext_s1.height + 4.0 + ext_s2.height) / 2.0 + ext_s1.height;

		cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_move_to (cr, x + w/2 - ext_s1.width / 2, ty);
		cairo_show_text (cr, s1);

                ty += ext_s1.height + 4;

		cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_move_to (cr, x + w/2 - ext_s2.width / 2, ty);
		cairo_show_text (cr, s2);

                g_free (s1);
                g_free (s2);

                cairo_restore (cr);

                y += h;
        }
}

static gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
        GduDiskWidget *disk_widget = GDU_DISK_WIDGET (user_data);
        GduPresentable *parent;
        int width;
        int height;
        GList *nodes;
        gboolean not_to_scale;
        cairo_t *cr;

        width = widget->allocation.width;
        height = widget->allocation.height;

        /* first go up to the top-level parent */
        parent = gdu_util_find_toplevel_presentable (disk_widget->priv->presentable);

        cr = gdk_cairo_create (widget->window);
        cairo_rectangle (cr,
			 event->area.x, event->area.y,
			 event->area.width, event->area.height);
        cairo_clip (cr);


        /* generate nodes for each presentable */
        nodes = generate_nodes (disk_widget, parent, 4, height - 2*4);

        /* make sure each node is at least 40 pixels */
        not_to_scale = squeeze_nodes (nodes, 30);

        /* draw the nodes */
        draw_nodes (cr, disk_widget, nodes, 4, 4, width - 4*4);
        nodes_free (nodes);

        /* TODO: print disclaimer if we're not to scale */

        cairo_destroy (cr);

        g_object_unref (parent);
        return TRUE;
}
