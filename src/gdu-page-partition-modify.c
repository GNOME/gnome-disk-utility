/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-partition-modify.c
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <glib-object.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-disk-widget.h"
#include "gdu-page.h"
#include "gdu-util.h"
#include "gdu-page-partition-modify.h"

struct _GduPagePartitionModifyPrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;
        GtkWidget *disk_widget;

        GtkWidget *modify_part_vbox;
        GtkWidget *modify_part_label_entry;
        GtkWidget *modify_part_type_combo_box;
        GtkWidget *modify_part_flag_boot_check_button;
        GtkWidget *modify_part_flag_required_check_button;
        GtkWidget *modify_part_revert_button;
        GtkWidget *modify_part_secure_erase_combo_box;

        GduPresentable *presentable;

        PolKitAction *pk_modify_partition_action;
        PolKitGnomeAction *modify_partition_action;

        PolKitAction *pk_delete_partition_action;
        PolKitGnomeAction *delete_partition_action;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_partition_modify_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPagePartitionModify, gdu_page_partition_modify, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_partition_modify_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void
gdu_page_partition_modify_finalize (GduPagePartitionModify *page)
{
        polkit_action_unref (page->priv->pk_delete_partition_action);
        g_object_unref (page->priv->delete_partition_action);

        polkit_action_unref (page->priv->pk_modify_partition_action);
        g_object_unref (page->priv->modify_partition_action);

        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);
        if (page->priv->presentable != NULL)
                g_object_unref (page->priv->presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_partition_modify_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (object);

        switch (prop_id) {
        case PROP_SHELL:
                if (page->priv->shell != NULL)
                        g_object_unref (page->priv->shell);
                page->priv->shell = g_object_ref (g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_page_partition_modify_get_property (GObject     *object,
                                    guint        prop_id,
                                    GValue      *value,
                                    GParamSpec  *pspec)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (object);

        switch (prop_id) {
        case PROP_SHELL:
                g_value_set_object (value, page->priv->shell);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_page_partition_modify_class_init (GduPagePartitionModifyClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_partition_modify_finalize;
        obj_class->set_property = gdu_page_partition_modify_set_property;
        obj_class->get_property = gdu_page_partition_modify_get_property;

        /**
         * GduPagePartitionModify:shell:
         *
         * The #GduShell instance hosting this page.
         */
        g_object_class_install_property (obj_class,
                                         PROP_SHELL,
                                         g_param_spec_object ("shell",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_SHELL,
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_READABLE));
}

static void
delete_partition_callback (GtkAction *action, gpointer user_data)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (user_data);
        int response;
        GtkWidget *dialog;
        GduDevice *device;
        GduPresentable *toplevel_presentable;
        char *secure_erase;

        secure_erase = NULL;

        toplevel_presentable = gdu_util_find_toplevel_presentable (page->priv->presentable);

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        /* TODO: mention what drive the partition is on etc. */
        dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (gdu_shell_get_toplevel (page->priv->shell)),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_MESSAGE_WARNING,
                                                     GTK_BUTTONS_CANCEL,
                                                     _("<b><big>Are you sure you want to delete the partition?</big></b>"));

        gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                                    _("All data on the partition will be irrecovably erased. "
                                                      "Make sure data important to you is backed up. "
                                                      "This action cannot be undone."));
        /* ... until we add data recovery to g-d-u! */

        gtk_dialog_add_button (GTK_DIALOG (dialog), _("Delete Partition"), 0);

        response = gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        if (response != 0)
                goto out;

        secure_erase = gdu_util_secure_erase_combo_box_get_selected (page->priv->modify_part_secure_erase_combo_box);

        gdu_device_op_delete_partition (device, secure_erase);

        /* we'll automatically go to toplevel once we get a
         * notification that the partition is removed
         */

out:
        g_free (secure_erase);
        g_object_unref (toplevel_presentable);
        g_object_unref (device);
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
modify_part_update_revert_apply_sensitivity (GduPagePartitionModify *page)
{
        gboolean label_differ;
        gboolean type_differ;
        gboolean flags_differ;
        char *selected_type;
        GduDevice *device;
        char **flags;

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
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
                            gtk_entry_get_text (GTK_ENTRY (page->priv->modify_part_label_entry))) != 0) {
                        label_differ = TRUE;
                }
        }

        flags = gdu_device_partition_get_flags (device);
        if (has_flag (flags, "boot") !=
            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->priv->modify_part_flag_boot_check_button)))
                flags_differ = TRUE;
        if (has_flag (flags, "required") !=
            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->priv->modify_part_flag_required_check_button)))
                flags_differ = TRUE;

        selected_type = gdu_util_part_type_combo_box_get_selected (page->priv->modify_part_type_combo_box);
        if (selected_type != NULL && strcmp (gdu_device_partition_get_type (device), selected_type) != 0) {
                type_differ = TRUE;
        }
        g_free (selected_type);

        if (label_differ || type_differ || flags_differ) {
                gtk_widget_set_sensitive (page->priv->modify_part_revert_button, TRUE);
                polkit_gnome_action_set_sensitive (page->priv->modify_partition_action, TRUE);
        } else {
                gtk_widget_set_sensitive (page->priv->modify_part_revert_button, FALSE);
                polkit_gnome_action_set_sensitive (page->priv->modify_partition_action, FALSE);
        }


