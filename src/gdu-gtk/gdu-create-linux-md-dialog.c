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

        /* represents user selected options */
        gchar *level;
        guint num_disks;
        guint64 component_size;
        guint64 total_size;

        /* Number of disks with room for components */
        guint available_num_disks;

        /* The maximum possible size of the array - this is a function of available_num_disks
         * and num_disks and how each disk is laid out. Is 0 if an array for the given
         * configuration cannot be created.
         */
        guint64 available_total_size;

        GtkWidget *name_entry;
        GtkWidget *num_disks_spin_button;
        GtkWidget *size_widget;
        GtkWidget *tree_view;

        GtkWidget *tip_container;
        GtkWidget *tip_image;
        GtkWidget *tip_label;
};

enum
{
        PROP_0,
        PROP_POOL,
        PROP_LEVEL,
        PROP_NAME,
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
                                         g_param_spec_string ("fs-label",
                                                              _("Filesystem label"),
                                                              _("The requested filesystem label"),
                                                              NULL,
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

/* ---------------------------------------------------------------------------------------------------- */

static void
on_combo_box_changed (GtkWidget *combo_box,
                      gpointer   user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);
        GduKnownFilesystem *kfs;
        gint max_label_len;

        /* keep in sync with where combo box is constructed in constructed() */
        g_free (dialog->priv->level);
        switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box))) {
        case 0:
                dialog->priv->level = g_strdup ("linear");
                break;
        case 1:
                dialog->priv->level = g_strdup ("raid0");
                break;
        case 2:
                dialog->priv->level = g_strdup ("raid1");
                break;
        case 3:
                dialog->priv->level = g_strdup ("raid4");
                break;
        case 4:
                dialog->priv->level = g_strdup ("raid5");
                break;
        case 5:
                dialog->priv->level = g_strdup ("raid6");
                break;
        case 6:
                dialog->priv->level = g_strdup ("raid10");
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        max_label_len = 0;
        kfs = gdu_pool_get_known_filesystem_by_id (dialog->priv->pool, dialog->priv->level);
        if (kfs != NULL) {
                max_label_len = gdu_known_filesystem_get_max_label_len (kfs);
                g_object_unref (kfs);
        }
}

static void
on_name_entry_activated (GtkWidget *combo_box,
                         gpointer   user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);

        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_num_disks_spin_button_value_changed (GtkSpinButton *spin_button,
                                        gpointer       user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);

        dialog->priv->num_disks = (gint) gtk_spin_button_get_value (spin_button);

        update (dialog);
}

