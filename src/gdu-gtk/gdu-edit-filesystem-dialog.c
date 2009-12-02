/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-edit-filesystem-dialog.c
 *
 * Copyright (C) 2009 David Zeuthen
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

#include "config.h"
#include <glib/gi18n-lib.h>

#include <atasmart.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "gdu-gtk.h"
#include "gdu-edit-filesystem-dialog.h"

/* ---------------------------------------------------------------------------------------------------- */

struct GduEditFilesystemDialogPrivate
{
        GtkWidget *label_entry;
};

enum {
        PROP_0,
        PROP_LABEL,
};

G_DEFINE_TYPE (GduEditFilesystemDialog, gdu_edit_filesystem_dialog, GDU_TYPE_DIALOG)

static void
gdu_edit_filesystem_dialog_finalize (GObject *object)
{
        //GduEditFilesystemDialog *dialog = GDU_EDIT_FILESYSTEM_DIALOG (object);

        if (G_OBJECT_CLASS (gdu_edit_filesystem_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_edit_filesystem_dialog_parent_class)->finalize (object);
}

static void
gdu_edit_filesystem_dialog_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
        GduEditFilesystemDialog *dialog = GDU_EDIT_FILESYSTEM_DIALOG (object);

        switch (property_id) {
        case PROP_LABEL:
                g_value_take_string (value, gdu_edit_filesystem_dialog_get_label (dialog));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_apply_sensitivity (GduEditFilesystemDialog *dialog)
{
        gboolean label_differ;
        GduDevice *device;

        device = gdu_dialog_get_device (GDU_DIALOG (dialog));

        label_differ = FALSE;

        if (g_strcmp0 (gdu_device_id_get_label (device),
                       gtk_entry_get_text (GTK_ENTRY (dialog->priv->label_entry))) != 0) {
                label_differ = TRUE;
        }

        if (label_differ) {
                gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_APPLY, TRUE);
        } else {
                gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_APPLY, FALSE);
        }
}

static void
update (GduEditFilesystemDialog *dialog)
{
        update_apply_sensitivity (dialog);
}


/* ---------------------------------------------------------------------------------------------------- */

static void
label_entry_changed (GtkWidget *combo_box,
                     gpointer   user_data)
{
        GduEditFilesystemDialog *dialog = GDU_EDIT_FILESYSTEM_DIALOG (user_data);
        update_apply_sensitivity (dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_edit_filesystem_dialog_constructed (GObject *object)
{
        GduEditFilesystemDialog *dialog = GDU_EDIT_FILESYSTEM_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *hbox;
        GtkWidget *vbox;
        GtkWidget *image;
        GtkWidget *label;
        gchar *s;
        gchar *s2;
        GIcon *icon;
        GduPresentable *p;
        GduDevice *d;
        GduPool *pool;
        GtkWidget *table;
        GtkWidget *entry;
        gint row;
        GduKnownFilesystem *kfs;

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_CANCEL,
                               GTK_RESPONSE_CANCEL);

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_APPLY,
                               GTK_RESPONSE_APPLY);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

        icon = gdu_presentable_get_icon (gdu_dialog_get_presentable (GDU_DIALOG (dialog)));

        hbox = gtk_hbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (content_area), hbox, TRUE, TRUE, 0);

        image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DIALOG);
        gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);

        p = gdu_dialog_get_presentable (GDU_DIALOG (dialog));
        d = gdu_presentable_get_device (p);
        pool = gdu_presentable_get_pool (p);
        kfs = gdu_pool_get_known_filesystem_by_id (pool, gdu_device_id_get_type (d));

        s2 = gdu_presentable_get_vpd_name (gdu_dialog_get_presentable (GDU_DIALOG (dialog)));
        s = g_strdup_printf (_("Edit Filesystem on %s"), s2);
        gtk_window_set_title (GTK_WINDOW (dialog), s);
        g_free (s);
        g_free (s2);

        /* --- */

        table = gtk_table_new (2, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        row = 0;

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        /* init with current label */
        gtk_entry_set_text (GTK_ENTRY (entry), gdu_device_id_get_label (d));
        if (kfs != NULL)
                gtk_entry_set_max_length (GTK_ENTRY (entry),
                                          gdu_known_filesystem_get_max_label_len (kfs));
        dialog->priv->label_entry = entry;

        row++;

        /* --- */


        g_signal_connect (dialog->priv->label_entry,
                          "changed",
                          G_CALLBACK (label_entry_changed),
                          dialog);

        g_object_unref (icon);
        g_object_unref (d);
        g_object_unref (pool);
        g_object_unref (kfs);

        update (dialog);

        if (G_OBJECT_CLASS (gdu_edit_filesystem_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_edit_filesystem_dialog_parent_class)->constructed (object);
}

static void
gdu_edit_filesystem_dialog_class_init (GduEditFilesystemDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduEditFilesystemDialogPrivate));

        object_class->get_property = gdu_edit_filesystem_dialog_get_property;
        object_class->constructed  = gdu_edit_filesystem_dialog_constructed;
        object_class->finalize     = gdu_edit_filesystem_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_LABEL,
                                         g_param_spec_string ("label",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READABLE));
}

static void
gdu_edit_filesystem_dialog_init (GduEditFilesystemDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog,
                                                    GDU_TYPE_EDIT_FILESYSTEM_DIALOG,
                                                    GduEditFilesystemDialogPrivate);
}

GtkWidget *
gdu_edit_filesystem_dialog_new (GtkWindow      *parent,
                               GduPresentable *presentable)
{
        g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);
        return GTK_WIDGET (g_object_new (GDU_TYPE_EDIT_FILESYSTEM_DIALOG,
                                         "transient-for", parent,
                                         "presentable", presentable,
                                         NULL));
}

gchar *
gdu_edit_filesystem_dialog_get_label (GduEditFilesystemDialog *dialog)
{
        g_return_val_if_fail (GDU_IS_EDIT_FILESYSTEM_DIALOG (dialog), NULL);
        return g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->priv->label_entry)));
}