out:
        if (device != NULL)
                g_object_unref (device);
}

static void
modify_part_revert (GduPagePartitionModify *page)
{
        gboolean show_flag_boot;
        gboolean show_flag_required;
        gboolean can_edit_part_label;
        GduDevice *device;
        const char *scheme;
        guint64 size;
        char **flags;

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        size = gdu_device_partition_get_size (device);
        scheme = gdu_device_partition_get_scheme (device);

        gdu_util_part_type_combo_box_rebuild (page->priv->modify_part_type_combo_box, scheme);
        gdu_util_part_type_combo_box_select (page->priv->modify_part_type_combo_box,
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
                gtk_widget_show (page->priv->modify_part_flag_boot_check_button);
        else
                gtk_widget_hide (page->priv->modify_part_flag_boot_check_button);

        if (show_flag_required)
                gtk_widget_show (page->priv->modify_part_flag_required_check_button);
        else
                gtk_widget_hide (page->priv->modify_part_flag_required_check_button);

        flags = gdu_device_partition_get_flags (device);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->priv->modify_part_flag_boot_check_button),
                                      has_flag (flags, "boot"));
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->priv->modify_part_flag_required_check_button),
                                      has_flag (flags, "required"));

        gtk_widget_set_sensitive (page->priv->modify_part_label_entry, can_edit_part_label);
        if (can_edit_part_label) {
                gtk_entry_set_text (GTK_ENTRY (page->priv->modify_part_label_entry),
                                    gdu_device_partition_get_label (device));
                /* TODO: check real max length */
                gtk_entry_set_max_length (GTK_ENTRY (page->priv->modify_part_label_entry), 31);
        } else {
                gtk_entry_set_text (GTK_ENTRY (page->priv->modify_part_label_entry), "");
        }

        modify_part_update_revert_apply_sensitivity (page);
out:
        if (device != NULL)
                g_object_unref (device);
}

static void
modify_part_type_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (user_data);
        modify_part_update_revert_apply_sensitivity (page);
}

static void
modify_part_label_entry_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (user_data);
        modify_part_update_revert_apply_sensitivity (page);
}

static void
modify_part_flag_check_button_clicked (GtkWidget *check_button, gpointer user_data)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (user_data);
        modify_part_update_revert_apply_sensitivity (page);
}

static void
modify_part_revert_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (user_data);
        modify_part_revert (page);
}

