/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-volumes.c
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

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>

#include <gdu-gtk/gdu-gtk.h>
#include "gdu-section-volumes.h"

struct _GduSectionVolumesPrivate
{
        GduPresentable *cur_volume;

        GtkWidget *grid;
        GtkWidget *details_table;
        GtkWidget *buttons_align;

        /* shared between all volume types */
        GduDetailsElement *usage_element;
        GduDetailsElement *capacity_element;
        GduDetailsElement *partition_element;
        GduDetailsElement *device_element;

        /* elements for the 'filesystem' usage */
        GduDetailsElement *fs_type_element;
        GduDetailsElement *fs_available_element;
        GduDetailsElement *fs_label_element;
        GduDetailsElement *fs_mount_point_element;

        GtkWidget *fs_mount_button;
        GtkWidget *fs_unmount_button;
};

G_DEFINE_TYPE (GduSectionVolumes, gdu_section_volumes, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_volumes_finalize (GObject *object)
{
        GduSectionVolumes *section = GDU_SECTION_VOLUMES (object);

        if (section->priv->cur_volume != NULL)
                g_object_unref (section->priv->cur_volume);

        if (G_OBJECT_CLASS (gdu_section_volumes_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_section_volumes_parent_class)->finalize (object);
}

/* ---------------------------------------------------------------------------------------------------- */

static GtkWidget *
create_button (const gchar *icon_name,
               const gchar *button_primary,
               const gchar *button_secondary)
{
        GtkWidget *hbox;
        GtkWidget *label;
        GtkWidget *image;
        GtkWidget *button;
        gchar *s;

        image = gtk_image_new_from_icon_name (icon_name,
                                              GTK_ICON_SIZE_BUTTON);
        gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
        gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_single_line_mode (GTK_LABEL (label), FALSE);
        s = g_strdup_printf ("%s\n"
                             "<span fgcolor='#404040'><small>%s</small></span>",
                             button_primary,
                             button_secondary);
        gtk_label_set_markup (GTK_LABEL (label), s);
        gtk_label_set_use_underline (GTK_LABEL (label), TRUE);
        g_free (s);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

        button = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
        gtk_container_add (GTK_CONTAINER (button), hbox);

        gtk_widget_set_size_request (label, 250, -1);

        return button;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_button (GtkWidget *table,
            GtkWidget *button,
            guint     *row,
            guint     *column)
{
        guint num_columns;

        gtk_table_attach (GTK_TABLE (table),
                          button,
                          *column, *column + 1,
                          *row, *row + 1,
                          GTK_FILL,
                          GTK_FILL,
                          0, 0);

        g_object_get (table,
                      "n-columns", &num_columns,
                      NULL);

        *column += 1;
        if (*column >= num_columns) {
                *column = 0;
                *row +=1;
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
unmount_op_callback (GduDevice *device,
                     GError    *error,
                     gpointer   user_data)
{
        GduShell *shell = GDU_SHELL (user_data);

        /* TODO: handle busy mounts using GtkMountOperation */

        if (error != NULL) {
                GtkWidget *dialog;
                dialog = gdu_error_dialog_for_volume (GTK_WINDOW (gdu_shell_get_toplevel (shell)),
                                                      device,
                                                      _("Error unmounting volume"),
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
on_unmount_button_clicked (GtkButton *button,
                           gpointer   user_data)
{
        GduSectionVolumes *section = GDU_SECTION_VOLUMES (user_data);

        GduPresentable *v;
        GduDevice *d;

        v = NULL;
        d = NULL;

        v = gdu_volume_grid_get_selected (GDU_VOLUME_GRID (section->priv->grid));
        if (v == NULL)
                goto out;

        d = gdu_presentable_get_device (v);
        if (d == NULL)
                goto out;

        gdu_device_op_filesystem_unmount (d,
                                          unmount_op_callback,
                                          g_object_ref (gdu_section_get_shell (GDU_SECTION (section))));

 out:
        if (d != NULL)
                g_object_unref (d);
        if (v != NULL)
                g_object_unref (v);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mount_op_callback (GduDevice *device,
                   gchar     *mount_point,
                   GError    *error,
                   gpointer   user_data)
{
        GduShell *shell = GDU_SHELL (user_data);

        if (error != NULL) {
                gdu_shell_raise_error (shell,
                                       NULL,
                                       error,
                                       _("Error mounting device"));
                g_error_free (error);
        } else {
                g_free (mount_point);
        }
        g_object_unref (shell);
}

static void
on_mount_button_clicked (GtkButton *button,
                         gpointer   user_data)
{
        GduSectionVolumes *section = GDU_SECTION_VOLUMES (user_data);
        GduPresentable *v;
        GduDevice *d;

        v = NULL;
        d = NULL;

        v = gdu_volume_grid_get_selected (GDU_VOLUME_GRID (section->priv->grid));
        if (v == NULL)
                goto out;

        d = gdu_presentable_get_device (v);
        if (d == NULL)
                goto out;

        gdu_device_op_filesystem_mount (d,
                                        NULL,
                                        mount_op_callback,
                                        g_object_ref (gdu_section_get_shell (GDU_SECTION (section))));

 out:
        if (d != NULL)
                g_object_unref (d);
        if (v != NULL)
                g_object_unref (v);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_volumes_update (GduSection *_section)
{
        GduSectionVolumes *section = GDU_SECTION_VOLUMES (_section);
        GduPresentable *v;
        GduDevice *d;
        gchar *s;
        gchar *s2;
        const gchar *usage;

        v = NULL;
        d = NULL;
        usage = "";

        v = gdu_volume_grid_get_selected (GDU_VOLUME_GRID (section->priv->grid));

        if (v != NULL) {
                d = gdu_presentable_get_device (v);
                if (d != NULL) {
                        usage = gdu_device_id_get_usage (d);
                }
        }

        /* ---------------------------------------------------------------------------------------------------- */
        /* rebuild table if the selected volume has changed */

        if (section->priv->cur_volume != v) {
                GPtrArray *elements;
                GtkWidget *child;
                GtkWidget *table;
                GtkWidget *button;
                guint row;
                guint column;

                child = gtk_bin_get_child (GTK_BIN (section->priv->buttons_align));
                if (child != NULL)
                        gtk_container_remove (GTK_CONTAINER (section->priv->buttons_align), child);
                row = 0;
                column = 0;
                table = gtk_table_new (1, 2, FALSE);
                gtk_table_set_row_spacings (GTK_TABLE (table), 0);
                gtk_table_set_col_spacings (GTK_TABLE (table), 0);
                gtk_container_add (GTK_CONTAINER (section->priv->buttons_align), table);

                if (section->priv->cur_volume != NULL)
                        g_object_unref (section->priv->cur_volume);
                section->priv->cur_volume = v != NULL ? g_object_ref (v) : NULL;

                section->priv->usage_element = NULL;
                section->priv->capacity_element = NULL;
                section->priv->partition_element = NULL;
                section->priv->device_element = NULL;
                section->priv->fs_type_element = NULL;
                section->priv->fs_label_element = NULL;
                section->priv->fs_available_element = NULL;
                section->priv->fs_mount_point_element = NULL;

                elements = g_ptr_array_new_with_free_func (g_object_unref);

                section->priv->usage_element = gdu_details_element_new (_("Usage:"), NULL, NULL);
                g_ptr_array_add (elements, section->priv->usage_element);

                section->priv->device_element = gdu_details_element_new (_("Device:"), NULL, NULL);
                g_ptr_array_add (elements, section->priv->device_element);

                section->priv->partition_element = gdu_details_element_new (_("Partition:"), NULL, NULL);
                g_ptr_array_add (elements, section->priv->partition_element);

                section->priv->capacity_element = gdu_details_element_new (_("Capacity:"), NULL, NULL);
                g_ptr_array_add (elements, section->priv->capacity_element);

                if (g_strcmp0 (usage, "filesystem") == 0) {
                        section->priv->fs_type_element = gdu_details_element_new (_("Type:"), NULL, NULL);
                        g_ptr_array_add (elements, section->priv->fs_type_element);

                        section->priv->fs_available_element = gdu_details_element_new (_("Available:"), NULL, NULL);
                        g_ptr_array_add (elements, section->priv->fs_available_element);

                        section->priv->fs_label_element = gdu_details_element_new (_("Label:"), NULL, NULL);
                        g_ptr_array_add (elements, section->priv->fs_label_element);

                        section->priv->fs_mount_point_element = gdu_details_element_new (_("Mount Point:"), NULL, NULL);
                        g_ptr_array_add (elements, section->priv->fs_mount_point_element);

                        button = create_button ("gdu-mount",
                                                _("_Mount Volume"),
                                                _("Mount the volume"));
                        g_signal_connect (button,
                                          "clicked",
                                          G_CALLBACK (on_mount_button_clicked),
                                          section);
                        section->priv->fs_mount_button = button;
                        add_button (table, button, &row, &column);

                        button = create_button ("gdu-unmount",
                                                _("_Unmount Volume"),
                                                _("Unmount the volume"));
                        g_signal_connect (button,
                                          "clicked",
                                          G_CALLBACK (on_unmount_button_clicked),
                                          section);
                        section->priv->fs_unmount_button = button;
                        add_button (table, button, &row, &column);

                        button = create_button ("gdu-check-disk",
                                                _("_Check Filesystem"),
                                                _("Check the filesystem for errors"));
                        add_button (table, button, &row, &column);

                        button = create_button ("nautilus-gdu",
                                                _("Fo_rmat Volume"),
                                                _("Format the volume"));
                        add_button (table, button, &row, &column);

                        if (d != NULL && gdu_device_is_partition (d)) {
                                button = create_button (GTK_STOCK_EDIT,
                                                        _("Ed_it Partition"),
                                                        _("Change partition type and flags"));
                                add_button (table, button, &row, &column);

                                button = create_button (GTK_STOCK_DELETE,
                                                        _("D_elete Partition"),
                                                        _("Delete the partition"));
                                add_button (table, button, &row, &column);
                        }
                }

                gdu_details_table_set_elements (GDU_DETAILS_TABLE (section->priv->details_table), elements);
                g_ptr_array_unref (elements);

                gtk_widget_show_all (table);
        }

        /* ---------------------------------------------------------------------------------------------------- */
        /* reset all elements */

        if (section->priv->usage_element != NULL)
                gdu_details_element_set_text (section->priv->usage_element, "–");
        if (section->priv->capacity_element != NULL) {
                if (v != NULL) {
                        s = gdu_util_get_size_for_display (gdu_presentable_get_size (v), FALSE, TRUE);
                        gdu_details_element_set_text (section->priv->capacity_element, s);
                        g_free (s);
                } else {
                        gdu_details_element_set_text (section->priv->capacity_element, "–");
                }
        }
        if (section->priv->partition_element != NULL) {
                if (d != NULL && gdu_device_is_partition (d)) {
                        s = gdu_util_get_desc_for_part_type (gdu_device_partition_get_scheme (d),
                                                             gdu_device_partition_get_type (d));
                        gdu_details_element_set_text (section->priv->partition_element, s);
                        /* TODO: include partition flags... */
                        g_free (s);
                } else {
                        gdu_details_element_set_text (section->priv->partition_element, "–");
                }
        }
        if (section->priv->device_element != NULL) {
                if (d != NULL) {
                        gdu_details_element_set_text (section->priv->device_element,
                                                      gdu_device_get_device_file (d));
                } else {
                        gdu_details_element_set_text (section->priv->device_element, "–");
                }
        }
        if (section->priv->fs_type_element != NULL)
                gdu_details_element_set_text (section->priv->fs_type_element, "–");
        if (section->priv->fs_available_element != NULL)
                gdu_details_element_set_text (section->priv->fs_available_element, "–");
        if (section->priv->fs_label_element != NULL)
                gdu_details_element_set_text (section->priv->fs_label_element, "–");
        if (section->priv->fs_mount_point_element != NULL)
                gdu_details_element_set_text (section->priv->fs_mount_point_element, "–");

        if (v == NULL)
                goto out;

        /* ---------------------------------------------------------------------------------------------------- */
        /* populate according to usage */

        if (g_strcmp0 (usage, "filesystem") == 0) {
                gdu_details_element_set_text (section->priv->usage_element, _("Filesystem"));
                s = gdu_util_get_fstype_for_display (gdu_device_id_get_type (d),
                                                     gdu_device_id_get_version (d),
                                                     TRUE);
                gdu_details_element_set_text (section->priv->fs_type_element, s);
                g_free (s);
                gdu_details_element_set_text (section->priv->fs_label_element,
                                              gdu_device_id_get_label (d));

                /* TODO: figure out amount of free space */
                gdu_details_element_set_text (section->priv->fs_available_element, "–");


                if (gdu_device_is_mounted (d)) {
                        const gchar* const *mount_paths;

                        /* For now we ignore if the device is mounted in multiple places */
                        mount_paths = (const gchar* const *) gdu_device_get_mount_paths (d);
                        s = g_strdup_printf ("<a title=\"%s\" href=\"file://%s\">%s</a>",
                                              /* Translators: this the mount point hyperlink tooltip */
                                              _("View files on the volume"),
                                             mount_paths[0],
                                             mount_paths[0]);
                        /* Translators: this the the text for the mount point
                         * item - %s is the mount point, e.g. '/media/disk'
                         */
                        s2 = g_strdup_printf (_("Mounted at %s"), s);
                        gdu_details_element_set_text (section->priv->fs_mount_point_element, s2);
                        g_free (s);
                        g_free (s2);

                        gtk_widget_set_sensitive (section->priv->fs_mount_button, FALSE);
                        gtk_widget_set_sensitive (section->priv->fs_unmount_button, TRUE);
                } else {
                        gdu_details_element_set_text (section->priv->fs_mount_point_element, _("Not Mounted"));

                        gtk_widget_set_sensitive (section->priv->fs_mount_button, TRUE);
                        gtk_widget_set_sensitive (section->priv->fs_unmount_button, FALSE);
                }

        } else if (g_strcmp0 (usage, "") == 0 &&
                   d != NULL && gdu_device_is_partition (d) &&
                   g_strcmp0 (gdu_device_partition_get_scheme (d), "mbr") == 0 &&
                   (g_strcmp0 (gdu_device_partition_get_type (d), "0x05") == 0 ||
                    g_strcmp0 (gdu_device_partition_get_type (d), "0x0f") == 0 ||
                    g_strcmp0 (gdu_device_partition_get_type (d), "0x85") == 0)) {
                gdu_details_element_set_text (section->priv->usage_element, _("Container for Logical Partitions"));

        } else if (GDU_IS_VOLUME_HOLE (v)) {
                GduDevice *drive_device;
                gdu_details_element_set_text (section->priv->usage_element, _("Unallocated Space"));
                drive_device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
                gdu_details_element_set_text (section->priv->device_element,
                                              gdu_device_get_device_file (drive_device));
                g_object_unref (drive_device);
        }


 out:
        if (d != NULL)
                g_object_unref (d);
        if (v != NULL)
                g_object_unref (v);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_grid_changed (GduVolumeGrid *grid,
                 gpointer       user_data)
{
        GduSectionVolumes *section = GDU_SECTION_VOLUMES (user_data);

        gdu_section_volumes_update (GDU_SECTION (section));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_volumes_constructed (GObject *object)
{
        GduSectionVolumes *section = GDU_SECTION_VOLUMES (object);
        GtkWidget *grid;
        GtkWidget *align;
        GtkWidget *label;
        GtkWidget *vbox2;
        GtkWidget *table;
        gchar *s;

        gtk_box_set_spacing (GTK_BOX (section), 12);

        /*------------------------------------- */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        s = g_strconcat ("<b>", _("_Volumes"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        gtk_label_set_use_underline (GTK_LABEL (label), TRUE);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 0);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (align), vbox2);

        grid = gdu_volume_grid_new (GDU_DRIVE (gdu_section_get_presentable (GDU_SECTION (section))));
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), grid);
        section->priv->grid = grid;
        gtk_box_pack_start (GTK_BOX (vbox2),
                            grid,
                            FALSE,
                            FALSE,
                            0);
        g_signal_connect (grid,
                          "changed",
                          G_CALLBACK (on_grid_changed),
                          section);

        table = gdu_details_table_new (2, NULL);
        section->priv->details_table = table;
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        section->priv->buttons_align = align;
        gtk_box_pack_start (GTK_BOX (vbox2), align, FALSE, FALSE, 0);

        /* -------------------------------------------------------------------------------- */

        gtk_widget_show_all (GTK_WIDGET (section));

        if (G_OBJECT_CLASS (gdu_section_volumes_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_section_volumes_parent_class)->constructed (object);
}

static void
gdu_section_volumes_class_init (GduSectionVolumesClass *klass)
{
        GObjectClass *gobject_class;
        GduSectionClass *section_class;

        gobject_class = G_OBJECT_CLASS (klass);
        section_class = GDU_SECTION_CLASS (klass);

        gobject_class->finalize    = gdu_section_volumes_finalize;
        gobject_class->constructed = gdu_section_volumes_constructed;
        section_class->update      = gdu_section_volumes_update;

        g_type_class_add_private (klass, sizeof (GduSectionVolumesPrivate));
}

static void
gdu_section_volumes_init (GduSectionVolumes *section)
{
        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_VOLUMES, GduSectionVolumesPrivate);
}

GtkWidget *
gdu_section_volumes_new (GduShell       *shell,
                         GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_VOLUMES,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
