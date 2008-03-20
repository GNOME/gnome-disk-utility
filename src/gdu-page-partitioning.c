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
#include <stdlib.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>
#include <math.h>

#include "gdu-page.h"
#include "gdu-util.h"
#include "gdu-page-partitioning.h"

struct _GduPagePartitioningPrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;
        GtkWidget *drawing_area;

        GtkWidget *delete_part_vbox;

        GtkWidget *create_part_vbox;
        GtkWidget *create_part_size_hscale;
        GtkWidget *create_part_fstype_combo_box;
        GtkWidget *create_part_fslabel_entry;
        GtkWidget *create_part_secure_erase_combo_box;

        GtkWidget *modify_part_vbox;
        GtkWidget *modify_part_resize_size_hscale;
        GtkWidget *modify_part_label_entry;
        GtkWidget *modify_part_type_combo_box;
        GtkWidget *modify_part_flag_bootable_check_button;

        GduPresentable *presentable;

        PolKitAction *pk_delete_partition_action;
        PolKitGnomeAction *delete_partition_action;

        PolKitAction *pk_create_partition_action;
        PolKitGnomeAction *create_partition_action;

        PolKitAction *pk_modify_partition_action;
        PolKitGnomeAction *modify_partition_action;
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
        polkit_action_unref (page->priv->pk_delete_partition_action);
        g_object_unref (page->priv->delete_partition_action);

        polkit_action_unref (page->priv->pk_create_partition_action);
        g_object_unref (page->priv->create_partition_action);

        polkit_action_unref (page->priv->pk_modify_partition_action);
        g_object_unref (page->priv->modify_partition_action);

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

                if (node->presentable == page->priv->presentable)
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

/**
 * find_toplevel_presentable:
 * @presentable: a #GduPresentable.
 *
 * Finds the top-level presentable for a given presentable.
 *
 * Returns: The presentable; caller must unref when done with it
 **/
static GduPresentable *
find_toplevel_presentable (GduPresentable *presentable)
{
        GduPresentable *parent;
        GduPresentable *maybe_parent;

        parent = presentable;
        do {
                maybe_parent = gdu_presentable_get_enclosing_presentable (parent);
                if (maybe_parent != NULL) {
                        g_object_unref (maybe_parent);
                        parent = maybe_parent;
                }
        } while (maybe_parent != NULL);

        return g_object_ref (parent);
}

static gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (user_data);
        GduPresentable *parent;
        int width;
        int height;
        GList *nodes;
        gboolean not_to_scale;
        cairo_t *cr;

        width = widget->allocation.width;
        height = widget->allocation.height;

        /* first go up to the top-level parent */
        parent = find_toplevel_presentable (page->priv->presentable);

        cr = gdk_cairo_create (widget->window);
        cairo_rectangle (cr,
			 event->area.x, event->area.y,
			 event->area.width, event->area.height);
        cairo_clip (cr);


        /* generate nodes for each presentable */
        nodes = generate_nodes (page, parent, 4, height - 2*4);

        /* make sure each node is at least 40 pixels */
        not_to_scale = squeeze_nodes (nodes, 30);

        /* draw the nodes */
        draw_nodes (cr, page, nodes, 4, 4, width - 4*4);
        nodes_free (nodes);

        /* TODO: print disclaimer if we're not to scale */

        cairo_destroy (cr);

        g_object_unref (parent);
        return TRUE;
}

static void
delete_partition_callback (GtkAction *action, gpointer user_data)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (user_data);
        int response;
        GtkWidget *dialog;
        GduDevice *device;
        GduPresentable *toplevel_presentable;

        toplevel_presentable = find_toplevel_presentable (page->priv->presentable);

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));

        g_return_if_fail (device != NULL);

        /* TODO: mention what drive the partition is on etc. */
        dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (gdu_shell_get_toplevel (page->priv->shell)),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_MESSAGE_WARNING,
                                                     GTK_BUTTONS_CANCEL,
                                                     _("<b><big>Are you sure you want to delete the partition?</big></b>"));

        gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                                    _("All data on the partition will be irrecovably erased. "
                                                      "Make sure data important to you is backed up. "
                                                      "This action cannot be undone."));
        /* ... until we add data recovery to g-d-u! */

        gtk_dialog_add_button (GTK_DIALOG (dialog), _("Delete Partition"), 0);

        response = gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        if (response != 0)
                goto out;

        gdu_device_op_delete_partition (device);

        /* go to toplevel */
        gdu_shell_select_presentable (page->priv->shell, toplevel_presentable);

