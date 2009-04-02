/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-partition.c
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

#include "gdu-section-partition.h"

struct _GduSectionPartitionPrivate
{
        GtkWidget *modify_part_label_entry;
        GtkWidget *modify_part_type_combo_box;
        GtkWidget *modify_part_flag_boot_check_button;
        GtkWidget *modify_part_flag_required_check_button;
        GtkWidget *modify_part_revert_button;

        PolKitAction *pk_change_action;
        PolKitAction *pk_change_system_internal_action;
        PolKitGnomeAction *modify_partition_action;
        PolKitGnomeAction *delete_partition_action;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionPartition, gdu_section_partition, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
op_delete_partition_callback (GduDevice *device,
                              GError *error,
                              gpointer user_data)
{
        GduSection *section = GDU_SECTION (user_data);
        if (error != NULL) {
                gdu_shell_raise_error (gdu_section_get_shell (section),
                                       gdu_section_get_presentable (section),
                                       error,
                                       _("Error deleting partition"));
        }
        g_object_unref (section);
}

static void
delete_partition_callback (GtkAction *action, gpointer user_data)
{
        GduSectionPartition *section = GDU_SECTION_PARTITION (user_data);
        GduDevice *device;
        GduPresentable *presentable;
        char *primary;
        char *secondary;
        gboolean do_erase;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        char *drive_name;
        char *name;
        const gchar *type;
        gint msdos_type;

        primary = NULL;
        secondary = NULL;
        toplevel_presentable = NULL;
        toplevel_device = NULL;
        drive_name = NULL;
        name = NULL;

	presentable = gdu_section_get_presentable (GDU_SECTION (section));
        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        toplevel_presentable = gdu_presentable_get_toplevel (
                gdu_section_get_presentable (GDU_SECTION (section)));
        if (toplevel_presentable == NULL) {
                g_warning ("%s: no toplevel presentable",  __FUNCTION__);
        }
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }

        drive_name = gdu_presentable_get_name (toplevel_presentable);
        name = gdu_presentable_get_name (presentable);
        type = gdu_device_partition_get_type (device);
        msdos_type = strtol (type, NULL, 0);

        primary = g_strconcat ("<b><big>", _("Are you sure you want to remove the partition, deleting existing data ?"), "</big></b>", NULL);