static void
on_size_widget_changed (GduSizeWidget *widget,
                        gpointer       user_data)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (user_data);

        dialog->priv->total_size = (guint64) gdu_size_widget_get_size (widget);

        update (dialog);
}

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
        GtkWidget *spin_button;
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
                                        _("Cr_eate"),
                                        GTK_RESPONSE_OK);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
        gtk_container_set_border_width (GTK_CONTAINER (content_area), 10);

        //vbox = gtk_vbox_new (FALSE, 0);
        //gtk_container_add (GTK_CONTAINER (content_area), vbox);
        vbox = content_area;

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
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Level:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gtk_combo_box_new_text ();
        /* keep in sync with on_combo_box_changed() */
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("Concatenated"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("Striped (RAID 0)"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("Mirrored (RAID 1)"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("RAID 4"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("RAID 5"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("RAID 6"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("RAID 10"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 2);
        dialog->priv->level = g_strdup ("raid1");
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        row++;

        /*  array name  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Name:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        /* Translators: Keep length of translation of "New RAID Device" to less than 32 characters */
        gtk_entry_set_text (GTK_ENTRY (entry), _("New RAID Device"));
        gtk_entry_set_max_length (GTK_ENTRY (entry), 32); /* on-disk-format restriction */
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        dialog->priv->name_entry = entry;
        row++;

        /* Number of components */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Number of _Disks:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);


        spin_button = gtk_spin_button_new_with_range (2, 10000, 1);
        dialog->priv->num_disks_spin_button = spin_button;
        g_signal_connect (spin_button,
                          "value-changed",
                          G_CALLBACK (on_num_disks_spin_button_value_changed),
                          dialog);

        align = gtk_alignment_new (0.0, 0.5, 0.0, 1.0);
        gtk_container_add (GTK_CONTAINER (align), spin_button);

        gtk_table_attach (GTK_TABLE (table),
                          align,
                          1, 2,
                          row, row + 1,
                          GTK_EXPAND | GTK_FILL,
                          GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), spin_button);
        row++;

        /* Size */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Array _Size:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        size_widget = gdu_size_widget_new (1000 * 1000,
                                           1000 * 1000,
                                           10 * 1000 * 1000);
        dialog->priv->size_widget = size_widget;
        g_signal_connect (size_widget,
                          "changed",
                          G_CALLBACK (on_size_widget_changed),
                          dialog);

        gtk_table_attach (GTK_TABLE (table), size_widget, 1, 2, row, row + 1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), size_widget);
        row++;

        /* -------------------------------------------------------------------------------- */

        /* Tree view for showing selected volumes */
        GtkWidget *tree_view;
        GtkWidget *scrolled_window;

        dialog->priv->model = gdu_pool_tree_model_new (dialog->priv->pool);

        tree_view = gdu_pool_tree_view_new (dialog->priv->model,
                                            GDU_POOL_TREE_VIEW_FLAGS_SHOW_TOGGLE);
        dialog->priv->tree_view = tree_view;

        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                             GTK_SHADOW_IN);

        gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        s = g_strconcat ("<b>", _("Components"), "</b>", NULL);
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

        g_signal_connect (combo_box,
                          "changed",
                          G_CALLBACK (on_combo_box_changed),
                          dialog);

        g_signal_connect (dialog->priv->name_entry,
                          "activate",
                          G_CALLBACK (on_name_entry_activated),
                          dialog);

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

        /* update the dialog */
        update (dialog);

        /* and start out with selecting max size */
        gdu_size_widget_set_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                  dialog->priv->available_total_size);

        /* select a sane size for the dialog and allow resizing */
        gtk_widget_set_size_request (GTK_WIDGET (dialog), 400, 450);
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

static gint
guint64_compare (gconstpointer a,
                 gconstpointer b)
{
        guint64 va, vb;
        va = *((guint64 *) a);
        vb = *((guint64 *) b);
        if (va > vb)
                return 1;
        else if (va < vb)
                return -1;
        else
                return 0;
}

static gboolean
get_selected_foreach_func (GtkTreeModel *model,
                           GtkTreePath  *path,
                           GtkTreeIter  *iter,
                           gpointer      user_data)
{
        GList **ret = user_data;
        gboolean toggled;
        GduPresentable *presentable;

        gtk_tree_model_get (model,
                            iter,
                            GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &presentable,
                            GDU_POOL_TREE_MODEL_COLUMN_TOGGLED, &toggled,
                            -1);
        if (presentable != NULL) {
                if (toggled)
                        *ret = g_list_prepend (*ret, g_object_ref (presentable));
                g_object_unref (presentable);
        }

        return FALSE; /* keep iterating */
}


static GList *
get_selected_presentables (GduCreateLinuxMdDialog *dialog)
{
        GList *ret;
        ret = NULL;
        gtk_tree_model_foreach (GTK_TREE_MODEL (dialog->priv->model),
                                get_selected_foreach_func,
                                &ret);
        return ret;
}

static void
update (GduCreateLinuxMdDialog *dialog)
{
        GList *l;
        GList *presentables;
        GHashTable *map_disks_to_biggest_component_size;
        GList *selected_presentables;
        GList *drives_for_selected_presentables;
        gboolean can_create;
        guint num_toggled;
        gchar *tip_text;
        const gchar *tip_stock_icon;

        tip_text = NULL;
        tip_stock_icon = NULL;

        can_create = FALSE;

        if (dialog->priv->num_disks < 2)
                dialog->priv->num_disks = 2;

        presentables = gdu_pool_get_presentables (dialog->priv->pool);
        selected_presentables = get_selected_presentables (dialog);
        drives_for_selected_presentables = NULL;
        for (l = selected_presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GduPresentable *drive;

                drive = gdu_presentable_get_toplevel (p);
                if (drive == NULL)
                        continue;

                g_object_unref (drive); /* don't own the ref */
                if (!GDU_IS_DRIVE (drive))
                        continue;

                if (g_list_find (drives_for_selected_presentables, drive) == NULL)
                        drives_for_selected_presentables = g_list_prepend (drives_for_selected_presentables, drive);
        }

        /* hash from drive to largest free component */
        map_disks_to_biggest_component_size = g_hash_table_new_full ((GHashFunc) gdu_presentable_hash,
                                                                     (GEqualFunc) gdu_presentable_equals,
                                                                     g_object_unref,
                                                                     g_free);
        for (l = presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GduPresentable *drive;
                guint64 size;
                guint64 *existing_size;

                if (gdu_presentable_is_allocated (p))
                        continue;

                drive = gdu_presentable_get_toplevel (p);
                if (drive == NULL)
                        continue;

                g_object_unref (drive); /* don't own the ref */
                if (!GDU_IS_DRIVE (drive))
                        continue;

                size = gdu_presentable_get_size (p);

                existing_size = g_hash_table_lookup (map_disks_to_biggest_component_size,
                                                     drive);
                if ((existing_size == NULL) || (existing_size != NULL && (*existing_size) > size)) {
                        g_hash_table_insert (map_disks_to_biggest_component_size,
                                             g_object_ref (drive),
                                             g_memdup (&size, sizeof (guint64)));
                }
        }

        dialog->priv->available_num_disks = g_hash_table_size (map_disks_to_biggest_component_size);

        if (dialog->priv->num_disks > dialog->priv->available_num_disks) {
                dialog->priv->available_total_size = 0;
        } else {
                GList *sizes;

                sizes = g_hash_table_get_values (map_disks_to_biggest_component_size);
                sizes = g_list_sort (sizes, guint64_compare);
                sizes = g_list_reverse (sizes);
                /* biggest is now first */

                dialog->priv->available_total_size = dialog->priv->num_disks *
                        (*((guint64 *) g_list_nth_data (sizes, dialog->priv->num_disks - 1)));

                g_list_free (sizes);
        }

        /* clamp total_size and num_disks to what is available */
        if (dialog->priv->total_size > dialog->priv->available_total_size)
                dialog->priv->total_size = dialog->priv->available_total_size;
        if (dialog->priv->num_disks > dialog->priv->available_num_disks)
                dialog->priv->num_disks = dialog->priv->available_num_disks;

        //if (dialog->priv->total_size == 0)
        //        dialog->priv->total_size = dialog->priv->available_total_size;

        g_debug ("==========");
        g_debug ("available_num_disks = %d", dialog->priv->available_num_disks);
        g_debug ("available_total_size = %" G_GUINT64_FORMAT, dialog->priv->available_total_size);
        g_debug ("num_disks = %d", dialog->priv->num_disks);
        g_debug ("total_size = %" G_GUINT64_FORMAT, dialog->priv->total_size);

        if (dialog->priv->available_total_size == 0) {
                gtk_widget_set_sensitive (dialog->priv->size_widget, FALSE);
                gtk_widget_set_sensitive (dialog->priv->tree_view, FALSE);
                tip_text = g_strdup (_("Not enough disks with available space. "
                                       "Plug in more disks or make space available."));
                tip_stock_icon = GTK_STOCK_DIALOG_ERROR;
        } else {
                gtk_widget_set_sensitive (dialog->priv->size_widget, TRUE);
                gtk_widget_set_sensitive (dialog->priv->tree_view, TRUE);
        }

        /* set range for num_disks_spin_button according to what we've found out */
        gtk_spin_button_set_range (GTK_SPIN_BUTTON (dialog->priv->num_disks_spin_button),
                                   2.0,
                                   dialog->priv->available_num_disks);
        if (gtk_spin_button_get_value (GTK_SPIN_BUTTON (dialog->priv->num_disks_spin_button)) != dialog->priv->num_disks)
                gtk_spin_button_set_value (GTK_SPIN_BUTTON (dialog->priv->num_disks_spin_button),
                                           dialog->priv->num_disks);

        /* ditto for the size widget */
        gdu_size_widget_set_max_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                      dialog->priv->available_total_size);
        if (gdu_size_widget_get_size (GDU_SIZE_WIDGET (dialog->priv->size_widget)) != dialog->priv->total_size)
                gdu_size_widget_set_size (GDU_SIZE_WIDGET (dialog->priv->size_widget),
                                          dialog->priv->total_size);

        /* --- */

        /* now update the tree model toggles to make free space objects selectable */
        num_toggled = 0;
        for (l = presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GduPresentable *drive;
                guint64 size;
                gboolean can_be_toggled;
                GtkTreeIter iter;
                gboolean cur_can_be_toggled;
                gboolean cur_toggled;

                can_be_toggled = FALSE;

                if (gdu_presentable_is_allocated (p))
                        goto determined;

                drive = gdu_presentable_get_toplevel (p);
                if (drive == NULL)
                        goto determined;

                g_object_unref (drive); /* don't own the ref */
                if (!GDU_IS_DRIVE (drive))
                        goto determined;

                size = gdu_presentable_get_size (p);

                if (size < dialog->priv->total_size / dialog->priv->num_disks)
                        goto determined;

                /* don't allow selecting two volumes on the same drive */
                if (g_list_find (drives_for_selected_presentables, drive) != NULL &&
                    g_list_find (selected_presentables, p) == NULL)
                        goto determined;

                can_be_toggled = TRUE;

        determined:
                if (!gdu_pool_tree_model_get_iter_for_presentable (dialog->priv->model,
                                                                   p,
                                                                   &iter)) {
                        g_warning ("Cannot find tree iter for presentable");
                        continue;
                }

                /* only update if there's a change - otherwise we'll cause loops */
                gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->model),
                                    &iter,
                                    GDU_POOL_TREE_MODEL_COLUMN_CAN_BE_TOGGLED, &cur_can_be_toggled,
                                    GDU_POOL_TREE_MODEL_COLUMN_TOGGLED, &cur_toggled,
                                    -1);

                if (cur_can_be_toggled != can_be_toggled) {
                        gtk_tree_store_set (GTK_TREE_STORE (dialog->priv->model),
                                            &iter,
                                            GDU_POOL_TREE_MODEL_COLUMN_CAN_BE_TOGGLED, can_be_toggled,
                                            -1);
                }
                if (!can_be_toggled) {
                        if (cur_toggled) {
                                gtk_tree_store_set (GTK_TREE_STORE (dialog->priv->model),
                                                    &iter,
                                                    GDU_POOL_TREE_MODEL_COLUMN_TOGGLED, FALSE,
                                                    -1);
                                cur_toggled = FALSE;
                        }
                }

                if (cur_toggled)
                        num_toggled++;
        }

        /* --- */

        if (num_toggled == dialog->priv->num_disks) {
                can_create = TRUE;
                if (tip_text == NULL) {
                        if (dialog->priv->total_size < 1000 * 1000) {
                                tip_text = g_strdup (_("Increase the size of the array."));
                                tip_stock_icon = GTK_STOCK_DIALOG_INFO;
                                can_create = FALSE;
                        } else {
                                tip_text = g_strdup (_("Array is ready to be created."));
                                tip_stock_icon = GTK_STOCK_DIALOG_INFO;
                        }
                }
        } else if (num_toggled < dialog->priv->num_disks) {
                if (tip_text == NULL) {
                        tip_text = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
                                                                 N_("Select one more component."),
                                                                 N_("Select %d more components."),
                                                                 dialog->priv->num_disks - num_toggled),
                                                    dialog->priv->num_disks - num_toggled);
                        tip_stock_icon = GTK_STOCK_DIALOG_INFO;
                }
        } else {
                /* num_toggled > dialog->priv->num_disks */
                if (tip_text == NULL) {
                        tip_text = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
                                                                 N_("Deselect one more component."),
                                                                 N_("Deselect %d more components."),
                                                                 dialog->priv->num_disks - num_toggled),
                                                    dialog->priv->num_disks - num_toggled);
                        tip_stock_icon = GTK_STOCK_DIALOG_INFO;
                }
        }

        gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           can_create);

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

        /* --- */

        g_list_foreach (presentables, (GFunc) g_object_unref, NULL);
        g_list_free (presentables);
        g_list_foreach (selected_presentables, (GFunc) g_object_unref, NULL);
        g_list_free (selected_presentables);
        g_list_free (drives_for_selected_presentables);
        g_hash_table_unref (map_disks_to_biggest_component_size);
        g_free (tip_text);
}

/* ---------------------------------------------------------------------------------------------------- */
