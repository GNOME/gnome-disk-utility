/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#define _GNU_SOURCE

#include "config.h"

#include <glib/gi18n-lib.h>
#include <math.h>

#include "gdu-create-linux-md-dialog.h"
#include "gdu-size-widget.h"

struct GduCreateLinuxMdDialogPrivate
{
        GduPool *pool;
        GduPoolTreeModel *model;

        GtkWidget *level_combo_box;
        GtkWidget *level_desc_label;

        GtkWidget *name_entry;

        GtkWidget *stripe_size_label;
        GtkWidget *stripe_size_combo_box;

        GtkWidget *tree_view;

        GtkWidget *size_label;
        GtkWidget *size_widget;

        GtkWidget *tip_container;
        GtkWidget *tip_image;
        GtkWidget *tip_label;

        /* represents user selected options */
        gchar *level;
        guint num_disks_needed;
        guint stripe_size;

        /* A list of GduDrive objects to create the components on */
        GList *selected_drives;
};

enum
{
        PROP_0,
        PROP_POOL,
        PROP_LEVEL,
        PROP_NAME,
        PROP_SIZE,
        PROP_COMPONENT_SIZE,
        PROP_STRIPE_SIZE,
        PROP_DRIVES,
};

static void gdu_create_linux_md_dialog_constructed (GObject *object);

static void update (GduCreateLinuxMdDialog *dialog);

static void on_presentable_added   (GduPool          *pool,
                                    GduPresentable   *presentable,
                                    gpointer          user_data);
static void on_presentable_removed (GduPool          *pool,
                                    GduPresentable   *presentable,
                                    gpointer          user_data);
static void on_presentable_changed (GduPool          *pool,
                                    GduPresentable   *presentable,
                                    gpointer          user_data);

static void on_row_changed (GtkTreeModel *tree_model,
                            GtkTreePath  *path,
                            GtkTreeIter  *iter,
                            gpointer      user_data);

static void on_row_deleted (GtkTreeModel *tree_model,
                            GtkTreePath  *path,
                            gpointer      user_data);

static void on_row_inserted (GtkTreeModel *tree_model,
                             GtkTreePath  *path,
                             GtkTreeIter  *iter,
                             gpointer      user_data);

static void get_sizes (GduCreateLinuxMdDialog *dialog,
                       guint                  *out_num_disks,
                       guint                  *out_num_available_disks,
                       guint64                *out_component_size,
                       guint64                *out_array_size,
                       guint64                *out_max_component_size,
                       guint64                *out_max_array_size);

G_DEFINE_TYPE (GduCreateLinuxMdDialog, gdu_create_linux_md_dialog, GTK_TYPE_DIALOG)

static void
gdu_create_linux_md_dialog_finalize (GObject *object)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (object);

        g_signal_handlers_disconnect_by_func (dialog->priv->pool, on_presentable_added, dialog);
        g_signal_handlers_disconnect_by_func (dialog->priv->pool, on_presentable_removed, dialog);
        g_signal_handlers_disconnect_by_func (dialog->priv->pool, on_presentable_changed, dialog);
        g_signal_handlers_disconnect_by_func (dialog->priv->model, on_row_changed, dialog);
        g_signal_handlers_disconnect_by_func (dialog->priv->model, on_row_deleted, dialog);
        g_signal_handlers_disconnect_by_func (dialog->priv->model, on_row_inserted, dialog);

        g_object_unref (dialog->priv->pool);
        g_object_unref (dialog->priv->model);
        g_free (dialog->priv->level);

        g_list_foreach (dialog->priv->selected_drives, (GFunc) g_object_unref, NULL);
        g_list_free (dialog->priv->selected_drives);

        if (G_OBJECT_CLASS (gdu_create_linux_md_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_create_linux_md_dialog_parent_class)->finalize (object);
}

