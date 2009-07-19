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

#include "config.h"

#include <glib/gi18n-lib.h>
#include "gdu-create-linux-md-dialog.h"

struct GduCreateLinuxMdDialogPrivate
{
        gchar *level;
        GduPool *pool;
        GtkWidget *name_entry;
};

enum
{
        PROP_0,
        PROP_POOL,
        PROP_LEVEL,
        PROP_NAME,
};

static void gdu_create_linux_md_dialog_constructed (GObject *object);

G_DEFINE_TYPE (GduCreateLinuxMdDialog, gdu_create_linux_md_dialog, GTK_TYPE_DIALOG)

static void
gdu_create_linux_md_dialog_finalize (GObject *object)
{
        GduCreateLinuxMdDialog *dialog = GDU_CREATE_LINUX_MD_DIALOG (object);

        g_object_unref (dialog->priv->pool);
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
        GtkWidget *vbox2;
        GdkPixbuf *pixbuf;
        gint row;
        gboolean ret;
        GtkWidget *align;
        gchar *s;

        ret = FALSE;

        pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                           "gdu-raid-array",
                                           48,
                                           0,
                                           NULL);

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
        gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 0);
        gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
        gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);

        gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
        gtk_window_set_title (GTK_WINDOW (dialog), "");
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "nautilus-gdu");

        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                        _("C_reate"),
                                        GTK_RESPONSE_OK);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
        gtk_container_set_border_width (GTK_CONTAINER (content_area), 10);

        /*  icon and text labels  */
        hbox = gtk_hbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (content_area), hbox, FALSE, TRUE, 0);

        image = gtk_image_new_from_pixbuf (pixbuf);
        gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 12);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 12, 0, 0);
        gtk_box_pack_start (GTK_BOX (hbox), align, TRUE, TRUE, 0);

        vbox2 = gtk_vbox_new (FALSE, 12);
        gtk_container_add (GTK_CONTAINER (align), vbox2);

        row = 0;

        table = gtk_table_new (2, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_table_set_row_spacings (GTK_TABLE (table), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        /*  filesystem type  */
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

        /*  filesystem label  */
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

        /* -------------------------------------------------------------------------------- */

        /* component tree-views */



        /* -------------------------------------------------------------------------------- */

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);

        image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_MENU);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	label = gtk_label_new (NULL);
        s = g_strconcat ("<i>",
                         _("Warning: All data on the volume will be irrevocably lost."),
                         "</i>",
                         NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);


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

        if (G_OBJECT_CLASS (gdu_create_linux_md_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_create_linux_md_dialog_parent_class)->constructed (object);
}

/* ---------------------------------------------------------------------------------------------------- */
