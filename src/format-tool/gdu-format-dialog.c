/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  format-window.c
 *
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Tomas Bzatek <tbzatek@redhat.com>
 *          David Zeuthen <davidz@redhat.com>
 *
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>

#include "gdu-format-dialog.h"

struct GduFormatDialogPrivate
{
        GduVolume *volume;
        gchar *fs_type;
        gboolean encrypt;
        GduPool *pool;
        GtkWidget *fs_label_entry;
};

enum
{
        PROP_0,
        PROP_VOLUME,
        PROP_FS_TYPE,
        PROP_FS_LABEL,
        PROP_ENCRYPT,
};

static void gdu_format_dialog_constructed (GObject *object);

G_DEFINE_TYPE (GduFormatDialog, gdu_format_dialog, GTK_TYPE_DIALOG)

static void
gdu_format_dialog_finalize (GObject *object)
{
        GduFormatDialog *dialog = GDU_FORMAT_DIALOG (object);

        g_object_unref (dialog->priv->volume);
        g_object_unref (dialog->priv->pool);
        g_free (dialog->priv->fs_type);

        if (G_OBJECT_CLASS (gdu_format_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_format_dialog_parent_class)->finalize (object);
}

static void
gdu_format_dialog_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GduFormatDialog *dialog = GDU_FORMAT_DIALOG (object);

        switch (property_id) {
        case PROP_VOLUME:
                g_value_set_object (value, dialog->priv->volume);
                break;

        case PROP_FS_TYPE:
                g_value_set_string (value, dialog->priv->fs_type);
                break;

        case PROP_FS_LABEL:
                g_value_set_string (value, gdu_format_dialog_get_fs_label (dialog));
                break;

        case PROP_ENCRYPT:
                g_value_set_boolean (value, dialog->priv->encrypt);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
gdu_format_dialog_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
        GduFormatDialog *dialog = GDU_FORMAT_DIALOG (object);

        switch (property_id) {
        case PROP_VOLUME:
                dialog->priv->volume = g_value_dup_object (value);
                dialog->priv->pool = gdu_presentable_get_pool (GDU_PRESENTABLE (dialog->priv->volume));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
gdu_format_dialog_class_init (GduFormatDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduFormatDialogPrivate));

        object_class->get_property = gdu_format_dialog_get_property;
        object_class->set_property = gdu_format_dialog_set_property;
        object_class->constructed  = gdu_format_dialog_constructed;
        object_class->finalize     = gdu_format_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_VOLUME,
                                         g_param_spec_object ("volume",
                                                              _("Volume"),
                                                              _("The volume to format"),
                                                              GDU_TYPE_VOLUME,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_FS_TYPE,
                                         g_param_spec_string ("fs-type",
                                                              _("Filesystem type"),
                                                              _("The selected filesystem type"),
                                                              NULL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_FS_LABEL,
                                         g_param_spec_string ("fs-label",
                                                              _("Filesystem label"),
                                                              _("The requested filesystem label"),
                                                              NULL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_STATIC_NAME |
                                                              G_PARAM_STATIC_NICK |
                                                              G_PARAM_STATIC_BLURB));

        g_object_class_install_property (object_class,
                                         PROP_ENCRYPT,
                                         g_param_spec_boolean ("encrypt",
                                                               _("Encryption"),
                                                               _("Whether the volume should be encrypted"),
                                                               FALSE,
                                                               G_PARAM_READABLE |
                                                               G_PARAM_STATIC_NAME |
                                                               G_PARAM_STATIC_NICK |
                                                               G_PARAM_STATIC_BLURB));
}

static void
gdu_format_dialog_init (GduFormatDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog, GDU_TYPE_FORMAT_DIALOG, GduFormatDialogPrivate);
}

GtkWidget *
gdu_format_dialog_new (GtkWindow *parent,
                       GduVolume *volume)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_FORMAT_DIALOG,
                                         "transient-for", parent,
                                         "volume", volume,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
gdu_format_dialog_get_fs_type  (GduFormatDialog *dialog)
{
        g_return_val_if_fail (GDU_IS_FORMAT_DIALOG (dialog), NULL);
        return g_strdup (dialog->priv->fs_type);
}

gchar *
gdu_format_dialog_get_fs_label (GduFormatDialog *dialog)
{
        g_return_val_if_fail (GDU_IS_FORMAT_DIALOG (dialog), NULL);
        return g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->priv->fs_label_entry)));
}

