/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-unrecognized.c
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

#include "gdu-section-unrecognized.h"

struct _GduSectionUnrecognizedPrivate
{
        gboolean init_done;

        GtkWidget *label_entry;
        GtkWidget *type_combo_box;
        GtkWidget *encrypt_check_button;
        GtkWidget *take_ownership_of_fs_check_button;

        PolKitAction *pk_change_action;
        PolKitAction *pk_change_system_internal_action;
        PolKitGnomeAction *erase_action;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionUnrecognized, gdu_section_unrecognized, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static char *
gdu_section_volume_unrecognized_get_fstype (GduSectionUnrecognized *section)
{
        return gdu_util_fstype_combo_box_get_selected (section->priv->type_combo_box);
}

static char *
gdu_section_volume_unrecognized_get_fslabel (GduSectionUnrecognized *section)
{
        char *ret;

        if (GTK_WIDGET_IS_SENSITIVE (section->priv->label_entry))
                ret = g_strdup (gtk_entry_get_text (GTK_ENTRY (section->priv->label_entry)));
        else
                ret = NULL;

        return ret;
}

static void
section_volume_unrecognized_type_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduSectionUnrecognized *section = GDU_SECTION_UNRECOGNIZED (user_data);
        char *fstype;
        GduKnownFilesystem *kfs;
        gboolean label_entry_sensitive;
        gboolean can_erase;
        gboolean have_owners;
        int max_label_len;
        GduPool *pool;

        fstype = NULL;

        label_entry_sensitive = FALSE;
        can_erase = FALSE;
        max_label_len = 0;
        have_owners = FALSE;

        pool = gdu_shell_get_pool (gdu_section_get_shell (GDU_SECTION (section)));

        fstype = gdu_util_fstype_combo_box_get_selected (combo_box);
        if (fstype != NULL) {
                kfs = gdu_pool_get_known_filesystem_by_id (pool, fstype);
                if (kfs != NULL) {
                        max_label_len = gdu_known_filesystem_get_max_label_len (kfs);
                        have_owners = gdu_known_filesystem_get_supports_unix_owners (kfs);
                        g_object_unref (kfs);
                }
                can_erase = TRUE;
        }

        if (max_label_len > 0)
                label_entry_sensitive = TRUE;

        gtk_entry_set_max_length (GTK_ENTRY (section->priv->label_entry), max_label_len);
        gtk_widget_set_sensitive (section->priv->label_entry, label_entry_sensitive);
        polkit_gnome_action_set_sensitive (section->priv->erase_action, can_erase);

        if (have_owners)
                gtk_widget_show (section->priv->take_ownership_of_fs_check_button);
        else
                gtk_widget_hide (section->priv->take_ownership_of_fs_check_button);

        g_free (fstype);
}

typedef struct {
        GduSectionUnrecognized *section;
        GduPresentable *presentable;
        char *encrypt_passphrase;
        gboolean save_in_keyring;
        gboolean save_in_keyring_session;
} CreateFilesystemData;

static void
create_filesystem_data_free (CreateFilesystemData *data)
{
        if (data->section != NULL)
                g_object_unref (data->section);
        if (data->presentable != NULL)
                g_object_unref (data->presentable);
        if (data->encrypt_passphrase != NULL) {
                memset (data->encrypt_passphrase, '\0', strlen (data->encrypt_passphrase));
                g_free (data->encrypt_passphrase);
        }
        g_free (data);
}

static void
erase_action_completed (GduDevice  *device,
                        GError     *error,
                        gpointer    user_data)
{
        CreateFilesystemData *data = user_data;

        if (error != NULL) {
                gdu_shell_raise_error (gdu_section_get_shell (GDU_SECTION (data->section)),
                                       data->presentable,
                                       error,
                                       _("Error creating partition"));
                g_error_free (error);
        } else if (data->encrypt_passphrase != NULL) {
                /* now set the passphrase if requested */
                if (data->save_in_keyring || data->save_in_keyring_session) {
                        gdu_util_save_secret (device,
                                              data->encrypt_passphrase,
                                              data->save_in_keyring_session);
                        /* make sure the tab for the encrypted device is updated (it displays whether
                         * the passphrase is in the keyring or now)
                         */
                        gdu_shell_update (gdu_section_get_shell (GDU_SECTION (data->section)));
                }
        }
        if (data != NULL)
                create_filesystem_data_free (data);
}

