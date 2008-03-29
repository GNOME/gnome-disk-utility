/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-volume-unrecognized.c
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
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-page.h"
#include "gdu-page-volume-unrecognized.h"
#include "gdu-util.h"

#include "gdu-drive.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"

struct _GduPageVolumeUnrecognizedPrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;

        GtkWidget *label_entry;
        GtkWidget *type_combo_box;
        GtkWidget *encrypt_check_button;

        PolKitAction *pk_erase_action;
        PolKitGnomeAction *erase_action;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_volume_unrecognized_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPageVolumeUnrecognized, gdu_page_volume_unrecognized, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_volume_unrecognized_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static char *
gdu_page_volume_unrecognized_get_fstype (GduPageVolumeUnrecognized *page)
{
        return gdu_util_fstype_combo_box_get_selected (page->priv->type_combo_box);
}

static char *
gdu_page_volume_unrecognized_get_fslabel (GduPageVolumeUnrecognized *page)
{
        char *ret;

        if (GTK_WIDGET_IS_SENSITIVE (page->priv->label_entry))
                ret = g_strdup (gtk_entry_get_text (GTK_ENTRY (page->priv->label_entry)));
        else
                ret = NULL;

        return ret;
}

static void
gdu_page_volume_unrecognized_finalize (GduPageVolumeUnrecognized *page)
{
        polkit_action_unref (page->priv->pk_erase_action);
        g_object_unref (page->priv->erase_action);

        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_volume_unrecognized_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
        GduPageVolumeUnrecognized *page = GDU_PAGE_VOLUME_UNRECOGNIZED (object);

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
gdu_page_volume_unrecognized_get_property (GObject     *object,
                                           guint        prop_id,
                                           GValue      *value,
                                           GParamSpec  *pspec)
{
        GduPageVolumeUnrecognized *page = GDU_PAGE_VOLUME_UNRECOGNIZED (object);

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
gdu_page_volume_unrecognized_class_init (GduPageVolumeUnrecognizedClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_volume_unrecognized_finalize;
        obj_class->set_property = gdu_page_volume_unrecognized_set_property;
        obj_class->get_property = gdu_page_volume_unrecognized_get_property;

        /**
         * GduPageVolumeUnrecognized:shell:
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
page_volume_unrecognized_type_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduPageVolumeUnrecognized *page = GDU_PAGE_VOLUME_UNRECOGNIZED (user_data);
        char *fstype;
        GduCreatableFilesystem *creatable_fs;
        gboolean label_entry_sensitive;
        gboolean can_erase;
        int max_label_len;

        label_entry_sensitive = FALSE;
        can_erase = FALSE;
        max_label_len = 0;

        fstype = gdu_util_fstype_combo_box_get_selected (combo_box);
        if (fstype != NULL) {
                creatable_fs = gdu_util_find_creatable_filesystem_for_fstype (fstype);
                if (creatable_fs != NULL) {
                        max_label_len = creatable_fs->max_label_len;
                }
                can_erase = TRUE;
        }

        if (max_label_len > 0)
                label_entry_sensitive = TRUE;

        gtk_entry_set_max_length (GTK_ENTRY (page->priv->label_entry), max_label_len);
        gtk_widget_set_sensitive (page->priv->label_entry, label_entry_sensitive);
        polkit_gnome_action_set_sensitive (page->priv->erase_action, can_erase);

        g_free (fstype);
}

typedef struct {
        GduPageVolumeUnrecognized *page;
        GduPresentable *presentable;
        char *encrypt_passphrase;
        gboolean save_in_keyring;
        gboolean save_in_keyring_session;
} CreateFilesystemData;

static void
create_filesystem_data_free (CreateFilesystemData *data)
{
        if (data->page != NULL)
                g_object_unref (data->page);
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
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        } else if (data != NULL) {
                /* now set the passphrase if requested */
                if (data->save_in_keyring || data->save_in_keyring_session) {
                        gdu_util_save_secret (device,
                                              data->encrypt_passphrase,
                                              data->save_in_keyring_session);
                        /* make sure the tab for the encrypted device is updated (it displays whether
                         * the passphrase is in the keyring or now)
                         */
                        gdu_shell_update (data->page->priv->shell);
                }
                create_filesystem_data_free (data);
        }
}

static void
erase_action_callback (GtkAction *action, gpointer user_data)
{
        GduPageVolumeUnrecognized *page = GDU_PAGE_VOLUME_UNRECOGNIZED (user_data);
        char *fslabel;
        char *fstype;
        GduDevice *device;
        CreateFilesystemData *data;
        GduPresentable *presentable;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        char *secure_erase;
        char *primary;
        char *secondary;
        char *drive_name;

        data = NULL;
        fstype = NULL;
        fslabel = NULL;
        secure_erase = NULL;
        primary = NULL;
        secondary = NULL;
        device = NULL;
        toplevel_presentable = NULL;
        toplevel_device = NULL;
        drive_name = NULL;

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL",  __FUNCTION__);
                goto out;
        }

        toplevel_presentable = gdu_util_find_toplevel_presentable (presentable);
        if (toplevel_presentable == NULL) {
                g_warning ("%s: no toplevel presentable",  __FUNCTION__);
        }
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }

        drive_name = gdu_presentable_get_name (toplevel_presentable);

        fstype = gdu_page_volume_unrecognized_get_fstype (page);
        fslabel = gdu_page_volume_unrecognized_get_fslabel (page);
        if (fslabel == NULL)
                fslabel = g_strdup ("");

        primary = g_strdup (_("<b><big>Are you sure you want to create a new file system, deleting existing data?</big></b>"));

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

        secure_erase = gdu_util_delete_confirmation_dialog (gdu_shell_get_toplevel (page->priv->shell),
                                                            "",
                                                            primary,
                                                            secondary,
                                                            _("C_reate"));

        if (secure_erase == NULL)
                goto out;

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->priv->encrypt_check_button))) {
                data = g_new (CreateFilesystemData, 1);
                data->page = g_object_ref (page);
                data->presentable = g_object_ref (presentable);

                data->encrypt_passphrase = gdu_util_dialog_ask_for_new_secret (
                        gdu_shell_get_toplevel (page->priv->shell),
                        &data->save_in_keyring,
                        &data->save_in_keyring_session);
                if (data->encrypt_passphrase == NULL) {
                        create_filesystem_data_free (data);
                        data = NULL;
                        goto out;
                }
        }

        gdu_device_op_mkfs (device,
                            fstype,
                            fslabel,
                            secure_erase,
                            data != NULL ? data->encrypt_passphrase : NULL,
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
        g_free (secure_erase);
        g_free (drive_name);
}

