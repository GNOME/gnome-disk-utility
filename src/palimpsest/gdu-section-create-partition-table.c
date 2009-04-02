/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-create-partition-table.c
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
#include <polkit-gnome/polkit-gnome.h>

#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>

#include "gdu-section-create-partition-table.h"

struct _GduSectionCreatePartitionTablePrivate
{
        GtkWidget *create_part_table_vbox;
        GtkWidget *create_part_table_type_combo_box;
        PolKitAction *pk_change_action;
        PolKitAction *pk_change_system_internal_action;
        PolKitGnomeAction *create_part_table_action;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionCreatePartitionTable, gdu_section_create_partition_table, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
create_partition_table_callback (GduDevice *device,
                                 GError *error,
                                 gpointer user_data)
{
        GduSection *section = GDU_SECTION (user_data);
        if (error != NULL) {
                gdu_shell_raise_error (gdu_section_get_shell (section),
                                       gdu_section_get_presentable (section),
                                       error,
                                       _("Error creating partition table"));
        }
        g_object_unref (section);
}

static void
create_part_table_callback (GtkAction *action, gpointer user_data)
{
        GduSectionCreatePartitionTable *section = GDU_SECTION_CREATE_PARTITION_TABLE (user_data);
        GduDevice *device;
        gboolean do_erase;
        char *scheme;
        char *primary;
        char *secondary;
        char *drive_name;

        scheme = NULL;
        primary = NULL;
        secondary = NULL;
        drive_name = NULL;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        drive_name = gdu_presentable_get_name (gdu_section_get_presentable (GDU_SECTION (section)));

        primary = g_strconcat ("<b><big>", _("Are you sure you want to format the disk, deleting existing data ?"), "</big></b>", NULL);

        if (gdu_device_is_removable (device)) {
                secondary = g_strdup_printf (_("All data on the media in \"%s\" will be irrecovably erased. "
                                               "Make sure important data is backed up. "
                                               "This action cannot be undone."),
                                             drive_name);
        } else {
                secondary = g_strdup_printf (_("All data on the drive \"%s\" will be irrecovably erased. "
                                               "Make sure important data is backed up. "
                                               "This action cannot be undone."),
                                             drive_name);
        }

        do_erase = gdu_util_delete_confirmation_dialog (gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section))),
                                                        "",
                                                        primary,
                                                        secondary,
                                                        _("C_reate"));


        if (!do_erase)
                goto out;

        scheme = gdu_util_part_table_type_combo_box_get_selected (
                section->priv->create_part_table_type_combo_box);

        gdu_device_op_partition_table_create (device,
                                              scheme,
                                              create_partition_table_callback,
                                              g_object_ref (section));

out:
        if (device != NULL)
                g_object_unref (device);
        g_free (scheme);
        g_free (primary);
        g_free (secondary);
        g_free (drive_name);
}

static void
update (GduSectionCreatePartitionTable *section)
{
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        g_object_set (section->priv->create_part_table_action,
                      "polkit-action",
                      gdu_device_is_system_internal (device) ?
                        section->priv->pk_change_system_internal_action :
                        section->priv->pk_change_action,
                      NULL);

out:
        if (device != NULL)
                g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_create_partition_table_finalize (GduSectionCreatePartitionTable *section)
{
        polkit_action_unref (section->priv->pk_change_action);
        polkit_action_unref (section->priv->pk_change_system_internal_action);
        g_object_unref (section->priv->create_part_table_action);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_create_partition_table_class_init (GduSectionCreatePartitionTableClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_create_partition_table_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionCreatePartitionTablePrivate));
}

static void
gdu_section_create_partition_table_init (GduSectionCreatePartitionTable *section)
{
        int row;
        GtkWidget *vbox2;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *combo_box;
        GtkWidget *button;
        GtkWidget *button_box;
        char *text;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_CREATE_PARTITION_TABLE, GduSectionCreatePartitionTablePrivate);

        section->priv->pk_change_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_change_action,
                                     "org.freedesktop.devicekit.disks.change");
        section->priv->pk_change_system_internal_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_change_system_internal_action,
                                     "org.freedesktop.devicekit.disks.change-system-internal");
        section->priv->create_part_table_action = polkit_gnome_action_new_default (
                "create-part-table",
                section->priv->pk_change_action,
                _("C_reate"),
                _("Create"));
        g_object_set (section->priv->create_part_table_action,
                      "auth-label", _("_Create..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (section->priv->create_part_table_action, "activate",
                          G_CALLBACK (create_part_table_callback), section);


        label = gtk_label_new (NULL);
        text = g_strdup_printf ("<b>%s</b>", _("Create Partition Table"));
        gtk_label_set_markup (GTK_LABEL (label), text);
        g_free (text);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 6);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To create a new partition table, select the partition table "
                                                   "type and then press \"Create\". All existing data will be lost."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);

        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* partition table type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Ty_pe:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_part_table_type_combo_box_create ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        section->priv->create_part_table_type_combo_box = combo_box;

        row++;

        /* partition table type desc */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gdu_util_part_table_type_combo_box_set_desc_label (combo_box, label);

        row++;

        /* create button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = polkit_gnome_action_create_button (section->priv->create_part_table_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);
}

GtkWidget *
gdu_section_create_partition_table_new (GduShell       *shell,
                                        GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_CREATE_PARTITION_TABLE,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
