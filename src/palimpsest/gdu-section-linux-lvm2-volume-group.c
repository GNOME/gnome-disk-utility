/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-linux-md-drive.c
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

#include "config.h"
#include <glib/gi18n.h>

#include <string.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>

#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>

#include "gdu-section-drive.h"
#include "gdu-section-linux-lvm2-volume-group.h"

struct _GduSectionLinuxLvm2VolumeGroupPrivate
{
        GduDetailsElement *name_element;
        GduDetailsElement *state_element;
        GduDetailsElement *capacity_element;
        GduDetailsElement *extent_size_element;
        GduDetailsElement *unallocated_size_element;
        GduDetailsElement *num_pvs_element;

        GduButtonElement *vg_start_button;
        GduButtonElement *vg_stop_button;
};

G_DEFINE_TYPE (GduSectionLinuxLvm2VolumeGroup, gdu_section_linux_lvm2_volume_group, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_linux_lvm2_volume_group_finalize (GObject *object)
{
        //GduSectionLinuxLvm2VolumeGroup *section = GDU_SECTION_LINUX_LVM2_VOLUME_GROUP (object);

        if (G_OBJECT_CLASS (gdu_section_linux_lvm2_volume_group_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_section_linux_lvm2_volume_group_parent_class)->finalize (object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
lvm2_vg_start_op_callback (GduPool   *pool,
                           GError    *error,
                           gpointer   user_data)
{
        GduShell *shell = GDU_SHELL (user_data);

        if (error != NULL) {
                GtkWidget *dialog;
                dialog = gdu_error_dialog_new (GTK_WINDOW (gdu_shell_get_toplevel (shell)),
                                               NULL,
                                               _("Error starting Volume Group"),
                                               error);
                gtk_widget_show_all (dialog);
                gtk_window_present (GTK_WINDOW (dialog));
                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
                g_error_free (error);
        }
        g_object_unref (shell);
}

static void
on_lvm2_vg_start_button_clicked (GduButtonElement *button_element,
                                 gpointer          user_data)
{
        GduSectionLinuxLvm2VolumeGroup *section = GDU_SECTION_LINUX_LVM2_VOLUME_GROUP (user_data);
        GduLinuxLvm2VolumeGroup *vg;
        GduPool *pool;
        const gchar *uuid;

        vg = GDU_LINUX_LVM2_VOLUME_GROUP (gdu_section_get_presentable (GDU_SECTION (section)));
        pool = gdu_presentable_get_pool (GDU_PRESENTABLE (vg));

        uuid = gdu_linux_lvm2_volume_group_get_uuid (vg);

        gdu_pool_op_linux_lvm2_vg_start (pool,
                                        uuid,
                                        lvm2_vg_start_op_callback,
                                        g_object_ref (gdu_section_get_shell (GDU_SECTION (section))));

        g_object_unref (pool);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
lvm2_vg_stop_op_callback (GduPool   *pool,
                          GError    *error,
                          gpointer   user_data)
{
        GduShell *shell = GDU_SHELL (user_data);

        if (error != NULL) {
                GtkWidget *dialog;
                dialog = gdu_error_dialog_new (GTK_WINDOW (gdu_shell_get_toplevel (shell)),
                                               NULL,
                                               _("Error stopping Volume Group"),
                                               error);
                gtk_widget_show_all (dialog);
                gtk_window_present (GTK_WINDOW (dialog));
                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
                g_error_free (error);
        }
        g_object_unref (shell);
}

static void
on_lvm2_vg_stop_button_clicked (GduButtonElement *button_element,
                                gpointer          user_data)
{
        GduSectionLinuxLvm2VolumeGroup *section = GDU_SECTION_LINUX_LVM2_VOLUME_GROUP (user_data);
        GduLinuxLvm2VolumeGroup *vg;
        GduPool *pool;
        const gchar *uuid;

        vg = GDU_LINUX_LVM2_VOLUME_GROUP (gdu_section_get_presentable (GDU_SECTION (section)));
        pool = gdu_presentable_get_pool (GDU_PRESENTABLE (vg));

        uuid = gdu_linux_lvm2_volume_group_get_uuid (vg);

        gdu_pool_op_linux_lvm2_vg_stop (pool,
                                        uuid,
                                        lvm2_vg_stop_op_callback,
                                        g_object_ref (gdu_section_get_shell (GDU_SECTION (section))));

        g_object_unref (pool);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_linux_lvm2_volume_group_update (GduSection *_section)
{
        GduSectionLinuxLvm2VolumeGroup *section = GDU_SECTION_LINUX_LVM2_VOLUME_GROUP (_section);
        GduPresentable *p;
        GduLinuxLvm2VolumeGroup *vg;
        GduDevice *pv_device;
        const gchar *name;
        guint64 size;
        guint64 unallocated_size;
        guint64 extent_size;
        gchar **pvs;
        gchar *s;
        GduLinuxLvm2VolumeGroupState state;
        gchar *state_str;
        gboolean show_vg_start_button;
        gboolean show_vg_stop_button;

        show_vg_start_button = FALSE;
        show_vg_stop_button = FALSE;

        state_str = NULL;

        p = gdu_section_get_presentable (_section);
        vg = GDU_LINUX_LVM2_VOLUME_GROUP (p);

        pv_device = gdu_linux_lvm2_volume_group_get_pv_device (vg);
        if (pv_device == NULL)
                goto out;

        name = gdu_device_linux_lvm2_pv_get_group_name (pv_device);
        size = gdu_device_linux_lvm2_pv_get_group_size (pv_device);
        unallocated_size = gdu_device_linux_lvm2_pv_get_group_unallocated_size (pv_device);
        extent_size = gdu_device_linux_lvm2_pv_get_group_extent_size (pv_device);
        pvs = gdu_device_linux_lvm2_pv_get_group_physical_volumes (pv_device);

        gdu_details_element_set_text (section->priv->name_element, name);
        s = gdu_util_get_size_for_display (size, FALSE, TRUE);
        gdu_details_element_set_text (section->priv->capacity_element, s);
        g_free (s);
        s = gdu_util_get_size_for_display (unallocated_size, FALSE, TRUE);
        gdu_details_element_set_text (section->priv->unallocated_size_element, s);
        g_free (s);
        /* Use the nerd units here (MiB) since that's what LVM defaults to (divisble by sector size etc.) */
        s = gdu_util_get_size_for_display (extent_size, TRUE, TRUE);
        gdu_details_element_set_text (section->priv->extent_size_element, s);
        g_free (s);
        s = g_strdup_printf ("%d", g_strv_length (pvs));
        gdu_details_element_set_text (section->priv->num_pvs_element, s);
        g_free (s);

        state = gdu_linux_lvm2_volume_group_get_state (vg);

        switch (state) {
        case GDU_LINUX_LVM2_VOLUME_GROUP_STATE_NOT_RUNNING:
                state_str = g_strdup (_("Not Running"));
                show_vg_start_button = TRUE;
                break;
        case GDU_LINUX_LVM2_VOLUME_GROUP_STATE_PARTIALLY_RUNNING:
                state_str = g_strdup (_("Partially Running"));
                show_vg_start_button = TRUE;
                show_vg_stop_button = TRUE;
                break;
        case GDU_LINUX_LVM2_VOLUME_GROUP_STATE_RUNNING:
                state_str = g_strdup (_("Running"));
                show_vg_stop_button = TRUE;
                break;
        default:
                state_str = g_strdup_printf (_("Unknown (%d)"), state);
                show_vg_start_button = TRUE;
                show_vg_stop_button = TRUE;
                break;
        }
        gdu_details_element_set_text (section->priv->state_element, state_str);


 out:
        gdu_button_element_set_visible (section->priv->vg_start_button, show_vg_start_button);
        gdu_button_element_set_visible (section->priv->vg_stop_button, show_vg_stop_button);

        if (pv_device != NULL)
                g_object_unref (pv_device);
        g_free (state_str);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_linux_lvm2_volume_group_constructed (GObject *object)
{
        GduSectionLinuxLvm2VolumeGroup *section = GDU_SECTION_LINUX_LVM2_VOLUME_GROUP (object);
        GtkWidget *align;
        GtkWidget *label;
        GtkWidget *table;
        GtkWidget *vbox;
        gchar *s;
        GduPresentable *p;
        GduDevice *d;
        GPtrArray *elements;
        GduDetailsElement *element;
        GduButtonElement *button_element;

        p = gdu_section_get_presentable (GDU_SECTION (section));
        d = gdu_presentable_get_device (p);

        gtk_box_set_spacing (GTK_BOX (section), 12);

        /*------------------------------------- */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        s = g_strconcat ("<b>", _("Volume Group"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 0);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, FALSE, 0);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        elements = g_ptr_array_new_with_free_func (g_object_unref);

        element = gdu_details_element_new (_("Name:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->name_element = element;

        element = gdu_details_element_new (_("Extent Size:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->extent_size_element = element;

        element = gdu_details_element_new (_("Physical Volumes:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->num_pvs_element = element;

        element = gdu_details_element_new (_("Capacity:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->capacity_element = element;

        element = gdu_details_element_new (_("State:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->state_element = element;

        element = gdu_details_element_new (_("Unallocated:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->unallocated_size_element = element;

        table = gdu_details_table_new (2, elements);
        g_ptr_array_unref (elements);
        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        /* -------------------------------------------------------------------------------- */

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);

        table = gdu_button_table_new (2, NULL);
        gtk_container_add (GTK_CONTAINER (align), table);
        elements = g_ptr_array_new_with_free_func (g_object_unref);

        button_element = gdu_button_element_new ("gdu-raid-array-start",
                                                 _("St_art Volume Group"),
                                                 _("Activate all LVs in the VG"));
        g_signal_connect (button_element,
                          "clicked",
                          G_CALLBACK (on_lvm2_vg_start_button_clicked),
                          section);
        section->priv->vg_start_button = button_element;
        g_ptr_array_add (elements, button_element);

        button_element = gdu_button_element_new ("gdu-raid-array-stop",
                                                 _("St_op Volume Group"),
                                                 _("Deactivate all LVs in the VG"));
        g_signal_connect (button_element,
                          "clicked",
                          G_CALLBACK (on_lvm2_vg_stop_button_clicked),
                          section);
        section->priv->vg_stop_button = button_element;
        g_ptr_array_add (elements, button_element);

        gdu_button_table_set_elements (GDU_BUTTON_TABLE (table), elements);
        g_ptr_array_unref (elements);

        /* -------------------------------------------------------------------------------- */

        gtk_widget_show_all (GTK_WIDGET (section));

        if (d != NULL)
                g_object_unref (d);

        if (G_OBJECT_CLASS (gdu_section_linux_lvm2_volume_group_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_section_linux_lvm2_volume_group_parent_class)->constructed (object);
}

static void
gdu_section_linux_lvm2_volume_group_class_init (GduSectionLinuxLvm2VolumeGroupClass *klass)
{
        GObjectClass *gobject_class;
        GduSectionClass *section_class;

        gobject_class = G_OBJECT_CLASS (klass);
        section_class = GDU_SECTION_CLASS (klass);

        gobject_class->finalize    = gdu_section_linux_lvm2_volume_group_finalize;
        gobject_class->constructed = gdu_section_linux_lvm2_volume_group_constructed;
        section_class->update      = gdu_section_linux_lvm2_volume_group_update;

        g_type_class_add_private (klass, sizeof (GduSectionLinuxLvm2VolumeGroupPrivate));
}

static void
gdu_section_linux_lvm2_volume_group_init (GduSectionLinuxLvm2VolumeGroup *section)
{
        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_LINUX_LVM2_VOLUME_GROUP, GduSectionLinuxLvm2VolumeGroupPrivate);
}

GtkWidget *
gdu_section_linux_lvm2_volume_group_new (GduShell       *shell,
                                         GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_LINUX_LVM2_VOLUME_GROUP,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