out:
        g_object_unref (toplevel_presentable);
        g_object_unref (device);
}

static void
create_partition_callback (GtkAction *action, gpointer user_data)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (user_data);
        GduPresentable *presentable;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        GduDevice *device;
        guint64 offset;
        guint64 size;
        char *type;
        char *label;
        char **flags;
        char *fstype;
        char *fslabel;
        char *fserase;
        const char *scheme;

        type = NULL;
        label = NULL;
        flags = NULL;
        device = NULL;
        fstype = NULL;
        fslabel = NULL;
        fserase = NULL;
        toplevel_presentable = NULL;
        toplevel_device = NULL;

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        g_assert (presentable != NULL);

        device = gdu_presentable_get_device (presentable);
        if (device != NULL) {
                g_warning ("%s: device is supposed to be NULL",  __FUNCTION__);
                goto out;
        }

        toplevel_presentable = find_toplevel_presentable (presentable);
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }
        if (!gdu_device_is_partition_table (toplevel_device)) {
                g_warning ("%s: device for toplevel presentable is not a partition table", __FUNCTION__);
                goto out;
        }

        offset = gdu_presentable_get_offset (presentable);
        size = (guint64) gtk_range_get_value (GTK_RANGE (page->priv->create_part_size_hscale));
        fstype = gdu_util_fstype_combo_box_get_selected (page->priv->create_part_fstype_combo_box);
        fslabel = g_strdup (gtk_entry_get_text (GTK_ENTRY (page->priv->create_part_fslabel_entry)));
        fserase = gdu_util_secure_erase_combo_box_get_selected (page->priv->create_part_secure_erase_combo_box);

        /* TODO: set flags */
        flags = NULL;

        /* default the partition type according to the kind of file system */
        scheme = gdu_device_partition_table_get_scheme (toplevel_device);

        /* see gdu_util.c:gdu_util_fstype_combo_box_create_store() */
        if (strcmp (fstype, "msdos_extended_partition") == 0) {
                type = g_strdup ("0x05");
                g_free (fstype);
                g_free (fslabel);
                g_free (fserase);
                fstype = g_strdup ("");
                fslabel = g_strdup ("");
                fserase = g_strdup ("");
        } else {
                type = gdu_util_get_default_part_type_for_scheme_and_fstype (scheme, fstype, size);
                if (type == NULL) {
                        g_warning ("Cannot determine default part type for scheme '%s' and fstype '%s'",
                                   scheme, fstype);
                        goto out;
                }
        }

        /* set partition label to the file system label (TODO: handle max len) */
        if (strcmp (scheme, "gpt") == 0 || strcmp (scheme, "apm") == 0) {
                label = g_strdup (label);
        } else {
                label = g_strdup ("");
        }

        gdu_device_op_create_partition (toplevel_device,
                                        offset,
                                        size,
                                        type,
                                        label,
                                        flags,
                                        fstype,
                                        fslabel,
                                        fserase);

        /* go to toplevel */
        gdu_shell_select_presentable (page->priv->shell, toplevel_presentable);

out:
        g_free (type);
        g_free (label);
        g_strfreev (flags);
        g_free (fstype);
        g_free (fslabel);
        g_free (fserase);
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
}

static void
modify_partition_callback (GtkAction *action, gpointer user_data)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device != NULL) {
                g_warning ("device is supposed to be NULL on create_partition_callback");
                g_object_unref (device);
                goto out;
        }

        g_warning ("TODO: modify partition");

out:
        ;
}


static const char * mbr_creatable_parttypes[] = {
        "0x83",
        "0x0c",
        "0x82",
        "0x05",
        "0xfd",
};
static int mbr_num_creatable_parttypes = sizeof (mbr_creatable_parttypes) / sizeof (const char *);