        if (gdu_device_is_removable (toplevel_device)) {
                if (name != NULL && strlen (name) > 0) {
                        if (msdos_type == 0x05 || msdos_type == 0x0f || msdos_type == 0x85) {
                                secondary = g_strdup_printf (_("All data on partition %d with name \"%s\" on the media in \"%s\" "
                                                             "and all partitions contained in this extended partition "
                                                             "will be irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             name,
                                                             drive_name);
                        }
                        else {
                                secondary = g_strdup_printf (_("All data on partition %d with name \"%s\" on the media in \"%s\" will be "
                                                             "irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             name,
                                                             drive_name);
                        }
                } else {
                        if (msdos_type == 0x05 || msdos_type == 0x0f || msdos_type == 0x85) {
                                secondary = g_strdup_printf (_("All data on partition %d on the media in \"%s\" "
                                                             "and all partitions contained in this extended partition "
                                                             "will be irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             drive_name);
                        }
                        else {
                                secondary = g_strdup_printf (_("All data on partition %d on the media in \"%s\" "
                                                             "will be irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             drive_name);
                        }
                }
        } else {
                if (name != NULL && strlen (name) > 0) {
                        if (msdos_type == 0x05 || msdos_type == 0x0f || msdos_type == 0x85) {
                                secondary = g_strdup_printf (_("All data on partition %d with name \"%s\" of \"%s\" "
                                                             "and all partitions contained in this extended partition "
                                                             "will be irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             name,
                                                             drive_name);
                        }
                        else {
                                secondary = g_strdup_printf (_("All data on partition %d with name \"%s\" of \"%s\" will be "
                                                             "irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             name,
                                                             drive_name);
                        }
                } else {
                        if (msdos_type == 0x05 || msdos_type == 0x0f || msdos_type == 0x85) {
                                secondary = g_strdup_printf (_("All data on partition %d of \"%s\" "
                                                             "and all partitions contained in this extended partition "
                                                             "will be irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             drive_name);
                        }
                        else {
                                secondary = g_strdup_printf (_("All data on partition %d of \"%s\" will be "
                                                             "irrecovably erased.\n\n"
                                                             "Make sure important data is backed up. "
                                                             "This action cannot be undone."),
                                                             gdu_device_partition_get_number (device),
                                                             drive_name);
                        }
                }
        }

        do_erase = gdu_util_delete_confirmation_dialog (gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section))),
                                                        "",
                                                        primary,
                                                        secondary,
                                                        _("_Delete Partition"));

        if (!do_erase)
                goto out;

        gdu_device_op_partition_delete (device,
                                        op_delete_partition_callback,
                                        g_object_ref (section));

        /* Note that we'll automatically go to toplevel once we get a notification
         * that the partition is removed.
         */

out:
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
        g_free (primary);
        g_free (secondary);
        g_free (drive_name);
}

static gboolean
has_flag (char **flags, const char *flag)
{
        int n;

        n = 0;
        while (flags != NULL && flags[n] != NULL) {
                if (strcmp (flags[n], flag) == 0)
                        return TRUE;
                n++;
        }
        return FALSE;
}

static void
modify_part_update_revert_apply_sensitivity (GduSectionPartition *section)
{
        gboolean label_differ;
        gboolean type_differ;
        gboolean flags_differ;
        char *selected_type;
        GduDevice *device;
        char **flags;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        label_differ = FALSE;
        type_differ = FALSE;
        flags_differ = FALSE;

        if (strcmp (gdu_device_partition_get_scheme (device), "gpt") == 0 ||
            strcmp (gdu_device_partition_get_scheme (device), "apm") == 0) {
                if (strcmp (gdu_device_partition_get_label (device),
                            gtk_entry_get_text (GTK_ENTRY (section->priv->modify_part_label_entry))) != 0) {
                        label_differ = TRUE;
                }
        }

        flags = gdu_device_partition_get_flags (device);
        if (has_flag (flags, "boot") !=
            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (section->priv->modify_part_flag_boot_check_button)))
                flags_differ = TRUE;
        if (has_flag (flags, "required") !=
            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (section->priv->modify_part_flag_required_check_button)))
                flags_differ = TRUE;

        selected_type = gdu_util_part_type_combo_box_get_selected (section->priv->modify_part_type_combo_box);
        if (selected_type != NULL && strcmp (gdu_device_partition_get_type (device), selected_type) != 0) {
                type_differ = TRUE;
        }
        g_free (selected_type);

        if (label_differ || type_differ || flags_differ) {
                gtk_widget_set_sensitive (section->priv->modify_part_revert_button, TRUE);
                polkit_gnome_action_set_sensitive (section->priv->modify_partition_action, TRUE);
        } else {
                gtk_widget_set_sensitive (section->priv->modify_part_revert_button, FALSE);
                polkit_gnome_action_set_sensitive (section->priv->modify_partition_action, FALSE);
        }


out:
        if (device != NULL)
                g_object_unref (device);
}

static void
update_partition_section (GduSectionPartition *section)
{
        gboolean show_flag_boot;
        gboolean show_flag_required;
        gboolean can_edit_part_label;
        GduDevice *device;
        const char *scheme;
        guint64 size;
        char **flags;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        g_object_set (section->priv->modify_partition_action,
                      "polkit-action",
                      gdu_device_is_system_internal (device) ?
                        section->priv->pk_change_system_internal_action :
                        section->priv->pk_change_action,
                      NULL);

        g_object_set (section->priv->delete_partition_action,
                      "polkit-action",
                      gdu_device_is_system_internal (device) ?
                        section->priv->pk_change_system_internal_action :
                        section->priv->pk_change_action,
                      NULL);

        size = gdu_device_partition_get_size (device);
        scheme = gdu_device_partition_get_scheme (device);

        gdu_util_part_type_combo_box_rebuild (section->priv->modify_part_type_combo_box, scheme);
        gdu_util_part_type_combo_box_select (section->priv->modify_part_type_combo_box,
                                             gdu_device_partition_get_type (device));

        can_edit_part_label = FALSE;
        show_flag_boot = FALSE;
        show_flag_required = FALSE;

        if (strcmp (scheme, "mbr") == 0) {
                show_flag_boot = TRUE;
        }

        if (strcmp (scheme, "gpt") == 0) {
                can_edit_part_label = TRUE;
                show_flag_required = TRUE;
        }

        if (strcmp (scheme, "apm") == 0) {
                can_edit_part_label = TRUE;
                show_flag_boot = TRUE;
        }

        if (show_flag_boot)
                gtk_widget_show (section->priv->modify_part_flag_boot_check_button);
        else
                gtk_widget_hide (section->priv->modify_part_flag_boot_check_button);

        if (show_flag_required)
                gtk_widget_show (section->priv->modify_part_flag_required_check_button);
        else
                gtk_widget_hide (section->priv->modify_part_flag_required_check_button);

        flags = gdu_device_partition_get_flags (device);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (section->priv->modify_part_flag_boot_check_button),
                                      has_flag (flags, "boot"));
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (section->priv->modify_part_flag_required_check_button),
                                      has_flag (flags, "required"));

        gtk_widget_set_sensitive (section->priv->modify_part_label_entry, can_edit_part_label);
        if (can_edit_part_label) {
                gtk_entry_set_text (GTK_ENTRY (section->priv->modify_part_label_entry),
                                    gdu_device_partition_get_label (device));
                /* TODO: check real max length */
                gtk_entry_set_max_length (GTK_ENTRY (section->priv->modify_part_label_entry), 31);
        } else {
                gtk_entry_set_text (GTK_ENTRY (section->priv->modify_part_label_entry), "");
        }

        modify_part_update_revert_apply_sensitivity (section);
out:
        if (device != NULL)
                g_object_unref (device);
}

static void
modify_part_type_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduSectionPartition *section = GDU_SECTION_PARTITION (user_data);
        modify_part_update_revert_apply_sensitivity (section);
}

static void
modify_part_label_entry_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduSectionPartition *section = GDU_SECTION_PARTITION (user_data);
        modify_part_update_revert_apply_sensitivity (section);
}

static void
modify_part_flag_check_button_clicked (GtkWidget *check_button, gpointer user_data)
{
        GduSectionPartition *section = GDU_SECTION_PARTITION (user_data);
        modify_part_update_revert_apply_sensitivity (section);
}

static void
modify_part_revert_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduSectionPartition *section = GDU_SECTION_PARTITION (user_data);
        update_partition_section (section);
}

