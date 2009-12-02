/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) 2009 David Zeuthen
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
#include <glib/gi18n.h>
#include <atasmart.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "gdu-gtk.h"
#include "gdu-add-component-linux-md-dialog.h"

/* ---------------------------------------------------------------------------------------------------- */

struct GduAddComponentLinuxMdDialogPrivate
{
        GtkWidget *disk_selection_widget;
};

enum {
        PROP_0,
        PROP_DRIVE,
        PROP_SIZE
};

G_DEFINE_TYPE (GduAddComponentLinuxMdDialog, gdu_add_component_linux_md_dialog, GDU_TYPE_DIALOG)

static void
gdu_add_component_linux_md_dialog_finalize (GObject *object)
{
        /*GduAddComponentLinuxMdDialog *dialog = GDU_ADD_COMPONENT_LINUX_MD_DIALOG (object);*/

        if (G_OBJECT_CLASS (gdu_add_component_linux_md_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_add_component_linux_md_dialog_parent_class)->finalize (object);
}

static void
gdu_add_component_linux_md_dialog_get_property (GObject    *object,
                                                guint       property_id,
                                                GValue     *value,
                                                GParamSpec *pspec)
{
        GduAddComponentLinuxMdDialog *dialog = GDU_ADD_COMPONENT_LINUX_MD_DIALOG (object);

        switch (property_id) {
        case PROP_DRIVE:
                g_value_take_object (value, gdu_add_component_linux_md_dialog_get_drive (dialog));
                break;

        case PROP_SIZE:
                g_value_set_uint64 (value, gdu_add_component_linux_md_dialog_get_size (dialog));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_add_sensitivity (GduAddComponentLinuxMdDialog *dialog)
{
        GPtrArray *drives;
        gboolean add_is_sensitive;

        drives = gdu_disk_selection_widget_get_selected_drives (GDU_DISK_SELECTION_WIDGET (dialog->priv->disk_selection_widget));
        add_is_sensitive = (drives->len > 0);
        g_ptr_array_unref (drives);

        gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_APPLY,
                                           add_is_sensitive);
}

static void
update (GduAddComponentLinuxMdDialog *dialog)
{
        update_add_sensitivity (dialog);
}


/* ---------------------------------------------------------------------------------------------------- */

static void
on_disk_selection_widget_changed (GduDiskSelectionWidget *widget,
                                  gpointer                user_data)
{
        GduAddComponentLinuxMdDialog *dialog = GDU_ADD_COMPONENT_LINUX_MD_DIALOG (user_data);
        update_add_sensitivity (dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_add_component_linux_md_dialog_constructed (GObject *object)
{
        GduAddComponentLinuxMdDialog *dialog = GDU_ADD_COMPONENT_LINUX_MD_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *hbox;
        GtkWidget *vbox;
        GtkWidget *image;
        GtkWidget *label;
        gchar *s;
        GIcon *icon;
        GduPresentable *p;
        GduDevice *d;
        GduPool *pool;
        GtkWidget *disk_selection_widget;
        GList *slaves;
        GduDevice *slave;
        guint64 component_size;
        gchar *component_size_str;
        gchar *array_name;
        gchar *array_name_vpd;

        slaves = gdu_linux_md_drive_get_slaves (GDU_LINUX_MD_DRIVE (gdu_dialog_get_presentable (GDU_DIALOG (dialog))));
        slave = GDU_DEVICE (slaves->data);
        /* TODO: need size slider for e.g. linear arrays */
        component_size = gdu_device_get_size (slave);
        component_size_str = gdu_util_get_size_for_display (component_size, FALSE, FALSE);
        array_name = gdu_presentable_get_name (gdu_dialog_get_presentable (GDU_DIALOG (dialog)));
        array_name_vpd = gdu_presentable_get_vpd_name (gdu_dialog_get_presentable (GDU_DIALOG (dialog)));

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_CANCEL,
                               GTK_RESPONSE_CANCEL);

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_ADD,
                               GTK_RESPONSE_APPLY);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

        icon = gdu_presentable_get_icon (gdu_dialog_get_presentable (GDU_DIALOG (dialog)));

        hbox = gtk_hbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (content_area), hbox, TRUE, TRUE, 0);

        image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DIALOG);
        gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

        p = gdu_dialog_get_presentable (GDU_DIALOG (dialog));
        d = gdu_presentable_get_device (p);
        pool = gdu_presentable_get_pool (p);

        s = g_strdup_printf (_("Add Component to %s"), array_name);
        gtk_window_set_title (GTK_WINDOW (dialog), s);
        g_free (s);

        /* --- */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 70); /* TODO: hate */
        /* Translators: The first %s is the size (e.g. "42 GB") and the two following %s are the
         * name and vpd_name of the array (e.g. "Saturn" and "6 TB RAID-6")
         */
        s = g_strdup_printf (_("Select a disk to create a %s component on for the RAID Array \"%s\" (%s)"),
                             component_size_str,
                             array_name,
                             array_name_vpd);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        /* --- */

        disk_selection_widget = gdu_disk_selection_widget_new (pool,
                                                               NULL, /* TODO: ignored_drives */
                                                               GDU_DISK_SELECTION_WIDGET_FLAGS_NONE);
        gdu_disk_selection_widget_set_component_size (GDU_DISK_SELECTION_WIDGET (disk_selection_widget),
                                                      component_size);
        dialog->priv->disk_selection_widget = disk_selection_widget;
        gtk_box_pack_start (GTK_BOX (vbox), disk_selection_widget, TRUE, TRUE, 0);

        /* --- */


        g_signal_connect (dialog->priv->disk_selection_widget,
                          "changed",
                          G_CALLBACK (on_disk_selection_widget_changed),
                          dialog);

        g_object_unref (icon);
        g_object_unref (d);
        g_object_unref (pool);

        /* select a sane size for the widget and allow resizing */
        gtk_widget_set_size_request (GTK_WIDGET (dialog), 550, 450);
        gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

        update (dialog);

        g_list_foreach (slaves, (GFunc) g_object_unref, NULL);
        g_list_free (slaves);
        g_free (component_size_str);
        g_free (array_name);
        g_free (array_name_vpd);

        if (G_OBJECT_CLASS (gdu_add_component_linux_md_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_add_component_linux_md_dialog_parent_class)->constructed (object);
}