static GtkWidget *
parttype_combo_box_create (const char *part_scheme)
{
        int n;
        GtkListStore *store;
	GtkCellRenderer *renderer;
        GtkWidget *combo_box;
        const char **creatable_parttypes;
        int num_creatable_parttypes;

        /* TODO: part_scheme */
        creatable_parttypes = mbr_creatable_parttypes;
        num_creatable_parttypes = mbr_num_creatable_parttypes;

        store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
        for (n = 0; n < num_creatable_parttypes; n++) {
                const char *parttype;
                char *parttype_name;
                GtkTreeIter iter;

                parttype = creatable_parttypes[n];

                parttype_name = gdu_util_get_desc_for_part_type (part_scheme, parttype);

                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter,
                                    0, parttype,
                                    1, parttype_name,
                                    -1);

                g_free (parttype_name);
        }

        combo_box = gtk_combo_box_new ();
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
        g_object_unref (store);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
					"text", 1,
					NULL);

        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

        return combo_box;
}

static void
create_part_fstype_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (user_data);
        char *fstype;
        GduCreatableFilesystem *creatable_fs;
        gboolean label_entry_sensitive;
        int max_label_len;

        label_entry_sensitive = FALSE;
        max_label_len = 0;

        fstype = gdu_util_fstype_combo_box_get_selected (combo_box);
        if (fstype != NULL) {
                creatable_fs = gdu_util_find_creatable_filesystem_for_fstype (fstype);
                /* Note: there may not have a creatable file system... e.g. the user could
                 *       select "Extended" on mbr partition tables.
                 */
                if (creatable_fs != NULL) {
                        max_label_len = creatable_fs->max_label_len;
                }
        }

        if (max_label_len > 0)
                label_entry_sensitive = TRUE;

        gtk_entry_set_max_length (GTK_ENTRY (page->priv->create_part_fslabel_entry), max_label_len);
        gtk_widget_set_sensitive (page->priv->create_part_fslabel_entry, label_entry_sensitive);

        g_free (fstype);
}

static char*
create_part_size_format_value_callback (GtkScale *scale, gdouble value)
{
        return gdu_util_get_size_for_display ((guint64) value, FALSE);
}