static void
erase_action_callback (GtkAction *action, gpointer user_data)
{
        GduSectionUnrecognized *section = GDU_SECTION_UNRECOGNIZED (user_data);
        char *fslabel;
        char *fstype;
        GduDevice *device;
        GduPool *pool;
        CreateFilesystemData *data;
        GduPresentable *presentable;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        gboolean do_erase;
        char *primary;
        char *secondary;
        char *drive_name;
        gboolean take_ownership;
        GduKnownFilesystem *kfs;

        data = NULL;
        fstype = NULL;
        fslabel = NULL;
        primary = NULL;
        secondary = NULL;
        device = NULL;
        toplevel_presentable = NULL;
        toplevel_device = NULL;
        drive_name = NULL;

        presentable = gdu_section_get_presentable (GDU_SECTION (section));
        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL",  __FUNCTION__);
                goto out;
        }

        pool = gdu_shell_get_pool (gdu_section_get_shell (GDU_SECTION (section)));

        toplevel_presentable = gdu_presentable_get_toplevel (presentable);
        if (toplevel_presentable == NULL) {
                g_warning ("%s: no toplevel presentable",  __FUNCTION__);
        }
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }

        drive_name = gdu_presentable_get_name (toplevel_presentable);

        fstype = gdu_section_volume_unrecognized_get_fstype (section);
        fslabel = gdu_section_volume_unrecognized_get_fslabel (section);
        if (fslabel == NULL)
                fslabel = g_strdup ("");

        take_ownership = FALSE;
        kfs = gdu_pool_get_known_filesystem_by_id (pool, fstype);
        if (kfs != NULL) {
                if (gdu_known_filesystem_get_supports_unix_owners (kfs) && gtk_toggle_button_get_active (
                            GTK_TOGGLE_BUTTON (section->priv->take_ownership_of_fs_check_button)))
                        take_ownership = TRUE;
                g_object_unref (kfs);
        }

        primary = g_strconcat ("<b><big>", _("Are you sure you want to create a new file system, deleting existing data ?"), "</big></b>", NULL);

        if (gdu_device_is_partition (device)) {
                if (gdu_device_is_removable (toplevel_device)) {
                        secondary = g_strdup_printf (_("All data on partition %d on the media in \"%s\" will be "
                                                       "irrecovably erased. "
                                                       "Make sure important data is backed up. "
                                                       "This action cannot be undone."),
                                                     gdu_device_partition_get_number (device),
                                                     drive_name);
                } else {
                        secondary = g_strdup_printf (_("All data on partition %d of \"%s\" will be "
                                                       "irrecovably erased. "
                                                       "Make sure important data is backed up. "
                                                       "This action cannot be undone."),
                                                     gdu_device_partition_get_number (device),
                                                     drive_name);
                }
        } else {
                if (gdu_device_is_removable (toplevel_device)) {
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
        }

        do_erase = gdu_util_delete_confirmation_dialog (gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section))),
                                                        "",
                                                            primary,
                                                            secondary,
                                                            _("C_reate"));

        if (!do_erase)
                goto out;

        data = g_new0 (CreateFilesystemData, 1);
        data->section = g_object_ref (section);
        data->presentable = g_object_ref (presentable);

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (section->priv->encrypt_check_button))) {
                data->encrypt_passphrase = gdu_util_dialog_ask_for_new_secret (
                        gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section))),
                        &data->save_in_keyring,
                        &data->save_in_keyring_session);
                if (data->encrypt_passphrase == NULL) {
                        create_filesystem_data_free (data);
                        data = NULL;
                        goto out;
                }
        }

        gdu_device_op_filesystem_create (device,
                                         fstype,
                                         fslabel,
                                         data->encrypt_passphrase,
                                         take_ownership,
                                         erase_action_completed,
                                         data);

out:
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
        g_free (primary);
        g_free (secondary);
        g_free (fstype);
        g_free (fslabel);
        g_free (drive_name);
}