static void
op_modify_partition_callback (GduDevice *device,
                              GError *error,
                              gpointer user_data)
{
        GduSection *section = GDU_SECTION (user_data);
        if (error != NULL) {
                gdu_shell_raise_error (gdu_section_get_shell (section),
                                       gdu_section_get_presentable (section),
                                       error,
                                       _("Error modifying partition"));
        }
        g_object_unref (section);
}

static void
modify_partition_callback (GtkAction *action, gpointer user_data)
{
        GduSectionPartition *section = GDU_SECTION_PARTITION (user_data);
        GduDevice *device;
        GPtrArray *flags;
        char *type;
        const char *label;
        char **existing_flags;
        char **flags_strv;
        gboolean flag_boot;
        gboolean flag_required;
        int n;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        /* this is generally a safe operation so don't prompt the user for his consent */

        existing_flags = gdu_device_partition_get_flags (device);

        flag_boot = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (section->priv->modify_part_flag_boot_check_button));
        flag_required = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (section->priv->modify_part_flag_required_check_button));

        flags = g_ptr_array_new ();
        for (n = 0; existing_flags[n] != NULL; n++) {
                g_warning ("existing_flags[n]='%s'", existing_flags[n]);
                if (strcmp (existing_flags[n], "boot") == 0) {
                        if (!flag_boot)
                                continue;
                        flag_boot = FALSE;
                }

                if (strcmp (existing_flags[n], "required") == 0) {
                        if (!flag_required)
                                continue;
                        flag_required = FALSE;
                }
                g_ptr_array_add (flags, g_strdup (existing_flags[n]));
        }
        if (flag_boot)
                g_ptr_array_add (flags, g_strdup ("boot"));
        if (flag_required)
                g_ptr_array_add (flags, g_strdup ("required"));
        g_ptr_array_add (flags, NULL);

        flags_strv = (char **) g_ptr_array_free (flags, FALSE);

        type = gdu_util_part_type_combo_box_get_selected (section->priv->modify_part_type_combo_box);
        label = gtk_entry_get_text (GTK_ENTRY (section->priv->modify_part_label_entry));

        gdu_device_op_partition_modify (device,
                                        type,
                                        label,
                                        flags_strv,
                                        op_modify_partition_callback,
                                        g_object_ref (section));
        g_free (type);
        g_strfreev (flags_strv);

out:
        if (device != NULL)
                g_object_unref (device);
}

static void
update (GduSectionPartition *section)
{
        update_partition_section (section);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_partition_finalize (GduSectionPartition *section)
{
        polkit_action_unref (section->priv->pk_change_action);
        polkit_action_unref (section->priv->pk_change_system_internal_action);
        g_object_unref (section->priv->delete_partition_action);
        g_object_unref (section->priv->modify_partition_action);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_partition_class_init (GduSectionPartitionClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_partition_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionPartitionPrivate));
}