gboolean
gdu_format_dialog_get_encrypt  (GduFormatDialog *dialog)
{
        g_return_val_if_fail (GDU_IS_FORMAT_DIALOG (dialog), FALSE);
        return dialog->priv->encrypt;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_combo_box_changed (GtkWidget *combo_box,
                      gpointer   user_data)
{
        GduFormatDialog *dialog = GDU_FORMAT_DIALOG (user_data);
        GduKnownFilesystem *kfs;
        gint max_label_len;

        /* keep in sync with where combo box is constructed in constructed() */
        g_free (dialog->priv->fs_type);
        switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box))) {
        case 0:
                dialog->priv->fs_type = g_strdup ("vfat");
                dialog->priv->encrypt = FALSE;
                break;
        case 1:
                dialog->priv->fs_type = g_strdup ("ext2");
                dialog->priv->encrypt = FALSE;
                break;
        case 2:
                dialog->priv->fs_type = g_strdup ("ext3");
                dialog->priv->encrypt = FALSE;
                break;
        case 3:
                dialog->priv->fs_type = g_strdup ("vfat");
                dialog->priv->encrypt = TRUE;
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        max_label_len = 0;
        kfs = gdu_pool_get_known_filesystem_by_id (dialog->priv->pool, dialog->priv->fs_type);
        if (kfs != NULL) {
                max_label_len = gdu_known_filesystem_get_max_label_len (kfs);
                g_object_unref (kfs);
        }
        gtk_entry_set_max_length (GTK_ENTRY (dialog->priv->fs_label_entry), max_label_len);
}

static void
on_fs_label_entry_activated (GtkWidget *combo_box,
                             gpointer   user_data)
{
        GduFormatDialog *dialog = GDU_FORMAT_DIALOG (user_data);

        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_presentable_removed (GduPresentable *presentable,
                        gpointer        user_data)
{
        GduFormatDialog *dialog = GDU_FORMAT_DIALOG (user_data);

        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_format_dialog_constructed (GObject *object)
{
        GduFormatDialog *dialog = GDU_FORMAT_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *button;
        GtkWidget *icon;
        GtkWidget *label;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *table;
        GtkWidget *entry;
        GtkWidget *combo_box;
        GtkWidget *vbox2;
        GdkPixbuf *pixbuf;
        gint row;
        GduPool *pool;
        GduDevice *device;
        gboolean ret;
        GtkWidget *align;
        gchar *s;

        ret = FALSE;

        pool = gdu_presentable_get_pool (GDU_PRESENTABLE (dialog->priv->volume));
        device = gdu_presentable_get_device (GDU_PRESENTABLE (dialog->priv->volume));

        pixbuf = gdu_util_get_pixbuf_for_presentable (GDU_PRESENTABLE (dialog->priv->volume), GTK_ICON_SIZE_DIALOG);

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
                                        /* Translators: Format is used as a verb here */
                                        _("_Format"),
                                        GTK_RESPONSE_OK);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Disk _Utility"), GTK_RESPONSE_ACCEPT);
        icon = gtk_image_new_from_icon_name ("palimpsest", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image (GTK_BUTTON (button), icon);
        gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (gtk_dialog_get_action_area (GTK_DIALOG (dialog))),
                                            button,
                                            TRUE);
        gtk_widget_set_tooltip_text (button, _("Use Disk Utility to format volume"));
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
        /* Translators: 'type' means 'filesystem type' here. */
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gtk_combo_box_new_text ();
        /* keep in sync with on_combo_box_changed() */
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("Compatible with all systems (FAT)"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("Compatible with Linux (ext2)"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("Compatible with Linux (ext3)"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box),
                                   _("Encrypted, compatible with Linux (FAT)"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
        dialog->priv->fs_type = g_strdup ("vfat");
        dialog->priv->encrypt = FALSE;
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        row++;

        /*  filesystem label  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: 'name' means 'filesystem label' here. */
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Name:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        /* Translators: Keep length of translation of "New Volume" to less than 16 characters */
        gtk_entry_set_text (GTK_ENTRY (entry), _("New Volume"));
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        dialog->priv->fs_label_entry = entry;
        row++;

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

        g_signal_connect (dialog->priv->fs_label_entry,
                          "activate",
                          G_CALLBACK (on_fs_label_entry_activated),
                          dialog);

        /* nuke dialog if device is yanked */
        g_signal_connect (dialog->priv->volume,
                          "removed",
                          G_CALLBACK (on_presentable_removed),
                          dialog);

        gtk_widget_grab_focus (dialog->priv->fs_label_entry);
        gtk_editable_select_region (GTK_EDITABLE (dialog->priv->fs_label_entry), 0, 1000);

        if (G_OBJECT_CLASS (gdu_format_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_format_dialog_parent_class)->constructed (object);
}

/* ---------------------------------------------------------------------------------------------------- */