static void
gdu_page_volume_unrecognized_init (GduPageVolumeUnrecognized *page)
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

        page->priv = g_new0 (GduPageVolumeUnrecognizedPrivate, 1);

        page->priv->pk_erase_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_erase_action, "org.freedesktop.devicekit.disks.erase");

        page->priv->erase_action = polkit_gnome_action_new_default ("create",
                                                                    page->priv->pk_erase_action,
                                                                    _("C_reate"),
                                                                    _("Create"));
        g_object_set (page->priv->erase_action,
                      "auth-label", _("C_reate..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (page->priv->erase_action, "activate", G_CALLBACK (erase_action_callback), page);

        // TODO:
        //gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->erase_action));


        page->priv->main_vbox = gtk_vbox_new (FALSE, 5);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        /* volume format + label */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Create File System</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), label, FALSE, FALSE, 0);
        vbox = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To create a new file system on the device, select the type "
                                                   "and label and then press \"Create\". All existing data will "
                                                   "be lost."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

        table = gtk_table_new (2, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        row = 0;

        /* file system label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        page->priv->label_entry = entry;

        row++;

        /* type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_fstype_combo_box_create (NULL);
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->type_combo_box = combo_box;

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

        /* whether to encrypt underlying device */
        check_button = gtk_check_button_new_with_mnemonic (_("E_ncrypt underlying device"));
        gtk_widget_set_tooltip_text (check_button,
                                     _("Encryption protects your data, requiring a "
                                       "passphrase to be enterered before the file system can be "
                                       "used. May decrease performance and may not be compatible if "
                                       "you use the media on other operating systems."));
        if (gdu_util_can_create_encrypted_device ()) {
                gtk_table_attach (GTK_TABLE (table), check_button, 1, 2, row, row + 1,
                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        }
        page->priv->encrypt_check_button = check_button;

        row++;

        /* update sensivity and length of fs label + entry */
        g_signal_connect (page->priv->type_combo_box, "changed",
                          G_CALLBACK (page_volume_unrecognized_type_combo_box_changed), page);


        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);

        button = polkit_gnome_action_create_button (page->priv->erase_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, TRUE, TRUE, 0);
}


GduPageVolumeUnrecognized *
gdu_page_volume_unrecognized_new (GduShell *shell)
{
        return GDU_PAGE_VOLUME_UNRECOGNIZED (g_object_new (GDU_TYPE_PAGE_VOLUME_UNRECOGNIZED, "shell", shell, NULL));
}

static gboolean
gdu_page_volume_unrecognized_update (GduPage *_page, GduPresentable *presentable, gboolean reset_page)
{
        GduPageVolumeUnrecognized *page = GDU_PAGE_VOLUME_UNRECOGNIZED (_page);
        gboolean ret;
        GduDevice *device;

        ret = FALSE;

        device = gdu_presentable_get_device (presentable);
        if (device == NULL)
                goto out;

        if (reset_page) {
                gtk_entry_set_text (GTK_ENTRY (page->priv->label_entry), "");
                gtk_combo_box_set_active (GTK_COMBO_BOX (page->priv->type_combo_box), 0);
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->priv->encrypt_check_button), FALSE);
        }

        gtk_widget_set_sensitive (page->priv->main_vbox, !gdu_device_is_read_only (device));

        ret = TRUE;
out:
        if (device != NULL)
                g_object_unref (device);

        return ret;
}

static GtkWidget *
gdu_page_volume_unrecognized_get_widget (GduPage *_page)
{
        GduPageVolumeUnrecognized *page = GDU_PAGE_VOLUME_UNRECOGNIZED (_page);
        return page->priv->main_vbox;
}

static void
gdu_page_volume_unrecognized_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_volume_unrecognized_get_widget;
        iface->update = gdu_page_volume_unrecognized_update;
}