static void
gdu_section_partition_init (GduSectionPartition *section)
{
        GtkWidget *vbox2;
        GtkWidget *button;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *entry;
        GtkWidget *combo_box;
        GtkWidget *check_button;
        GtkWidget *button_box;
        int row;
        gchar *s;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_PARTITION, GduSectionPartitionPrivate);

        section->priv->pk_change_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_change_action,
                                     "org.freedesktop.devicekit.disks.change");
        section->priv->pk_change_system_internal_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_change_system_internal_action,
                                     "org.freedesktop.devicekit.disks.change-system-internal");

        section->priv->delete_partition_action = polkit_gnome_action_new_default (
                "delete-partition",
                section->priv->pk_change_action,
                _("_Delete"),
                _("Delete"));
        g_object_set (section->priv->delete_partition_action,
                      "auth-label", _("_Delete..."),
                      "yes-icon-name", GTK_STOCK_DELETE,
                      "no-icon-name", GTK_STOCK_DELETE,
                      "auth-icon-name", GTK_STOCK_DELETE,
                      "self-blocked-icon-name", GTK_STOCK_DELETE,
                      NULL);
        g_signal_connect (section->priv->delete_partition_action, "activate",
                          G_CALLBACK (delete_partition_callback), section);

        section->priv->modify_partition_action = polkit_gnome_action_new_default (
                "modify-partition",
                section->priv->pk_change_action,
                _("_Apply"),
                _("Apply"));
        g_object_set (section->priv->modify_partition_action,
                      "auth-label", _("_Apply..."),
                      "yes-icon-name", GTK_STOCK_APPLY,
                      "no-icon-name", GTK_STOCK_APPLY,
                      "auth-icon-name", GTK_STOCK_APPLY,
                      "self-blocked-icon-name", GTK_STOCK_APPLY,
                      NULL);
        g_signal_connect (section->priv->modify_partition_action, "activate",
                          G_CALLBACK (modify_partition_callback), section);


        label = gtk_label_new (NULL);
        s = g_strconcat ("<b>", _("Partition"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 6);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The attributes of the partition can be edited. "
                                                   "The partition can also be deleted to make room for other data."));
        gtk_label_set_width_chars (GTK_LABEL (label), 50);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (6, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);

        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* partition label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Part_ition Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        section->priv->modify_part_label_entry = entry;

        row++;

        /* partition type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Ty_pe:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_part_type_combo_box_create (NULL);
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        section->priv->modify_part_type_combo_box = combo_box;

        row++;

        /* flags */

        /* used by mbr, apm */
        check_button = gtk_check_button_new_with_mnemonic (_("_Bootable"));
        gtk_table_attach (GTK_TABLE (table), check_button, 0, 2, row, row +1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        section->priv->modify_part_flag_boot_check_button = check_button;

        row++;

        /* used by gpt */
        check_button = gtk_check_button_new_with_mnemonic (_("Required / Firm_ware"));
        gtk_table_attach (GTK_TABLE (table), check_button, 0, 2, row, row +1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        section->priv->modify_part_flag_required_check_button = check_button;

        /* delete, revert and apply buttons */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);

        button = polkit_gnome_action_create_button (section->priv->delete_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        button = gtk_button_new_with_mnemonic (_("_Revert"));
        section->priv->modify_part_revert_button = button;
        gtk_container_add (GTK_CONTAINER (button_box), button);

        button = polkit_gnome_action_create_button (section->priv->modify_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        g_signal_connect (section->priv->modify_part_type_combo_box, "changed",
                          G_CALLBACK (modify_part_type_combo_box_changed), section);
        g_signal_connect (section->priv->modify_part_label_entry, "changed",
                          G_CALLBACK (modify_part_label_entry_changed), section);
        g_signal_connect (section->priv->modify_part_flag_boot_check_button, "toggled",
                          G_CALLBACK (modify_part_flag_check_button_clicked), section);
        g_signal_connect (section->priv->modify_part_flag_required_check_button, "toggled",
                          G_CALLBACK (modify_part_flag_check_button_clicked), section);
        g_signal_connect (section->priv->modify_part_revert_button, "clicked",
                          G_CALLBACK (modify_part_revert_button_clicked), section);
}

GtkWidget *
gdu_section_partition_new (GduShell       *shell,
                           GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_PARTITION,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