static void
modify_partition_callback (GtkAction *action, gpointer user_data)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (user_data);
        GduDevice *device;
        GPtrArray *flags;
        char *type;
        const char *label;
        char **existing_flags;
        char **flags_strv;
        gboolean flag_boot;
        gboolean flag_required;
        int n;

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        /* this is generally a safe operation so don't prompt the user for his consent */

        existing_flags = gdu_device_partition_get_flags (device);

        flag_boot = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->priv->modify_part_flag_boot_check_button));
        flag_required = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->priv->modify_part_flag_required_check_button));

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

        type = gdu_util_part_type_combo_box_get_selected (page->priv->modify_part_type_combo_box);
        label = gtk_entry_get_text (GTK_ENTRY (page->priv->modify_part_label_entry));

        gdu_device_op_modify_partition (device,
                                        type,
                                        label,
                                        flags_strv);
        g_free (type);
        g_strfreev (flags_strv);

out:
        if (device != NULL)
                g_object_unref (device);
}
static void
gdu_page_partition_modify_init (GduPagePartitionModify *page)
{
        GtkWidget *hbox;
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *vbox3;
        GtkWidget *button;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *entry;
        GtkWidget *combo_box;
        GtkWidget *check_button;
        GtkWidget *button_box;
        int row;

        page->priv = g_new0 (GduPagePartitionModifyPrivate, 1);

        page->priv->pk_delete_partition_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_delete_partition_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->delete_partition_action = polkit_gnome_action_new_default (
                "delete-partition",
                page->priv->pk_delete_partition_action,
                _("_Delete"),
                _("Delete"));
        g_object_set (page->priv->delete_partition_action,
                      "auth-label", _("_Delete..."),
                      "yes-icon-name", GTK_STOCK_DELETE,
                      "no-icon-name", GTK_STOCK_DELETE,
                      "auth-icon-name", GTK_STOCK_DELETE,
                      "self-blocked-icon-name", GTK_STOCK_DELETE,
                      NULL);
        g_signal_connect (page->priv->delete_partition_action, "activate",
                          G_CALLBACK (delete_partition_callback), page);

        page->priv->pk_modify_partition_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_modify_partition_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->modify_partition_action = polkit_gnome_action_new_default (
                "modify-partition",
                page->priv->pk_modify_partition_action,
                _("A_pply"),
                _("Apply"));
        g_object_set (page->priv->modify_partition_action,
                      "auth-label", _("A_pply..."),
                      "yes-icon-name", GTK_STOCK_APPLY,
                      "no-icon-name", GTK_STOCK_APPLY,
                      "auth-icon-name", GTK_STOCK_APPLY,
                      "self-blocked-icon-name", GTK_STOCK_APPLY,
                      NULL);
        g_signal_connect (page->priv->modify_partition_action, "activate",
                          G_CALLBACK (modify_partition_callback), page);

        page->priv->main_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        hbox = gtk_hbox_new (FALSE, 10);

        page->priv->disk_widget = gdu_disk_widget_new (NULL);
        gtk_widget_set_size_request (page->priv->disk_widget, 150, 150);

        vbox = gtk_vbox_new (FALSE, 10);

        /* ---------------- */
        /* Modify partition */
        /* ---------------- */

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox3, FALSE, TRUE, 0);
        page->priv->modify_part_vbox = vbox3;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Modify Partition</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("You can change the type, label, and flags of the partition. "));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        table = gtk_table_new (2, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        /* partition label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        page->priv->modify_part_label_entry = entry;

        row++;

        /* partition type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_part_type_combo_box_create (NULL);
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->modify_part_type_combo_box = combo_box;

        /* flags */

        /* used by mbr, apm */
        check_button = gtk_check_button_new_with_mnemonic (_("_Boot"));
        gtk_box_pack_start (GTK_BOX (vbox2), check_button, FALSE, TRUE, 0);
        page->priv->modify_part_flag_boot_check_button = check_button;

        /* used by gpt */
        check_button = gtk_check_button_new_with_mnemonic (_("R_equired"));
        gtk_box_pack_start (GTK_BOX (vbox2), check_button, FALSE, TRUE, 0);
        page->priv->modify_part_flag_required_check_button = check_button;

        /* revert and apply buttons */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = gtk_button_new_with_mnemonic (_("_Revert"));
        page->priv->modify_part_revert_button = button;
        gtk_container_add (GTK_CONTAINER (button_box), button);
        button = polkit_gnome_action_create_button (page->priv->modify_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        g_signal_connect (page->priv->modify_part_type_combo_box, "changed",
                          G_CALLBACK (modify_part_type_combo_box_changed), page);
        g_signal_connect (page->priv->modify_part_label_entry, "changed",
                          G_CALLBACK (modify_part_label_entry_changed), page);
        g_signal_connect (page->priv->modify_part_flag_boot_check_button, "toggled",
                          G_CALLBACK (modify_part_flag_check_button_clicked), page);
        g_signal_connect (page->priv->modify_part_flag_required_check_button, "toggled",
                          G_CALLBACK (modify_part_flag_check_button_clicked), page);
        g_signal_connect (page->priv->modify_part_revert_button, "clicked",
                          G_CALLBACK (modify_part_revert_button_clicked), page);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The partition can also be deleted to make room for other "
                                                   "partitions."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (2, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Secure Erase:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_secure_erase_combo_box_create ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->modify_part_secure_erase_combo_box = combo_box;

        row++;

        /* secure erase desc */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gdu_util_secure_erase_combo_box_set_desc_label (combo_box, label);

        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = polkit_gnome_action_create_button (page->priv->delete_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);


        gtk_box_pack_start (GTK_BOX (hbox), page->priv->disk_widget, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), hbox, TRUE, TRUE, 0);
}


GduPagePartitionModify *
gdu_page_partition_modify_new (GduShell *shell)
{
        return GDU_PAGE_PARTITION_MODIFY (g_object_new (GDU_TYPE_PAGE_PARTITION_MODIFY, "shell", shell, NULL));
}

static gboolean
gdu_page_partition_modify_update (GduPage *_page, GduPresentable *presentable)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (_page);
        GduDevice *device;
        gboolean show_page;
        guint64 size;
        GduDevice *toplevel_device;
        GduPresentable *toplevel_presentable;
        const char *scheme;

        show_page = FALSE;

        toplevel_presentable = NULL;
        toplevel_device = NULL;

        device = gdu_presentable_get_device (presentable);

        toplevel_presentable = gdu_util_find_toplevel_presentable (presentable);
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_presentable == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }

        if (device != NULL && gdu_device_is_partition (device)) {
                show_page = TRUE;
        }

        if (!show_page)
                goto out;

        scheme = gdu_device_partition_table_get_scheme (toplevel_device);

        if (page->priv->presentable != NULL)
                g_object_unref (page->priv->presentable);
        page->priv->presentable = g_object_ref (presentable);

        gdu_disk_widget_set_presentable (GDU_DISK_WIDGET (page->priv->disk_widget), presentable);
        gtk_widget_queue_draw_area (page->priv->disk_widget,
                                    0, 0,
                                    page->priv->disk_widget->allocation.width,
                                    page->priv->disk_widget->allocation.height);

        size = gdu_presentable_get_size (presentable);

        modify_part_revert (page);
        gtk_widget_show (page->priv->modify_part_vbox);

out:
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);

        return show_page;
}

static GtkWidget *
gdu_page_partition_modify_get_widget (GduPage *_page)
{
        GduPagePartitionModify *page = GDU_PAGE_PARTITION_MODIFY (_page);
        return page->priv->main_vbox;
}

static char *
gdu_page_partition_modify_get_name (GduPage *page)
{
        return g_strdup (_("Modify Partition"));
}

static void
gdu_page_partition_modify_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_partition_modify_get_widget;
        iface->get_name = gdu_page_partition_modify_get_name;
        iface->update = gdu_page_partition_modify_update;
}