static void
gdu_add_component_linux_md_dialog_class_init (GduAddComponentLinuxMdDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduAddComponentLinuxMdDialogPrivate));

        object_class->get_property = gdu_add_component_linux_md_dialog_get_property;
        object_class->constructed  = gdu_add_component_linux_md_dialog_constructed;
        object_class->finalize     = gdu_add_component_linux_md_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_DRIVE,
                                         g_param_spec_object ("drive",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_DRIVE,
                                                              G_PARAM_READABLE));

        g_object_class_install_property (object_class,
                                         PROP_SIZE,
                                         g_param_spec_uint64 ("size",
                                                              NULL,
                                                              NULL,
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READABLE));
}

static void
gdu_add_component_linux_md_dialog_init (GduAddComponentLinuxMdDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog,
                                                    GDU_TYPE_ADD_COMPONENT_LINUX_MD_DIALOG,
                                                    GduAddComponentLinuxMdDialogPrivate);
}

GtkWidget *
gdu_add_component_linux_md_dialog_new (GtkWindow       *parent,
                                       GduLinuxMdDrive *linux_md_drive)
{
        g_return_val_if_fail (GDU_IS_LINUX_MD_DRIVE (linux_md_drive), NULL);
        return GTK_WIDGET (g_object_new (GDU_TYPE_ADD_COMPONENT_LINUX_MD_DIALOG,
                                         "transient-for", parent,
                                         "presentable", linux_md_drive,
                                         NULL));
}

GduDrive *
gdu_add_component_linux_md_dialog_get_drive (GduAddComponentLinuxMdDialog *dialog)
{
        GPtrArray *drives;
        GduDrive *ret;

        g_return_val_if_fail (GDU_IS_ADD_COMPONENT_LINUX_MD_DIALOG (dialog), NULL);

        ret = NULL;
        drives = gdu_disk_selection_widget_get_selected_drives (GDU_DISK_SELECTION_WIDGET (dialog->priv->disk_selection_widget));
        if (drives->len > 0)
                ret = g_object_ref (GDU_DRIVE (drives->pdata[0]));
        g_ptr_array_unref (drives);

        return ret;
}

guint64
gdu_add_component_linux_md_dialog_get_size  (GduAddComponentLinuxMdDialog *dialog)
{
        g_return_val_if_fail (GDU_IS_ADD_COMPONENT_LINUX_MD_DIALOG (dialog), 0);
        return gdu_disk_selection_widget_get_component_size (GDU_DISK_SELECTION_WIDGET (dialog->priv->disk_selection_widget));
}