static void
gdu_page_partitioning_init (GduPagePartitioning *page)
{
        GtkWidget *hbox;
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *vbox3;
        GtkWidget *button;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *entry;
        GtkWidget *combo_box;
        GtkWidget *check_button;
        GtkWidget *hscale;
        GtkWidget *button_box;
        int row;

        page->priv = g_new0 (GduPagePartitioningPrivate, 1);

        page->priv->pk_delete_partition_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_delete_partition_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->delete_partition_action = polkit_gnome_action_new_default (
                "delete-partition",
                page->priv->pk_delete_partition_action,
                _("_Delete"),
                _("Delete"));
        g_object_set (page->priv->delete_partition_action,
                      "auth-label", _("_Delete..."),
                      "yes-icon-name", GTK_STOCK_DELETE,
                      "no-icon-name", GTK_STOCK_DELETE,
                      "auth-icon-name", GTK_STOCK_DELETE,
                      "self-blocked-icon-name", GTK_STOCK_DELETE,
                      NULL);
        g_signal_connect (page->priv->delete_partition_action, "activate",
                          G_CALLBACK (delete_partition_callback), page);


        page->priv->pk_create_partition_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_create_partition_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->create_partition_action = polkit_gnome_action_new_default (
                "create-partition",
                page->priv->pk_create_partition_action,
                _("_Create"),
                _("Create"));
        g_object_set (page->priv->create_partition_action,
                      "auth-label", _("_Create..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (page->priv->create_partition_action, "activate",
                          G_CALLBACK (create_partition_callback), page);


        page->priv->pk_modify_partition_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_modify_partition_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->modify_partition_action = polkit_gnome_action_new_default (
                "modify-partition",
                page->priv->pk_modify_partition_action,
                _("_Apply"),
                _("Apply"));
        g_object_set (page->priv->modify_partition_action,
                      "auth-label", _("_Apply..."),
                      "yes-icon-name", GTK_STOCK_APPLY,
                      "no-icon-name", GTK_STOCK_APPLY,
                      "auth-icon-name", GTK_STOCK_APPLY,
                      "self-blocked-icon-name", GTK_STOCK_APPLY,
                      NULL);
        g_signal_connect (page->priv->modify_partition_action, "activate",
                          G_CALLBACK (modify_partition_callback), page);

        page->priv->main_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        hbox = gtk_hbox_new (FALSE, 10);

        page->priv->drawing_area = gtk_drawing_area_new ();
        gtk_widget_set_size_request (page->priv->drawing_area, 150, 150);
        g_signal_connect (G_OBJECT (page->priv->drawing_area), "expose-event",
                          G_CALLBACK (expose_event_callback), page);

        vbox = gtk_vbox_new (FALSE, 10);

        /* ---------------- */
        /* Delete partition */
        /* ---------------- */

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox3, FALSE, TRUE, 0);
        page->priv->delete_part_vbox = vbox3;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Delete Partition</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The space currently occupied by the partition will be "
                                                   "designated as unallocated space. This can be used for creating "
                                                   "other partitions."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        /* delete button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = polkit_gnome_action_create_button (page->priv->delete_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* ---------------- */
        /* Create partition */
        /* ---------------- */

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox3, FALSE, TRUE, 0);
        page->priv->create_part_vbox = vbox3;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Create Partition</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To create a new partition, select the size and whether to create "
                                                   "a file system. The partition type, label and flags can be changed "
                                                   "after creation."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* create size */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("S_ize:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        hscale = gtk_hscale_new_with_range (0, 100, 128 * 1024 * 1024);
        gtk_table_attach (GTK_TABLE (table), hscale, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        g_signal_connect (hscale, "format-value", (GCallback) create_part_size_format_value_callback, page);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), hscale);
        page->priv->create_part_size_hscale = hscale;

        row++;

        /* secure erase */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Secure Erase:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_secure_erase_combo_box_create ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->create_part_secure_erase_combo_box = combo_box;

        row++;

        /* _file system_ type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        combo_box = gdu_util_fstype_combo_box_create (NULL);
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->create_part_fstype_combo_box = combo_box;

        row++;

        /* _file system_ label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        page->priv->create_part_fslabel_entry = entry;

        /* create button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = polkit_gnome_action_create_button (page->priv->create_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* update sensivity and length of fs label and ensure it's set initially */
        g_signal_connect (page->priv->create_part_fstype_combo_box, "changed",
                          G_CALLBACK (create_part_fstype_combo_box_changed), page);
        create_part_fstype_combo_box_changed (page->priv->create_part_fstype_combo_box, page);

        /* ---------------- */
        /* Modify partition */
        /* ---------------- */

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox3, FALSE, TRUE, 0);
        page->priv->modify_part_vbox = vbox3;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Modify Partition</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("Change type, label, flags and size of the partition. Note "
                                                   "that the partition label is different from that of the file "
                                                   "system label."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* resize size */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("S_ize:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        hscale = gtk_hscale_new_with_range (0, 100, 128 * 1024 * 1024);
        gtk_table_attach (GTK_TABLE (table), hscale, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        g_signal_connect (hscale, "format-value", (GCallback) create_part_size_format_value_callback, page);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), hscale);
        page->priv->modify_part_resize_size_hscale = hscale;

        table = gtk_table_new (2, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        /* partition label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        page->priv->modify_part_label_entry = entry;

        row++;

        /* partition type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = parttype_combo_box_create ("mbr"); /* TODO: depends on the part scheme */
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->modify_part_type_combo_box = combo_box;

        /* flags! (TODO: depends on the part scheme) */
        check_button = gtk_check_button_new_with_mnemonic (_("_Bootable"));
        gtk_box_pack_start (GTK_BOX (vbox2), check_button, FALSE, TRUE, 0);
        page->priv->modify_part_flag_bootable_check_button = check_button;

        /* revert and apply buttons */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = gtk_button_new_with_mnemonic (_("_Revert"));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        button = polkit_gnome_action_create_button (page->priv->modify_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* TODO: connect signal for revert button */

        /* ---------------- */

        gtk_box_pack_start (GTK_BOX (hbox), page->priv->drawing_area, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), hbox, TRUE, TRUE, 0);

        /* don't show these by default; we'll turn them on/off depending on
         * the presentable
         */
        gtk_widget_set_no_show_all (page->priv->create_part_vbox, TRUE);
        gtk_widget_set_no_show_all (page->priv->delete_part_vbox, TRUE);
        gtk_widget_set_no_show_all (page->priv->modify_part_vbox, TRUE);
}


GduPagePartitioning *
gdu_page_partitioning_new (GduShell *shell)
{
        return GDU_PAGE_PARTITIONING (g_object_new (GDU_TYPE_PAGE_PARTITIONING, "shell", shell, NULL));
}

static gboolean
has_extended_partition (GduPagePartitioning *page, GduPresentable *presentable)
{
        GList *l;
        GList *enclosed_presentables;
        gboolean ret;

        ret = FALSE;

        enclosed_presentables = gdu_pool_get_enclosed_presentables (gdu_shell_get_pool (page->priv->shell),
                                                                    presentable);
        for (l = enclosed_presentables; l != NULL; l = l->next) {
                GduPresentable *p = l->data;
                GduDevice *d;

                d = gdu_presentable_get_device (p);
                if (d == NULL)
                        continue;

                if (gdu_device_is_partition (d)) {
                        int partition_type;
                        partition_type = strtol (gdu_device_partition_get_type (d), NULL, 0);
                        if (partition_type == 0x05 ||
                            partition_type == 0x0f ||
                            partition_type == 0x85) {
                                ret = TRUE;
                                break;
                        }
                }

        }
        return ret;
}

static gboolean
gdu_page_partitioning_update (GduPage *_page, GduPresentable *presentable)
{
        GduPagePartitioning *page = GDU_PAGE_PARTITIONING (_page);
        GduDevice *device;
        gboolean show_page;
        gboolean show_delete_part;
        gboolean show_create_part;
        gboolean show_modify_part;
        guint64 size;
        GduDevice *toplevel_device;
        GduPresentable *toplevel_presentable;
        const char *scheme;

        gtk_widget_set_no_show_all (page->priv->create_part_vbox, FALSE);
        gtk_widget_set_no_show_all (page->priv->delete_part_vbox, FALSE);
        gtk_widget_set_no_show_all (page->priv->modify_part_vbox, FALSE);

        show_page = FALSE;
        show_delete_part = FALSE;
        show_create_part = FALSE;
        show_modify_part = FALSE;

        toplevel_presentable = NULL;
        toplevel_device = NULL;

        device = gdu_presentable_get_device (presentable);

        if (device == NULL) {
                show_create_part = TRUE;
        } else {
                if (gdu_device_is_partition (device)) {
                    show_delete_part = TRUE;
                    show_modify_part = TRUE;
                }
        }

        if (!(show_create_part || show_delete_part || show_modify_part))
                goto out;

        toplevel_presentable = find_toplevel_presentable (presentable);
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_presentable == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }
        scheme = gdu_device_partition_table_get_scheme (toplevel_device);

        show_page = TRUE;

        if (page->priv->presentable != NULL)
                g_object_unref (page->priv->presentable);
        page->priv->presentable = g_object_ref (presentable);

        gtk_widget_queue_draw_area (page->priv->drawing_area,
                                    0, 0,
                                    page->priv->drawing_area->allocation.width,
                                    page->priv->drawing_area->allocation.height);

        size = gdu_presentable_get_size (presentable);

        if (show_create_part) {
                gtk_range_set_range (GTK_RANGE (page->priv->create_part_size_hscale), 0, size);
                gtk_range_set_value (GTK_RANGE (page->priv->create_part_size_hscale), size);
                gtk_widget_show_all (page->priv->create_part_vbox);

                /* only allow creation of extended partitions if there currently are none */
                if (has_extended_partition (page, toplevel_presentable)) {
                        gdu_util_fstype_combo_box_rebuild (page->priv->create_part_fstype_combo_box, NULL);
                } else {
                        gdu_util_fstype_combo_box_rebuild (page->priv->create_part_fstype_combo_box, scheme);
                }
        } else {
                gtk_widget_hide_all (page->priv->create_part_vbox);
        }

        if (show_modify_part) {
                gtk_range_set_range (GTK_RANGE (page->priv->modify_part_resize_size_hscale), 0, size);
                gtk_range_set_value (GTK_RANGE (page->priv->modify_part_resize_size_hscale), size);
                /* TODO: more */
                gtk_widget_show_all (page->priv->modify_part_vbox);
        } else {
                gtk_widget_hide_all (page->priv->modify_part_vbox);
        }

        if (show_delete_part) {
                gtk_widget_show_all (page->priv->delete_part_vbox);
        } else {
                gtk_widget_hide_all (page->priv->delete_part_vbox);
        }

out:
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);

        return show_page;
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