static void
update (GduSectionUnrecognized *section)
{
        GduPresentable *presentable;
        GduDevice *device;
        GduPool *pool;

        presentable = gdu_section_get_presentable (GDU_SECTION (section));
        device = gdu_presentable_get_device (presentable);

        if (device == NULL) {
                g_warning ("%s: device is NULL for presentable",  __FUNCTION__);
                goto out;
        }

        pool = gdu_shell_get_pool (gdu_section_get_shell (GDU_SECTION (section)));

        g_object_set (section->priv->erase_action,
                      "polkit-action",
                      gdu_device_is_system_internal (device) ?
                        section->priv->pk_change_system_internal_action :
                        section->priv->pk_change_action,
                      NULL);

        if (!section->priv->init_done) {
                section->priv->init_done = TRUE;

                gtk_entry_set_text (GTK_ENTRY (section->priv->label_entry), "");
                gtk_combo_box_set_active (GTK_COMBO_BOX (section->priv->type_combo_box), 0);
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (section->priv->encrypt_check_button), FALSE);

                gdu_util_fstype_combo_box_rebuild (section->priv->type_combo_box, pool, NULL);

                if (!gdu_pool_supports_luks_devices (pool)) {
                        gtk_widget_hide (section->priv->encrypt_check_button);
                }

                /* initial probe to get things right */
                section_volume_unrecognized_type_combo_box_changed (section->priv->type_combo_box, section);
        }

        gtk_widget_set_sensitive (GTK_WIDGET (section), !gdu_device_is_read_only (device));

out:
        if (device != NULL)
                g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_unrecognized_finalize (GduSectionUnrecognized *section)
{
        polkit_action_unref (section->priv->pk_change_action);
        polkit_action_unref (section->priv->pk_change_system_internal_action);
        g_object_unref (section->priv->erase_action);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_unrecognized_class_init (GduSectionUnrecognizedClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_unrecognized_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionUnrecognizedPrivate));
}

static void
gdu_section_unrecognized_init (GduSectionUnrecognized *section)
{
        int row;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *vbox;
        GtkWidget *table;
        GtkWidget *combo_box;
        GtkWidget *entry;
        GtkWidget *button;
        GtkWidget *button_box;
        GtkWidget *check_button;
        char *s;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_UNRECOGNIZED, GduSectionUnrecognizedPrivate);

        section->priv->pk_change_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_change_action,
                                     "org.freedesktop.devicekit.disks.change");
        section->priv->pk_change_system_internal_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_change_system_internal_action,
                                     "org.freedesktop.devicekit.disks.change-system-internal");

        section->priv->erase_action = polkit_gnome_action_new_default ("create",
                                                                    section->priv->pk_change_action,
                                                                    _("_Create"),
                                                                    _("Create"));
        g_object_set (section->priv->erase_action,
                      "auth-label", _("_Create..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (section->priv->erase_action, "activate", G_CALLBACK (erase_action_callback), section);

        // TODO:
        //gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->erase_action));


        /* volume format + label */
        label = gtk_label_new (NULL);
        s = g_strconcat ("<b>", _("Create File System"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 6);
        vbox = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To create a new file system on the device, select the type "
                                                   "and label and then press \"Create\". All existing data will "
                                                   "be lost."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

        table = gtk_table_new (2, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);

        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        row = 0;

        /* file system label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        section->priv->label_entry = entry;

        row++;

        /* type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_fstype_combo_box_create (NULL, NULL);
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        section->priv->type_combo_box = combo_box;

        row++;

        /* type desc */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gdu_util_fstype_combo_box_set_desc_label (combo_box, label);

        row++;

        /* whether to chown fs root for user */
        check_button = gtk_check_button_new_with_mnemonic (_("T_ake ownership of file system"));
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_button), TRUE);
        gtk_widget_set_tooltip_text (check_button,
                                     _("The selected file system has a concept of file ownership. "
                                       "If checked, the created file system be will be owned by you. "
                                       "If not checked, only the super user can access the file system."));
        gtk_table_attach (GTK_TABLE (table), check_button, 0, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        section->priv->take_ownership_of_fs_check_button = check_button;

        row++;

        /* whether to encrypt underlying device */
        check_button = gtk_check_button_new_with_mnemonic (_("Encr_ypt underlying device"));
        gtk_widget_set_tooltip_text (check_button,
                                     _("Encryption protects your data, requiring a "
                                       "passphrase to be enterered before the file system can be "
                                       "used. May decrease performance and may not be compatible if "
                                       "you use the media on other operating systems."));
        gtk_table_attach (GTK_TABLE (table), check_button, 0, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        section->priv->encrypt_check_button = check_button;

        row++;

        /* update sensivity and length of fs label + entry */
        g_signal_connect (section->priv->type_combo_box, "changed",
                          G_CALLBACK (section_volume_unrecognized_type_combo_box_changed), section);

        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);

        button = polkit_gnome_action_create_button (section->priv->erase_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, TRUE, TRUE, 0);
}

GtkWidget *
gdu_section_unrecognized_new (GduShell       *shell,
                                        GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_UNRECOGNIZED,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