static void
gdu_create_linux_md_dialog_get_property (GObject    *object,
                                         guint       property_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (object);
        GPtrArray *p;

        switch (property_id) {
        case PROP_POOL:
                g_value_set_object (value, dialog->priv->pool);
                break;

        case PROP_LEVEL:
                g_value_set_string (value, dialog->priv->level);
                break;

        case PROP_NAME:
                g_value_set_string (value, gdu_create_linux_md_dialog_get_name (dialog));
                break;

        case PROP_SIZE:
                g_value_set_uint64 (value, gdu_create_linux_md_dialog_get_size (dialog));
                break;

        case PROP_COMPONENT_SIZE:
                g_value_set_uint64 (value, gdu_create_linux_md_dialog_get_component_size (dialog));
                break;

        case PROP_STRIPE_SIZE:
                g_value_set_uint64 (value, gdu_create_linux_md_dialog_get_stripe_size (dialog));
                break;

        case PROP_DRIVES:
                p = gdu_create_linux_md_dialog_get_drives (dialog);
                g_value_set_boxed (value, p);
                g_ptr_array_unref (p);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
gdu_create_linux_md_dialog_set_property (GObject      *object,
                                         guint         property_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (object);

        switch (property_id) {
        case PROP_POOL:
                dialog->priv->pool = g_value_dup_object (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
gdu_create_linux_md_dialog_class_init (GduCreateLinuxMdDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduCreateLinuxMdDialogPrivate));

        object_class->get_property = gdu_create_linux_md_dialog_get_property;
        object_class->set_property = gdu_create_linux_md_dialog_set_property;
        object_class->constructed  = gdu_create_linux_md_dialog_constructed;
        object_class->finalize     = gdu_create_linux_md_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_POOL,
                                         g_param_spec_object ("pool",
                                                              _("Pool"),
                                                              _("The pool of devices"),
                                                              GDU_TYPE_POOL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_LEVEL,
                                         g_param_spec_string ("level",
                                                              _("RAID Level"),
                                                              _("The selected RAID level"),
                                                              NULL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_NAME,
                                         g_param_spec_string ("name",
                                                              _("Name"),
                                                              _("The requested name for the array"),
                                                              NULL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_SIZE,
                                         g_param_spec_uint64 ("size",
                                                              _("Size"),
                                                              _("The requested size of the array"),
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_COMPONENT_SIZE,
                                         g_param_spec_uint64 ("component-size",
                                                              _("Component Size"),
                                                              _("The size of each component"),
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_STRIPE_SIZE,
                                         g_param_spec_uint64 ("stripe-size",
                                                              _("Stripe Size"),
                                                              _("The requested stripe size of the array"),
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_DRIVES,
                                         g_param_spec_boxed ("drives",
                                                             _("Drives"),
                                                             _("Array of drives to use for the array"),
                                                             G_TYPE_PTR_ARRAY,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_STATIC_NAME |
                                                             G_PARAM_STATIC_NICK |
                                                             G_PARAM_STATIC_BLURB));
}

static void
gdu_create_linux_md_dialog_init (GduCreateLinuxMdDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog, GDU_TYPE_CREATE_LINUX_MD_DIALOG, GduCreateLinuxMdDialogPrivate);
}

GtkWidget *
gdu_create_linux_md_dialog_new (GtkWindow *parent,
                                GduPool   *pool)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_CREATE_LINUX_MD_DIALOG,
                                         "transient-for", parent,
                                         "pool", pool,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
gdu_create_linux_md_dialog_get_level  (GduCreateLinuxMdDialog *dialog)
{
        g_return_val_if_fail (GDU_IS_CREATE_LINUX_MD_DIALOG (dialog), NULL);
        return g_strdup (dialog->priv->level);
}

gchar *
gdu_create_linux_md_dialog_get_name (GduCreateLinuxMdDialog *dialog)
{
        g_return_val_if_fail (GDU_IS_CREATE_LINUX_MD_DIALOG (dialog), NULL);
        return g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->priv->name_entry)));
}

guint64
gdu_create_linux_md_dialog_get_size (GduCreateLinuxMdDialog  *dialog)
{
        guint64 array_size;

        g_return_val_if_fail (GDU_IS_CREATE_LINUX_MD_DIALOG (dialog), 0);

        get_sizes (dialog,
                   NULL,  /* num_disks */
                   NULL,  /* num_available_disks */
                   NULL,  /* component_size */
                   &array_size,
                   NULL,  /* max_component_size */
                   NULL); /* max_array_size */

        return array_size;
}

guint64
gdu_create_linux_md_dialog_get_component_size (GduCreateLinuxMdDialog  *dialog)
{
        guint64 component_size;

        g_return_val_if_fail (GDU_IS_CREATE_LINUX_MD_DIALOG (dialog), 0);

        get_sizes (dialog,
                   NULL,  /* num_disks */
                   NULL,  /* num_available_disks */
                   &component_size,
                   NULL,  /* array_size */
                   NULL,  /* max_component_size */
                   NULL); /* max_array_size */

        return component_size;
}

guint64
gdu_create_linux_md_dialog_get_stripe_size (GduCreateLinuxMdDialog  *dialog)
{
        g_return_val_if_fail (GDU_IS_CREATE_LINUX_MD_DIALOG (dialog), 0);

        if (g_strcmp0 (dialog->priv->level, "raid1") == 0)
                return 0;
        else
                return dialog->priv->stripe_size;
}

GPtrArray *
gdu_create_linux_md_dialog_get_drives (GduCreateLinuxMdDialog  *dialog)
{
        GPtrArray *p;
        GList *l;

        g_return_val_if_fail (GDU_IS_CREATE_LINUX_MD_DIALOG (dialog), NULL);

        p = g_ptr_array_new_with_free_func (g_object_unref);
        for (l = dialog->priv->selected_drives; l != NULL; l = l->next) {
                GduPresentable *drive = GDU_PRESENTABLE (l->data);
                g_ptr_array_add (p, g_object_ref (drive));
        }
        return p;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_level_combo_box_changed (GtkWidget *combo_box,
                            gpointer   user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        gchar *s;
        gchar *markup;

        /* keep in sync with where combo box is constructed in constructed() */
        g_free (dialog->priv->level);
        switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box))) {
        case 0:
                dialog->priv->level = g_strdup ("raid0");
                dialog->priv->num_disks_needed = 2;
                break;
        case 1:
                dialog->priv->level = g_strdup ("raid1");
                dialog->priv->num_disks_needed = 2;
                break;
        case 2:
                dialog->priv->level = g_strdup ("raid5");
                dialog->priv->num_disks_needed = 3;
                break;
        case 3:
                dialog->priv->level = g_strdup ("raid6");
                dialog->priv->num_disks_needed = 4;
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        s = gdu_linux_md_get_raid_level_description (dialog->priv->level);
        markup = g_strconcat ("<small><i>",
                              s,
                              "</i></small>",
                              NULL);
        g_free (s);
        gtk_label_set_markup (GTK_LABEL (dialog->priv->level_desc_label), markup);
        g_free (markup);

        update (dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_stripe_size_combo_box_changed (GtkWidget *combo_box,
                                  gpointer   user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);

        /* keep in sync with where combo box is constructed in constructed() */
        switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box))) {
        case 0:
                dialog->priv->stripe_size = 4 * 1024;
                break;
        case 1:
                dialog->priv->stripe_size = 8 * 1024;
                break;
        case 2:
                dialog->priv->stripe_size = 16 * 1024;
                break;
        case 3:
                dialog->priv->stripe_size = 32 * 1024;
                break;
        case 4:
                dialog->priv->stripe_size = 64 * 1024;
                break;
        case 5:
                dialog->priv->stripe_size = 128 * 1024;
                break;
        case 6:
                dialog->priv->stripe_size = 256 * 1024;
                break;
        case 7:
                dialog->priv->stripe_size = 512 * 1024;
                break;
        case 8:
                dialog->priv->stripe_size = 1024 * 1024;
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        update (dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_name_entry_activated (GtkWidget *combo_box,
                         gpointer   user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);

        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
drive_is_selected (GduCreateLinuxMdDialog *dialog,
                   GduPresentable         *drive)
{
        return g_list_find (dialog->priv->selected_drives, drive) != NULL;
}

static void
drive_remove (GduCreateLinuxMdDialog *dialog,
              GduPresentable         *drive)
{
        GList *l;

        l = g_list_find (dialog->priv->selected_drives, drive);
        if (l != NULL) {
                g_object_unref (l->data);
                dialog->priv->selected_drives = g_list_delete_link (dialog->priv->selected_drives,
                                                                    l);
        }
}

static void
drive_add (GduCreateLinuxMdDialog *dialog,
           GduPresentable         *drive)
{
        g_return_if_fail (!drive_is_selected (dialog, drive));

        dialog->priv->selected_drives = g_list_prepend (dialog->priv->selected_drives,
                                                        g_object_ref (drive));
}


static void
drive_toggle (GduCreateLinuxMdDialog *dialog,
              GduPresentable         *drive)
{
        if (drive_is_selected (dialog, drive)) {
                drive_remove (dialog, drive);
        } else {
                drive_add (dialog, drive);
        }
}


static void
toggle_data_func (GtkCellLayout   *cell_layout,
                  GtkCellRenderer *renderer,
                  GtkTreeModel    *tree_model,
                  GtkTreeIter     *iter,
                  gpointer         user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        GduPresentable *p;
        gboolean is_toggled;

        gtk_tree_model_get (tree_model,
                            iter,
                            GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &p,
                            -1);

        is_toggled = drive_is_selected (dialog, p);

        g_object_set (renderer,
                      "active", is_toggled,
                      NULL);

        g_object_unref (p);
}

static void
on_disk_toggled (GtkCellRendererToggle *renderer,
                 const gchar           *path_string,
                 gpointer               user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        GtkTreeIter iter;
        GtkTreePath *path;
        GduPresentable *p;

        if (!gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (dialog->priv->model),
                                                  &iter,
                                                  path_string))
                goto out;

        gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->model),
                            &iter,
                            GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &p,
                            -1);

        drive_toggle (dialog, p);

        path = gtk_tree_model_get_path (GTK_TREE_MODEL (dialog->priv->model),
                                        &iter);
        gtk_tree_model_row_changed (GTK_TREE_MODEL (dialog->priv->model),
                                    path,
                                    &iter);
        gtk_tree_path_free (path);

        g_object_unref (p);

        update (dialog);

 out:
        ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
disk_name_data_func (GtkCellLayout   *cell_layout,
                     GtkCellRenderer *renderer,
                     GtkTreeModel    *tree_model,
                     GtkTreeIter     *iter,
                     gpointer         user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        GtkTreeSelection *tree_selection;
        gchar *name;
        gchar *vpd_name;
        gchar *desc;
        gchar *markup;
        GtkStyle *style;
        GdkColor desc_gdk_color = {0};
        gchar *desc_color;
        GtkStateType state;

        tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->tree_view));

        gtk_tree_model_get (tree_model,
                            iter,
                            GDU_POOL_TREE_MODEL_COLUMN_NAME, &name,
                            GDU_POOL_TREE_MODEL_COLUMN_VPD_NAME, &vpd_name,
                            GDU_POOL_TREE_MODEL_COLUMN_DESCRIPTION, &desc,
                            -1);

        /* This color business shouldn't be this hard... */
        style = gtk_widget_get_style (GTK_WIDGET (dialog->priv->tree_view));
        if (gtk_tree_selection_iter_is_selected (tree_selection, iter)) {
                if (GTK_WIDGET_HAS_FOCUS (GTK_WIDGET (dialog->priv->tree_view)))
                        state = GTK_STATE_SELECTED;
                else
                        state = GTK_STATE_ACTIVE;
        } else {
                state = GTK_STATE_NORMAL;
        }
#define BLEND_FACTOR 0.7
        desc_gdk_color.red   = style->text[state].red   * BLEND_FACTOR +
                               style->base[state].red   * (1.0 - BLEND_FACTOR);
        desc_gdk_color.green = style->text[state].green * BLEND_FACTOR +
                               style->base[state].green * (1.0 - BLEND_FACTOR);
        desc_gdk_color.blue  = style->text[state].blue  * BLEND_FACTOR +
                               style->base[state].blue  * (1.0 - BLEND_FACTOR);
#undef BLEND_FACTOR
        desc_color = g_strdup_printf ("#%02x%02x%02x",
                                      (desc_gdk_color.red >> 8),
                                      (desc_gdk_color.green >> 8),
                                      (desc_gdk_color.blue >> 8));

        markup = g_strdup_printf ("<b>%s</b>\n"
                                  "<span fgcolor=\"%s\"><small>%s\n%s</small></span>",
                                  name,
                                  desc_color,
                                  vpd_name,
                                  desc);

        g_object_set (renderer,
                      "markup", markup,
                      NULL);

        g_free (name);
        g_free (vpd_name);
        g_free (desc);
        g_free (markup);
        g_free (desc_color);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
notes_data_func (GtkCellLayout   *cell_layout,
                 GtkCellRenderer *renderer,
                 GtkTreeModel    *tree_model,
                 GtkTreeIter     *iter,
                 gpointer         user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        GduPresentable *p;
        gchar *markup;
        gchar *s;
        gint width;

        gtk_tree_model_get (tree_model,
                            iter,
                            GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &p,
                            -1);

        if (GDU_IS_DRIVE (p)) {
                GduDevice *d;
                guint64 largest_segment;
                gboolean whole_disk_is_uninitialized;
                guint num_partitions;
                gboolean is_partitioned;

                d = gdu_presentable_get_device (p);

                num_partitions = 0;
                is_partitioned = FALSE;
                if (gdu_device_is_partition_table (d)) {
                        is_partitioned = TRUE;
                        num_partitions = gdu_device_partition_table_get_count (d);
                }

                g_warn_if_fail (gdu_drive_has_unallocated_space (GDU_DRIVE (p),
                                                                 &whole_disk_is_uninitialized,
                                                                 &largest_segment,
                                                                 NULL));


                if (drive_is_selected (dialog, p)) {
                        guint num_disks;
                        guint num_available_disks;
                        guint64 component_size;
                        guint64 array_size;

                        get_sizes (dialog,
                                   &num_disks,
                                   &num_available_disks,
                                   &component_size,
                                   &array_size,
                                   NULL,  /* max_component_size */
                                   NULL); /* max_array_size */


                        if (array_size > 1000 * 1000) {
                                gchar *strsize;
                                strsize = gdu_util_get_size_for_display (component_size, FALSE);

                                if (whole_disk_is_uninitialized) {
                                        /* Translators: This is shown in the Details column.
                                         * %s is the component size e.g. '42 GB'.
                                         */
                                        markup = g_strdup_printf (_("The disk will be partitioned and a %s partition "
                                                                    "will be created"),
                                                                  strsize);
                                } else {
                                        /* Translators: This is shown in the Details column.
                                         * %s is the component size e.g. '42 GB'.
                                         */
                                        markup = g_strdup_printf (_("A %s partition will be created"),
                                                                  strsize);
                                }
                                g_free (strsize);
                        } else {
                                if (whole_disk_is_uninitialized) {
                                        /* Translators: This is shown in the Details column. */
                                        markup = g_strdup (_("The disk will be partitioned and a partition "
                                                             "will be created"));
                                } else {
                                        /* Translators: This is shown in the Details column. */
                                        markup = g_strdup (_("A partition will be created"));
                                }
                        }

                } else {
                        gchar *strsize;

                        strsize = gdu_util_get_size_for_display (largest_segment, FALSE);

                        if (whole_disk_is_uninitialized) {
                                /* Translators: This is shown in the Details column.
                                 * %s is the component size e.g. '42 GB'.
                                 */
                                markup = g_strdup_printf (_("Whole disk is uninitialized. %s available for use"),
                                                          strsize);
                        } else {
                                if (!is_partitioned) {
                                        /* Translators: This is shown in the Details column.
                                         * %s is the component size e.g. '42 GB'.
                                         */
                                        markup = g_strdup_printf (_("%s available for use"), strsize);
                                } else {
                                        if (num_partitions == 0) {
                                                /* Translators: This is shown in the Details column.
                                                 * %s is the component size e.g. '42 GB'.
                                                 */
                                                markup = g_strdup_printf (_("The disk has no partitions. "
                                                                            "%s available for use"),
                                                                          strsize);
                                        } else {
                                                s = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                                                                "The disk has %d partition",
                                                                                "The disk has %d partitions",
                                                                                num_partitions),
                                                                     num_partitions);
                                                /* Translators: This is shown in the Details column.
                                                 * First %s is the dngettext() result of "The disk has %d partitions.".
                                                 * Second %s is the component size e.g. '42 GB'.
                                                 */
                                                markup = g_strdup_printf (_("%s. Largest contiguous free block is %s"),
                                                                          s,
                                                                          strsize);
                                                g_free (s);
                                        }
                                }
                        }

                        g_free (strsize);
                }

                g_object_unref (d);
        } else {
                markup = g_strdup ("");
        }


        width = gtk_tree_view_column_get_fixed_width (GTK_TREE_VIEW_COLUMN (cell_layout));
        g_warn_if_fail (width > 12);
        width -= 12;

        s = g_strconcat ("<small>",
                         markup,
                         "</small>",
                         NULL);
        g_object_set (renderer,
                      "markup", s,
                      "wrap-width", width,
                      NULL);
        g_free (s);

        g_free (markup);
        g_object_unref (p);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_size_widget_changed (GduSizeWidget *size_widget,
                        gpointer       user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        GList *l;

        update (dialog);

        /* need to trigger row-changed for the selected disks since
         * component size is listed in the "Details" column
         */
        for (l = dialog->priv->selected_drives; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GtkTreePath *path;
                GtkTreeIter iter;

                gdu_pool_tree_model_get_iter_for_presentable (dialog->priv->model,
                                                              p,
                                                              &iter);

                path = gtk_tree_model_get_path (GTK_TREE_MODEL (dialog->priv->model),
                                                &iter);
                gtk_tree_model_row_changed (GTK_TREE_MODEL (dialog->priv->model),
                                            path,
                                            &iter);
                gtk_tree_path_free (path);
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_create_linux_md_dialog_constructed (GObject *object)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *button;
        GtkWidget *label;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *table;
        GtkWidget *entry;
        GtkWidget *combo_box;
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *size_widget;
        gint row;
        gboolean ret;
        GtkWidget *align;
        gchar *s;

        ret = FALSE;

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
        gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 0);
        gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
        gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);

        gtk_window_set_title (GTK_WINDOW (dialog), _("Create RAID Array"));
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "gdu-raid-array");

        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                        _("C_reate"),
                                        GTK_RESPONSE_OK);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
        gtk_container_set_border_width (GTK_CONTAINER (content_area), 10);

        vbox = content_area;
        gtk_box_set_spacing (GTK_BOX (vbox), 6);

        /* -------------------------------------------------------------------------------- */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        s = g_strconcat ("<b>", _("General"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 12);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 6, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);

        row = 0;

        table = gtk_table_new (2, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_table_set_row_spacings (GTK_TABLE (table), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        /*  RAID level  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("RAID _Level:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gtk_combo_box_new_text ();
        dialog->priv->level_combo_box = combo_box;
        /* keep in sync with on_level_combo_box_changed() */
        s = gdu_linux_md_get_raid_level_for_display ("raid0", TRUE);
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), s);
        g_free (s);
        s = gdu_linux_md_get_raid_level_for_display ("raid1", TRUE);
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), s);
        g_free (s);
        s = gdu_linux_md_get_raid_level_for_display ("raid5", TRUE);
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), s);
        g_free (s);
        s = gdu_linux_md_get_raid_level_for_display ("raid6", TRUE);
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), s);
        g_free (s);
        g_signal_connect (combo_box,
                          "changed",
                          G_CALLBACK (on_level_combo_box_changed),
                          dialog);

        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        row++;

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row +1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        dialog->priv->level_desc_label = label;
        row++;

        /*  Array name  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Array _Name:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        /* Translators: This is the default name to use for the new array.
         * Keep length of UTF-8 representation of the translation of "New RAID Array" to less than
         * 32 bytes since that's the on-disk-format limit.
         */
        gtk_entry_set_text (GTK_ENTRY (entry), _("New RAID Array"));
        gtk_entry_set_max_length (GTK_ENTRY (entry), 32); /* on-disk-format restriction */
        g_signal_connect (entry,
                          "activate",
                          G_CALLBACK (on_name_entry_activated),
                          dialog);

        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        dialog->priv->name_entry = entry;
        row++;

        /*  Stripe Size  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Stripe S_ize:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        /* keep in sync with on_stripe_size_combo_box_changed() */
        combo_box = gtk_combo_box_new_text ();
        /* Translators: The following strings (4 KiB, 8 Kib, ..., 1 MiB) are for choosing the RAID stripe size.
         * Since the rest of gnome-disk-utility use the sane 1k=1000 conventions and RAID needs the 1k=1024
         * convenention (this is because disk block sizes are powers of two) we resort to the nerdy units.
         */
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("4 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("8 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("16 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("32 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("64 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("128 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("256 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("512 KiB"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("1 MiB"));
        dialog->priv->stripe_size = 64 * 1024;
        g_signal_connect (combo_box,
                          "changed",
                          G_CALLBACK (on_stripe_size_combo_box_changed),
                          dialog);

        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        dialog->priv->stripe_size_label = label;
        dialog->priv->stripe_size_combo_box = combo_box;
        row++;

        /*  Array size  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Array _Size:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        size_widget = gdu_size_widget_new (0,
                                           0,
                                           0);
        g_signal_connect (size_widget,
                          "changed",
                          G_CALLBACK (on_size_widget_changed),
                          dialog);
        gtk_table_attach (GTK_TABLE (table), size_widget, 1, 2, row, row + 1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        dialog->priv->size_label = label;
        dialog->priv->size_widget = size_widget;
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), size_widget);
        row++;

        /* -------------------------------------------------------------------------------- */

        /* Tree view for showing selected volumes */
        GtkWidget *tree_view;
        GtkWidget *scrolled_window;
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;

        dialog->priv->model = gdu_pool_tree_model_new (dialog->priv->pool,
                                                       GDU_POOL_TREE_MODEL_FLAGS_NO_VOLUMES |
                                                       GDU_POOL_TREE_MODEL_FLAGS_NO_UNALLOCATABLE_DRIVES);

        tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (dialog->priv->model));
        gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (tree_view), TRUE);
        dialog->priv->tree_view = tree_view;

        column = gtk_tree_view_column_new ();
        /* Tranlators: this string is used for the column header */
        gtk_tree_view_column_set_title (column, _("Use"));
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        renderer = gtk_cell_renderer_toggle_new ();
        gtk_tree_view_column_pack_start (column,
                                         renderer,
                                         FALSE);
        g_signal_connect (renderer,
                          "toggled",
                          G_CALLBACK (on_disk_toggled),
                          dialog);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            toggle_data_func,
                                            dialog,
                                            NULL);


        column = gtk_tree_view_column_new ();
        /* Tranlators: this string is used for the column header */
        gtk_tree_view_column_set_title (column, _("Disk"));
        gtk_tree_view_column_set_expand (column, TRUE);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        renderer = gtk_cell_renderer_pixbuf_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_column_set_attributes (column,
                                             renderer,
                                             "gicon", GDU_POOL_TREE_MODEL_COLUMN_ICON,
                                             NULL);
        g_object_set (renderer,
                      "stock-size", GTK_ICON_SIZE_DIALOG,
                      NULL);

        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            disk_name_data_func,
                                            dialog,
                                            NULL);

        column = gtk_tree_view_column_new ();
        /* Tranlators: this string is used for the column header */
        gtk_tree_view_column_set_title (column, _("Details"));
        gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_min_width (column, 170);
        gtk_tree_view_column_set_max_width (column, 170);
        gtk_tree_view_column_set_fixed_width (column, 170);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_end (column, renderer, FALSE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            notes_data_func,
                                            dialog,
                                            NULL);
        g_object_set (renderer,
                      "xalign", 0.0,
                      "yalign", 0.0,
                      "wrap-mode", PANGO_WRAP_WORD_CHAR,
                      NULL);

        gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (tree_view), FALSE);
        gtk_tree_view_set_level_indentation (GTK_TREE_VIEW (tree_view), 16);
        gtk_tree_view_expand_all (GTK_TREE_VIEW (tree_view));


        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                             GTK_SHADOW_IN);

        gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        s = g_strconcat ("<b>", _("Disks"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 12);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 6, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (vbox), align, TRUE, TRUE, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);

        gtk_box_pack_start (GTK_BOX (vbox2), scrolled_window, TRUE, TRUE, 0);

        /* -------------------------------------------------------------------------------- */



        /* -------------------------------------------------------------------------------- */

        gtk_widget_grab_focus (dialog->priv->name_entry);
        gtk_editable_select_region (GTK_EDITABLE (dialog->priv->name_entry), 0, 1000);


        /* -------------------------------------------------------------------------------- */

        g_signal_connect (dialog->priv->pool,
                          "presentable-added",
                          G_CALLBACK (on_presentable_added),
                          dialog);
        g_signal_connect (dialog->priv->pool,
                          "presentable-removed",
                          G_CALLBACK (on_presentable_removed),
                          dialog);
        g_signal_connect (dialog->priv->pool,
                          "presentable-changed",
                          G_CALLBACK (on_presentable_changed),
                          dialog);
        g_signal_connect (dialog->priv->model,
                          "row-changed",
                          G_CALLBACK (on_row_changed),
                          dialog);
        g_signal_connect (dialog->priv->model,
                          "row-deleted",
                          G_CALLBACK (on_row_deleted),
                          dialog);
        g_signal_connect (dialog->priv->model,
                          "row-inserted",
                          G_CALLBACK (on_row_inserted),
                          dialog);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);

        image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_MENU);
        gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

        dialog->priv->tip_container = hbox;
        dialog->priv->tip_image = image;
        dialog->priv->tip_label = label;
        gtk_widget_set_no_show_all (hbox, TRUE);

        /* Calls on_level_combo_box_changed() which calls update() */
        gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->level_combo_box), 0);
        gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->stripe_size_combo_box), 4);

        /* select a sane size for the dialog and allow resizing */
        gtk_widget_set_size_request (GTK_WIDGET (dialog), 500, 550);
        gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

        if (G_OBJECT_CLASS (gdu_create_linux_md_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_create_linux_md_dialog_parent_class)->constructed (object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_presentable_added (GduPool          *pool,
                      GduPresentable   *presentable,
                      gpointer          user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        update (dialog);
}

static void
on_presentable_removed (GduPool          *pool,
                        GduPresentable   *presentable,
                        gpointer          user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);

        if (drive_is_selected (dialog, presentable))
                drive_remove (dialog, presentable);

        update (dialog);
}

static void
on_presentable_changed (GduPool          *pool,
                        GduPresentable   *presentable,
                        gpointer          user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        update (dialog);
}

static void
on_row_changed (GtkTreeModel *tree_model,
                GtkTreePath  *path,
                GtkTreeIter  *iter,
                gpointer      user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        update (dialog);
}

static void
on_row_deleted (GtkTreeModel *tree_model,
                GtkTreePath  *path,
                gpointer      user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        update (dialog);
}

static void
on_row_inserted (GtkTreeModel *tree_model,
                 GtkTreePath  *path,
                 GtkTreeIter  *iter,
                 gpointer      user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        update (dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
count_num_available_disks_func (GtkTreeModel *model,
                                GtkTreePath  *path,
                                GtkTreeIter  *iter,
                                gpointer      data)
{
        GduPresentable *p;
        guint *num_available_disks = data;

        gtk_tree_model_get (model,
                            iter,
                            GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &p,
                            -1);

        if (GDU_IS_DRIVE (p))
                *num_available_disks = *num_available_disks + 1;

        g_object_unref (p);

        return FALSE;
}

static void
get_sizes (GduCreateLinuxMdDialog *dialog,
           guint                  *out_num_disks,
           guint                  *out_num_available_disks,
           guint64                *out_component_size,
           guint64                *out_array_size,
           guint64                *out_max_component_size,
           guint64                *out_max_array_size)
{
        guint num_disks;
        guint num_available_disks;
        guint64 component_size;
        guint64 array_size;
        guint64 max_component_size;
        guint64 max_array_size;
        gdouble factor;
        GList *l;

        num_disks = 0;
        num_available_disks = 0;
        max_component_size = G_MAXUINT64;

        for (l = dialog->priv->selected_drives; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                guint64 largest_segment;
                gboolean whole_disk_is_uninitialized;

                g_warn_if_fail (gdu_drive_has_unallocated_space (GDU_DRIVE (p),
                                                                 &whole_disk_is_uninitialized,
                                                                 &largest_segment,
                                                                 NULL));

                if (largest_segment < max_component_size)
                        max_component_size = largest_segment;

                num_disks++;
        }
        if (max_component_size == G_MAXUINT64)
                max_component_size = 0;

        factor = 0.0;
        if (num_disks > 1) {
                if (g_strcmp0 (dialog->priv->level, "raid0") == 0) {
                        factor = num_disks;
                } else if (g_strcmp0 (dialog->priv->level, "raid1") == 0) {
                        factor = 1.0;
                } else if (g_strcmp0 (dialog->priv->level, "raid5") == 0) {
                        if (num_disks > 1)
                                factor = num_disks - 1;
                } else if (g_strcmp0 (dialog->priv->level, "raid6") == 0) {
                        if (num_disks > 2)
                                factor = num_disks - 2;
                } else {
                        g_assert_not_reached ();
                }
        }
        max_array_size = max_component_size * factor;
        array_size = gdu_size_widget_get_size (GDU_SIZE_WIDGET (dialog->priv->size_widget));
        if (max_array_size > 0)
                component_size = max_component_size * ( ((gdouble) array_size) / ((gdouble) max_array_size) );
        else
                component_size = 0;

        gtk_tree_model_foreach (GTK_TREE_MODEL (dialog->priv->model),
                                count_num_available_disks_func,
                                &num_available_disks);

        if (out_num_disks != NULL)
                *out_num_disks = num_disks;

        if (out_num_available_disks != NULL)
                *out_num_available_disks = num_available_disks;

        if (out_component_size != NULL)
                *out_component_size = component_size;

        if (out_array_size != NULL)
                *out_array_size = array_size;

        if (out_max_component_size != NULL)
                *out_max_component_size = max_component_size;

        if (out_max_array_size != NULL)
                *out_max_array_size = max_array_size;
}

static void
update (GduCreateLinuxMdDialog *dialog)
{
        gchar *tip_text;
        const gchar *tip_stock_icon;
        guint num_disks;
        guint num_available_disks;
        guint64 max_component_size;
        guint64 max_array_size;
        gboolean can_create;
        gchar *level_str;
        gboolean size_widget_was_sensitive;
        guint64 array_size;

        tip_text = NULL;
        tip_stock_icon = NULL;
        can_create = FALSE;

        get_sizes (dialog,
                   &num_disks,
                   &num_available_disks,
                   NULL,  /* component_size */
                   &array_size,
                   &max_component_size,
                   &max_array_size);

        level_str = gdu_linux_md_get_raid_level_for_display (dialog->priv->level, FALSE);

        size_widget_was_sensitive = gtk_widget_get_sensitive (dialog->priv->size_widget);

        if (num_available_disks < dialog->priv->num_disks_needed) {
                gtk_widget_set_sensitive (dialog->priv->size_label, FALSE);
                gtk_widget_set_sensitive (dialog->priv->size_widget, FALSE);
                gdu_size_widget_set_max_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                              2000 * 1000);
                gdu_size_widget_set_min_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                              1000 * 1000);
                gdu_size_widget_set_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                          1000 * 1000);

                if (tip_text == NULL) {
                        /* Translators: This is for the tip text shown in the dialog.
                         * First %s is the short localized name for the RAID level, e.g. "RAID-1".
                         */
                        tip_text = g_strdup_printf (_("Insufficient number disks to create a %s array."),
                                                    level_str);
                        tip_stock_icon = GTK_STOCK_DIALOG_ERROR;
                }

        } else if (num_disks < dialog->priv->num_disks_needed) {

                gtk_widget_set_sensitive (dialog->priv->size_label, FALSE);
                gtk_widget_set_sensitive (dialog->priv->size_widget, FALSE);
                gdu_size_widget_set_max_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                              2000 * 1000);
                gdu_size_widget_set_min_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                              1000 * 1000);
                gdu_size_widget_set_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                          1000 * 1000);

                if (tip_text == NULL) {
                        if (num_disks == 0) {
                                tip_text = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                                                       "To create a %s array, select %d disks.",
                                                                       "To create a %s array, select %d disks.",
                                                                       dialog->priv->num_disks_needed - num_disks),
                                                            level_str,
                                                            dialog->priv->num_disks_needed - num_disks);
                        } else {
                                tip_text = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                                                       "To create a %s array, select %d more disks.",
                                                                       "To create a %s array, select %d more disks.",
                                                                       dialog->priv->num_disks_needed - num_disks),
                                                            level_str,
                                                            dialog->priv->num_disks_needed - num_disks);
                        }
                        tip_stock_icon = GTK_STOCK_DIALOG_INFO;
                }

        } else {

                gtk_widget_set_sensitive (dialog->priv->size_label, TRUE);
                gtk_widget_set_sensitive (dialog->priv->size_widget, TRUE);
                gdu_size_widget_set_max_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                              max_array_size);
                gdu_size_widget_set_min_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                              1000 * 1000);
                if (!size_widget_was_sensitive)
                        gdu_size_widget_set_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                                  max_array_size);

                if (tip_text == NULL) {
                        gchar *strsize;

                        strsize = gdu_util_get_size_for_display (array_size, FALSE);
                        /* Translators: This is for the tip text shown in the dialog.
                         * First %s is the size e.g. '42 GB'.
                         * Second %s is the short localized name for the RAID level, e.g. "RAID-1".
                         * Third parameter is the number of disks to use (always greater than 1).
                         */
                        tip_text = g_strdup_printf (_("To create a %s %s array on %d disks, press \"Create\""),
                                                    strsize,
                                                    level_str,
                                                    num_disks);
                        tip_stock_icon = GTK_STOCK_DIALOG_INFO;

                        can_create = TRUE;

                        g_free (strsize);
                }
        }

        if (g_strcmp0 (dialog->priv->level, "raid0") == 0 ||
            g_strcmp0 (dialog->priv->level, "raid5") == 0 ||
            g_strcmp0 (dialog->priv->level, "raid6") == 0) {
                gtk_widget_set_sensitive (dialog->priv->stripe_size_label, TRUE);
                gtk_widget_set_sensitive (dialog->priv->stripe_size_combo_box, TRUE);
        } else {
                gtk_widget_set_sensitive (dialog->priv->stripe_size_label, FALSE);
                gtk_widget_set_sensitive (dialog->priv->stripe_size_combo_box, FALSE);
        }


        if (tip_text != NULL) {
                gchar *s;
                s = g_strconcat ("<i>",
                                 tip_text,
                                 "</i>",
                                 NULL);
                gtk_label_set_markup (GTK_LABEL (dialog->priv->tip_label), s);
                g_free (s);
                gtk_image_set_from_stock (GTK_IMAGE (dialog->priv->tip_image),
                                          tip_stock_icon,
                                          GTK_ICON_SIZE_MENU);
                gtk_widget_show (dialog->priv->tip_container);
                gtk_widget_show (dialog->priv->tip_image);
                gtk_widget_show (dialog->priv->tip_label);
        } else {
                gtk_widget_hide (dialog->priv->tip_container);
                gtk_widget_hide (dialog->priv->tip_image);
                gtk_widget_hide (dialog->priv->tip_label);
        }

        gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           can_create);

        g_free (level_str);
}
