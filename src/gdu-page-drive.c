/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-drive.c
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
#include "gdu-page-drive.h"
#include "gdu-util.h"
#include "gdu-tree.h"

#include "gdu-drive.h"
#include "gdu-activatable-drive.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"

struct _GduPageDrivePrivate
{
        GduShell *shell;

        GtkWidget *notebook;

        GtkWidget *health_status_image;
        GtkWidget *health_status_label;
        GtkWidget *health_status_explanation_label;
        GtkWidget *health_last_self_test_result_label;
        GtkWidget *health_power_on_hours_label;
        GtkWidget *health_temperature_label;
        GtkWidget *health_refresh_button;
        GtkWidget *health_selftest_button;

        GtkWidget *create_part_table_vbox;
        GtkWidget *create_part_table_type_combo_box;

        GtkWidget *linux_md_name_label;
        GtkWidget *linux_md_type_label;
        GtkWidget *linux_md_size_label;
        GtkWidget *linux_md_components_label;
        GtkWidget *linux_md_state_label;
        GtkWidget *linux_md_tree_view;
        GtkTreeStore *linux_md_tree_store;
        GtkWidget *linux_md_add_to_array_button;
        GtkWidget *linux_md_remove_from_array_button;
        GtkWidget *linux_md_add_new_to_array_button;

        PolKitAction *pk_create_part_table_action;
        PolKitGnomeAction *create_part_table_action;
};

enum {
        MD_LINUX_ICON_COLUMN,
        MD_LINUX_NAME_COLUMN,
        MD_LINUX_STATE_STRING_COLUMN,
        MD_LINUX_STATE_COLUMN,
        MD_LINUX_OBJPATH_COLUMN,
        MD_LINUX_N_COLUMNS,
};

static GObjectClass *parent_class = NULL;

static void gdu_page_drive_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPageDrive, gdu_page_drive, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_drive_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void health_refresh_button_clicked (GtkWidget *button, gpointer user_data);
static void health_selftest_button_clicked (GtkWidget *button, gpointer user_data);
static void add_to_array_button_clicked (GtkWidget *button, gpointer user_data);
static void add_new_to_array_button_clicked (GtkWidget *button, gpointer user_data);
static void remove_from_array_button_clicked (GtkWidget *button, gpointer user_data);

static void linux_md_tree_changed (GtkTreeSelection *selection, gpointer user_data);

static void
gdu_page_drive_finalize (GduPageDrive *page)
{
        polkit_action_unref (page->priv->pk_create_part_table_action);
        g_object_unref (page->priv->create_part_table_action);

        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_drive_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (object);

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
gdu_page_drive_get_property (GObject     *object,
                             guint        prop_id,
                             GValue      *value,
                             GParamSpec  *pspec)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (object);

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
gdu_page_drive_class_init (GduPageDriveClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_drive_finalize;
        obj_class->set_property = gdu_page_drive_set_property;
        obj_class->get_property = gdu_page_drive_get_property;

        /**
         * GduPageDrive:shell:
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
create_part_table_callback (GtkAction *action, gpointer user_data)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);
        GduDevice *device;
        char *secure_erase;
        char *scheme;
        char *primary;
        char *secondary;
        char *drive_name;

        scheme = NULL;
        secure_erase = NULL;
        primary = NULL;
        secondary = NULL;
        drive_name = NULL;

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        drive_name = gdu_presentable_get_name (gdu_shell_get_selected_presentable (page->priv->shell));

        primary = g_strdup (_("<b><big>Are you sure you want to format the disk, deleting existing data?</big></b>"));

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

        secure_erase = gdu_util_delete_confirmation_dialog (gdu_shell_get_toplevel (page->priv->shell),
                                                            "",
                                                            primary,
                                                            secondary,
                                                            _("C_reate"));


        if (secure_erase == NULL)
                goto out;

        scheme = gdu_util_part_table_type_combo_box_get_selected (
                page->priv->create_part_table_type_combo_box);

        gdu_device_op_create_partition_table (device, scheme, secure_erase);

out:
        if (device != NULL)
                g_object_unref (device);
        g_free (scheme);
        g_free (secure_erase);
        g_free (primary);
        g_free (secondary);
        g_free (drive_name);
}

static void
gdu_page_drive_init (GduPageDrive *page)
{
        int row;
        GtkWidget *hbox;
        GtkWidget *vbox2;
        GtkWidget *vbox3;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *combo_box;
        GtkWidget *button;
        GtkWidget *button_box;
        GtkWidget *image;
        GtkWidget *scrolled_window;
        GtkWidget *tree_view;
        GtkTreeSelection *selection;

        page->priv = g_new0 (GduPageDrivePrivate, 1);

        page->priv->pk_create_part_table_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_create_part_table_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->create_part_table_action = polkit_gnome_action_new_default (
                "create-part-table",
                page->priv->pk_create_part_table_action,
                _("C_reate"),
                _("Create"));
        g_object_set (page->priv->create_part_table_action,
                      "auth-label", _("C_reate..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (page->priv->create_part_table_action, "activate",
                          G_CALLBACK (create_part_table_callback), page);

        page->priv->notebook = gtk_notebook_new ();
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->notebook), 8);
        gtk_notebook_set_show_tabs (GTK_NOTEBOOK (page->priv->notebook), FALSE);
        gtk_notebook_set_show_border (GTK_NOTEBOOK (page->priv->notebook), FALSE);

        /* -------------------------- */
        /* -------------------------- */
        /* Normal drive page (page 0) */
        /* -------------------------- */
        /* -------------------------- */

        vbox3 = gtk_vbox_new (FALSE, 5);
        gtk_notebook_append_page (GTK_NOTEBOOK (page->priv->notebook), vbox3, NULL);
        page->priv->create_part_table_vbox = vbox3;

        /* ------ */
        /* Health */
        /* ------ */

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Health</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("Some disks supports S.M.A.R.T., a monitoring system for "
                                                   "disks to detect and report on various indicators of "
                                                   "reliability, in the hope of anticipating failures."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;


        /* power on hours */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Powered On:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        page->priv->health_power_on_hours_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* temperature */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Temperature:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        page->priv->health_temperature_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* temperature */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Last Test:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        page->priv->health_last_self_test_result_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* assessment */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Assessment:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        hbox = gtk_hbox_new (FALSE, 5);
        image = gtk_image_new ();
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        label = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
        gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        page->priv->health_status_image = image;
        page->priv->health_status_label = label;

        row++;

        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        page->priv->health_status_explanation_label = label;

        /* health buttons */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);

        button = gtk_button_new_with_mnemonic (_("Refre_sh"));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked", G_CALLBACK (health_refresh_button_clicked), page);
        page->priv->health_refresh_button = button;

        button = gtk_button_new_with_mnemonic (_("Se_lftest..."));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked", G_CALLBACK (health_selftest_button_clicked), page);
        page->priv->health_selftest_button = button;

        /* ------------ */
        /* Format Drive */
        /* ------------ */

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Format Media</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To format the media in the drive, select the formatting type "
                                                   "and then press \"Create\". All existing data will be lost."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* partition table type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_part_table_type_combo_box_create ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->create_part_table_type_combo_box = combo_box;

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
        button = polkit_gnome_action_create_button (page->priv->create_part_table_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* ---------------------- */
        /* ---------------------- */
        /* No media page (page 1) */
        /* ---------------------- */
        /* ---------------------- */

        vbox3 = gtk_vbox_new (FALSE, 5);
        gtk_notebook_append_page (GTK_NOTEBOOK (page->priv->notebook), vbox3, NULL);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>No Media Detected</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To format or edit media, insert it into the drive and wait "
                                                   "a few seconds."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        /* media detect button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = gtk_button_new_with_mnemonic (_("_Detect Media"));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* TODO: hook up the "Detect Media" button */

        /* ----------------------- */
        /* ----------------------- */
        /* Linux MD Drive (page 2) */
        /* ----------------------- */
        /* ----------------------- */

        vbox3 = gtk_vbox_new (FALSE, 5);
        gtk_notebook_append_page (GTK_NOTEBOOK (page->priv->notebook), vbox3, NULL);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox3), table, FALSE, FALSE, 0);

        row = 0;

        /* name */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Array Name:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        page->priv->linux_md_name_label = label;

        row++;

        /* size */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Array Size:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        page->priv->linux_md_size_label = label;

        row++;

        /* type (level) */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>RAID Type:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        page->priv->linux_md_type_label = label;

        row++;

        /* components */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Components:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        page->priv->linux_md_components_label = label;

        row++;

        /* components */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>State:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        page->priv->linux_md_state_label = label;

        row++;

        tree_view = gtk_tree_view_new ();
        page->priv->linux_md_tree_view = tree_view;
        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
        gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);
        gtk_box_pack_start (GTK_BOX (vbox3), scrolled_window, TRUE, TRUE, 0);

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
        g_signal_connect (selection, "changed", (GCallback) linux_md_tree_changed, page);

        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_set_homogeneous (GTK_BOX (button_box), FALSE);
        gtk_box_pack_start (GTK_BOX (vbox3), button_box, FALSE, FALSE, 0);

        button = gtk_button_new_with_mnemonic (_("A_ttach"));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        page->priv->linux_md_add_to_array_button = button;
        g_signal_connect (button, "clicked", G_CALLBACK (add_to_array_button_clicked), page);
        gtk_widget_set_tooltip_text (button, _("Attaches the stale component to the RAID array. "
                                               "After attachment, data from the array will be "
                                               "synchronized on the component."));

        button = gtk_button_new_with_mnemonic (_("_Detach"));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_REMOVE, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        page->priv->linux_md_remove_from_array_button = button;
        g_signal_connect (button, "clicked", G_CALLBACK (remove_from_array_button_clicked), page);
        gtk_widget_set_tooltip_text (button, _("Detaches the running component from the RAID array. Data on "
                                               "the component will be erased and the volume will be ready "
                                               "for other use."));

        button = gtk_button_new_with_mnemonic (_("_Add..."));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_NEW, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        page->priv->linux_md_add_new_to_array_button = button;
        g_signal_connect (button, "clicked", G_CALLBACK (add_new_to_array_button_clicked), page);
        gtk_widget_set_tooltip_text (button, _("Adds a new component to the running RAID array. Use this "
                                               "when replacing a failed component or adding a hot spare."));

        /* add renderers for tree view */
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, _("RAID Component"));
        renderer = gtk_cell_renderer_pixbuf_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", MD_LINUX_ICON_COLUMN,
                                             NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", MD_LINUX_NAME_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, _("State"));
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", MD_LINUX_STATE_STRING_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree_view), TRUE);

}


GduPageDrive *
gdu_page_drive_new (GduShell *shell)
{
        return GDU_PAGE_DRIVE (g_object_new (GDU_TYPE_PAGE_DRIVE, "shell", shell, NULL));
}

static void
smart_data_set_pending (GduPageDrive *page)
{
        gtk_image_clear (GTK_IMAGE (page->priv->health_status_image));
        gtk_label_set_markup (GTK_LABEL (page->priv->health_status_label), _("<i>Retrieving...</i>"));
        gtk_label_set_text (GTK_LABEL (page->priv->health_power_on_hours_label), _("-"));
        gtk_label_set_text (GTK_LABEL (page->priv->health_temperature_label), _("-"));
        gtk_label_set_markup (GTK_LABEL (page->priv->health_last_self_test_result_label), _("-"));

        gtk_widget_set_sensitive (page->priv->health_refresh_button, FALSE);
        gtk_widget_set_sensitive (page->priv->health_selftest_button, FALSE);
        gtk_widget_hide (page->priv->health_status_image);
        gtk_widget_hide (page->priv->health_status_explanation_label);
}

static void
smart_data_set_not_supported (GduPageDrive *page)
{
        gtk_image_clear (GTK_IMAGE (page->priv->health_status_image));
        gtk_label_set_markup (GTK_LABEL (page->priv->health_status_label), _("<i>Not Supported</i>"));
        gtk_label_set_text (GTK_LABEL (page->priv->health_power_on_hours_label), _("-"));
        gtk_label_set_text (GTK_LABEL (page->priv->health_temperature_label), _("-"));
        gtk_label_set_markup (GTK_LABEL (page->priv->health_last_self_test_result_label), _("-"));

        gtk_widget_set_sensitive (page->priv->health_refresh_button, FALSE);
        gtk_widget_set_sensitive (page->priv->health_selftest_button, FALSE);
        gtk_widget_hide (page->priv->health_status_image);
        gtk_widget_hide (page->priv->health_status_explanation_label);
}

static void
smart_data_set (GduPageDrive *page,
                gboolean passed,
                int power_on_hours,
                int temperature,
                const char *last_self_test_result)
{
        char *s;
        int fahrenheit;
        const char *last;

        gtk_widget_set_sensitive (page->priv->health_refresh_button, TRUE);
        gtk_widget_set_sensitive (page->priv->health_selftest_button, TRUE);
        gtk_widget_show (page->priv->health_status_image);

        if (passed) {
                gtk_image_set_from_stock (GTK_IMAGE (page->priv->health_status_image),
                                          GTK_STOCK_YES, GTK_ICON_SIZE_MENU);
                gtk_label_set_text (GTK_LABEL (page->priv->health_status_label), _("Passed"));
                gtk_widget_hide (page->priv->health_status_explanation_label);
        } else {
                gtk_image_set_from_stock (GTK_IMAGE (page->priv->health_status_image),
                                          GTK_STOCK_NO, GTK_ICON_SIZE_MENU);
                gtk_label_set_markup (GTK_LABEL (page->priv->health_status_label), _("<span foreground='red'><b>FAILING</b></span>"));
                gtk_label_set_markup (GTK_LABEL (page->priv->health_status_explanation_label),
                                      _("<small><i><b>"
                                        "Drive failure expected in less than 24 hours. "
                                        "Save all data immediately."
                                        "</b></i></small>"));
        }

        if (power_on_hours < 24)
                s = g_strdup_printf (_("%d hours"), power_on_hours);
        else {
                int d;
                int h;

                d = power_on_hours / 24;
                h = power_on_hours - d * 24;

                if (d == 0)
                        s = g_strdup_printf (_("%d days"), d);
                else if (d == 1)
                        s = g_strdup_printf (_("%d days, 1 hour"), d);
                else
                        s = g_strdup_printf (_("%d days, %d hours"), d, h);
        }
        gtk_label_set_text (GTK_LABEL (page->priv->health_power_on_hours_label), s);
        g_free (s);

        fahrenheit = 9 * temperature / 5 + 32;
        s = g_strdup_printf (_("%d° C / %d° F"), temperature, fahrenheit);
        gtk_label_set_text (GTK_LABEL (page->priv->health_temperature_label), s);
        g_free (s);

        if (strcmp (last_self_test_result, "unknown") == 0) {
                last = _("Unknown");
        } else if (strcmp (last_self_test_result, "completed_ok") == 0) {
                last = _("Completed OK");
        } else if (strcmp (last_self_test_result, "not_completed_aborted") == 0) {
                last = _("Cancelled");
        } else if (strcmp (last_self_test_result, "not_completed_aborted_reset") == 0) {
                last = _("Cancelled (with hard or soft reset)");
        } else if (strcmp (last_self_test_result, "not_completed_unknown_reason") == 0) {
                last = _("Not completed (a fatal error might have occured)");
        } else if (strcmp (last_self_test_result, "completed_failed_electrical") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (electrical test)");
        } else if (strcmp (last_self_test_result, "completed_failed_servo") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (servo/seek test)");
        } else if (strcmp (last_self_test_result, "completed_failed_read") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (read test)");
        } else if (strcmp (last_self_test_result, "completed_failed_damage") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (device is suspected of having handled damage");
        }
        gtk_label_set_markup (GTK_LABEL (page->priv->health_last_self_test_result_label), last);
}

static void
retrieve_smart_data_cb (GduDevice  *device,
                        gboolean    passed,
                        int         power_on_hours,
                        int         temperature,
                        const char *last_self_test_result,
                        GError     *error,
                        gpointer    user_data)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);

        if (error != NULL) {
                smart_data_set_not_supported (page);
                g_error_free (error);
                goto out;
        }

        smart_data_set (page, passed, power_on_hours, temperature, last_self_test_result);

out:
        ;
}

static void
health_refresh_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        smart_data_set_pending (page);
        gdu_device_retrieve_smart_data (device, retrieve_smart_data_cb, page);

out:
        if (device != NULL)
                g_object_unref (device);
}

static void
health_selftest_button_clicked (GtkWidget *button, gpointer user_data)
{
        int response;
        GtkWidget *dialog;
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);
        GduDevice *device;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *main_vbox;
        GtkWidget *label;
        GtkWidget *radio0;
        GtkWidget *radio1;
        const char *test;

        test = NULL;

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }


        dialog = gtk_dialog_new_with_buttons (_("S.M.A.R.T. Selftest"),
                                              GTK_WINDOW (gdu_shell_get_toplevel (page->priv->shell)),
                                              GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
                                              NULL);

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	main_vbox = gtk_vbox_new (FALSE, 10);
	gtk_box_pack_start (GTK_BOX (hbox), main_vbox, TRUE, TRUE, 0);

	label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<big><b>Select what S.M.A.R.T. test to run on the drive.</b></big>"));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

	label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The tests may take a very long time to complete depending "
                                                   "on the speed and size of the disk. You can continue using "
                                                   "your system while the test is running."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

        radio0 = gtk_radio_button_new_with_mnemonic_from_widget (NULL,
                                                                 _("_Short (usually less than ten minutes)"));
        radio1 = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (radio0),
                                                                 _("_Long (usually tens of minutes)"));

	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (radio0), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (radio1), FALSE, FALSE, 0);

        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Initiate Selftest"), 0);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), 0);

        gtk_widget_show_all (dialog);
        response = gtk_dialog_run (GTK_DIALOG (dialog));

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio0))) {
                test = "short";
        } else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio1))) {
                test = "long";
        }

        gtk_widget_destroy (dialog);
        if (response != 0)
                goto out;

        gdu_device_smart_data_purge_cache (device);

        /* TODO: option for captive */
        gdu_device_op_run_smart_selftest (device, test, FALSE);
out:
        if (device != NULL)
                g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_new_to_array_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);
        GduPresentable *presentable;
        GduPresentable *selected_presentable;
        GduDevice *device;
        GduDevice *selected_device;
        GduActivatableDrive *activatable_drive;
        GduPool *pool;
        GtkWidget *dialog;
        GtkWidget *vbox;
        int response;
        GtkWidget *tree_view;
        char *array_name;
        char *s;

        device = NULL;
        selected_device = NULL;
        pool = NULL;
        array_name = NULL;
        selected_presentable = NULL;

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        if (!GDU_IS_ACTIVATABLE_DRIVE (presentable)) {
                g_warning ("%s: is not an activatable drive", __FUNCTION__);
                goto out;
        }

        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                g_warning ("%s: activatable drive not active", __FUNCTION__);
                goto out;
        }

        activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);

        pool = gdu_device_get_pool (device);

        dialog = gtk_dialog_new_with_buttons ("",
                                              GTK_WINDOW (gdu_shell_get_toplevel (page->priv->shell)),
                                              GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
                                              NULL);


	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);

        GtkWidget *hbox;

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);

        GtkWidget *image;

	image = gtk_image_new_from_pixbuf (gdu_util_get_pixbuf_for_presentable (presentable, GTK_ICON_SIZE_DIALOG));
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	vbox = gtk_vbox_new (FALSE, 10);
	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

        array_name = gdu_presentable_get_name (presentable);

        GtkWidget *label;
        label = gtk_label_new (NULL);
        s = g_strdup_printf ( _("<big><b>Select a volume to use as component in the array \"%s\"</b></big>\n\n"
                                "Only volumes of acceptable sizes can be selected. You may "
                                "need to manually create new volumes of acceptable sizes."),
                              array_name);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);


        GtkWidget *scrolled_window;
        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                             GTK_SHADOW_IN);
        tree_view = gdu_device_tree_new (pool);
        gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

        gtk_widget_grab_focus (gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL));
        gtk_dialog_add_button (GTK_DIALOG (dialog), _("Add _Volume"), 0);

        gtk_window_set_default_size (GTK_WINDOW (dialog), -1, 350);

        gtk_widget_show_all (dialog);
        response = gtk_dialog_run (GTK_DIALOG (dialog));

        selected_presentable = gdu_device_tree_get_selected_presentable (GTK_TREE_VIEW (tree_view));
        if (selected_presentable != NULL)
                g_object_ref (selected_presentable);
        gtk_widget_destroy (dialog);

        if (response < 0)
                goto out;

        if (selected_presentable == NULL)
                goto out;

        selected_device = gdu_presentable_get_device (selected_presentable);
        if (selected_device == NULL)
                goto out;

        g_warning ("got it: %s", gdu_device_get_object_path (selected_device));

        /* got it! */
        gdu_device_op_add_component_to_linux_md_array (device, gdu_device_get_object_path (selected_device));


out:
        g_free (array_name);
        if (selected_presentable != NULL)
                g_object_unref (selected_presentable);
        if (selected_device != NULL)
                g_object_unref (selected_device);
        if (device != NULL)
                g_object_unref (device);
        if (pool != NULL)
                g_object_unref (pool);
}

static void
add_to_array_button_clicked (GtkWidget *button, gpointer user_data)
{
        GtkTreePath *path;
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);
        GduDevice *device;
        GduPresentable *presentable;
        GduActivatableDrive *activatable_drive;
        GduDevice *slave_device;
        GduPool *pool;
        GduActivableDriveSlaveState slave_state;
        char *component_objpath;

        device = NULL;
        slave_device = NULL;
        pool = NULL;
        component_objpath = NULL;

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        if (!GDU_IS_ACTIVATABLE_DRIVE (presentable)) {
                g_warning ("%s: is not an activatable drive", __FUNCTION__);
                goto out;
        }

        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                g_warning ("%s: activatable drive not active", __FUNCTION__);
                goto out;
        }

        activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);

        gtk_tree_view_get_cursor (GTK_TREE_VIEW (page->priv->linux_md_tree_view), &path, NULL);
        if (path != NULL) {
                GtkTreeIter iter;

                if (gtk_tree_model_get_iter (GTK_TREE_MODEL (page->priv->linux_md_tree_store), &iter, path)) {

                        gtk_tree_model_get (GTK_TREE_MODEL (page->priv->linux_md_tree_store), &iter,
                                            MD_LINUX_OBJPATH_COLUMN,
                                            &component_objpath,
                                            -1);
                }
                gtk_tree_path_free (path);
        }

        if (component_objpath == NULL) {
                g_warning ("%s: no component selected", __FUNCTION__);
                goto out;
        }

        pool = gdu_device_get_pool (device);

        slave_device = gdu_pool_get_by_object_path (pool, component_objpath);
        if (slave_device == NULL) {
                g_warning ("%s: no device for component objpath %s", __FUNCTION__, component_objpath);
                goto out;
        }

        slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, slave_device);
        if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_NOT_FRESH) {
                /* yay, add this to the array */
                gdu_device_op_add_component_to_linux_md_array (device, component_objpath);
        }


out:
        g_free (component_objpath);
        if (device != NULL)
                g_object_unref (device);
        if (pool != NULL)
                g_object_unref (pool);
        if (slave_device != NULL)
                g_object_unref (slave_device);
}

static void
remove_from_array_button_clicked (GtkWidget *button, gpointer user_data)
{
        GtkTreePath *path;
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);
        GduDevice *device;
        GduPresentable *presentable;
        GduActivatableDrive *activatable_drive;
        GduDevice *slave_device;
        GduPool *pool;
        GduActivableDriveSlaveState slave_state;
        char *component_objpath;
        GduPresentable *slave_presentable;

        device = NULL;
        slave_device = NULL;
        pool = NULL;
        component_objpath = NULL;
        slave_presentable = NULL;

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        if (!GDU_IS_ACTIVATABLE_DRIVE (presentable)) {
                g_warning ("%s: is not an activatable drive", __FUNCTION__);
                goto out;
        }

        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                g_warning ("%s: activatable drive not active", __FUNCTION__);
                goto out;
        }

        activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);

        gtk_tree_view_get_cursor (GTK_TREE_VIEW (page->priv->linux_md_tree_view), &path, NULL);
        if (path != NULL) {
                GtkTreeIter iter;

                if (gtk_tree_model_get_iter (GTK_TREE_MODEL (page->priv->linux_md_tree_store), &iter, path)) {

                        gtk_tree_model_get (GTK_TREE_MODEL (page->priv->linux_md_tree_store), &iter,
                                            MD_LINUX_OBJPATH_COLUMN,
                                            &component_objpath,
                                            -1);
                }
                gtk_tree_path_free (path);
        }

        if (component_objpath == NULL) {
                g_warning ("%s: no component selected", __FUNCTION__);
                goto out;
        }

        pool = gdu_device_get_pool (device);

        slave_device = gdu_pool_get_by_object_path (pool, component_objpath);
        if (slave_device == NULL) {
                g_warning ("%s: no device for component objpath %s", __FUNCTION__, component_objpath);
                goto out;
        }

        slave_presentable = gdu_pool_get_volume_by_device (pool, slave_device);
        if (slave_presentable == NULL) {
                g_warning ("%s: no volume for component objpath %s", __FUNCTION__, component_objpath);
                goto out;
        }

        slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, slave_device);
        if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING ||
            slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_SYNCING ||
            slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE) {
                char *primary;
                char *secondary;
                char *secure_erase;
                char *array_name;
                char *component_name;

                array_name = gdu_presentable_get_name (presentable);
                component_name = gdu_presentable_get_name (slave_presentable);

                /* confirmation dialog */
                primary = g_strdup (_("<b><big>Are you sure you want to remove the component from the array?</big></b>"));

                secondary = g_strdup_printf (_("The data on component \"%s\" of the RAID Array \"%s\" will be "
                                               "irrecovably erased and the RAID Array might be degraded. "
                                               "Make sure important data is backed up. "
                                               "This action cannot be undone."),
                                             component_name,
                                             array_name);

                secure_erase = gdu_util_delete_confirmation_dialog (gdu_shell_get_toplevel (page->priv->shell),
                                                                    "",
                                                                    primary,
                                                                    secondary,
                                                                    _("_Remove Component"));
                if (secure_erase != NULL) {
                        /* yay, remove this component from the array */
                        gdu_device_op_remove_component_from_linux_md_array (device, component_objpath, secure_erase);
                }

                g_free (primary);
                g_free (secondary);
                g_free (secure_erase);
                g_free (array_name);
                g_free (component_name);
        }


out:
        g_free (component_objpath);
        if (device != NULL)
                g_object_unref (device);
        if (pool != NULL)
                g_object_unref (pool);
        if (slave_device != NULL)
                g_object_unref (slave_device);
        if (slave_presentable != NULL)
                g_object_unref (slave_presentable);
}

static void
linux_md_buttons_update (GduPageDrive *page)
{
        GtkTreePath *path;
        char *component_objpath;
        gboolean show_add_to_array_button;
        gboolean show_add_new_to_array_button;
        gboolean show_remove_from_array_button;
        GduPresentable *presentable;
        GduActivatableDrive *activatable_drive;
        GduDevice *device;
        GduDevice *slave_device;
        GduPool *pool;
        GduActivableDriveSlaveState slave_state;

        component_objpath = NULL;
        device = NULL;
        slave_device = NULL;
        pool = NULL;
        show_add_to_array_button = FALSE;
        show_add_new_to_array_button = FALSE;
        show_remove_from_array_button = FALSE;

        gtk_tree_view_get_cursor (GTK_TREE_VIEW (page->priv->linux_md_tree_view), &path, NULL);
        if (path != NULL) {
                GtkTreeIter iter;

                if (gtk_tree_model_get_iter (GTK_TREE_MODEL (page->priv->linux_md_tree_store), &iter, path)) {

                        gtk_tree_model_get (GTK_TREE_MODEL (page->priv->linux_md_tree_store), &iter,
                                            MD_LINUX_OBJPATH_COLUMN,
                                            &component_objpath,
                                            -1);
                }
                gtk_tree_path_free (path);
        }

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        if (!GDU_IS_ACTIVATABLE_DRIVE (presentable)) {
                g_warning ("%s: is not an activatable drive", __FUNCTION__);
                goto out;
        }

        activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);

        /* can only add/remove components on a running drive */
        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                goto out;
        }

        /* can always add a new component when the drive is running */
        show_add_new_to_array_button = TRUE;

        pool = gdu_device_get_pool (device);

        if (component_objpath != NULL) {

                slave_device = gdu_pool_get_by_object_path (pool, component_objpath);
                if (slave_device == NULL) {
                        g_warning ("%s: no device for component objpath %s", __FUNCTION__, component_objpath);
                        goto out;
                }

                slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, slave_device);

                if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_NOT_FRESH) {
                        /* yay, we can add this to the array */
                        show_add_to_array_button = TRUE;
                }

                if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING ||
                    slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_SYNCING ||
                    slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE) {
                        /* yay, we can remove this to the array */
                        show_remove_from_array_button = TRUE;
                }
        }

out:
        gtk_widget_set_sensitive (page->priv->linux_md_add_to_array_button, show_add_to_array_button);
        gtk_widget_set_sensitive (page->priv->linux_md_add_new_to_array_button, show_add_new_to_array_button);
        gtk_widget_set_sensitive (page->priv->linux_md_remove_from_array_button, show_remove_from_array_button);

        g_free (component_objpath);
        if (device != NULL)
                g_object_unref (device);
        if (pool != NULL)
                g_object_unref (pool);
        if (slave_device != NULL)
                g_object_unref (slave_device);
}

static void
linux_md_tree_changed (GtkTreeSelection *selection, gpointer user_data)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);
        linux_md_buttons_update (page);
}

static const GtkTargetEntry dnd_targets[1] = {
        {"STRING", 0, 0},
};

static const int num_dnd_targets = 1;

static void
linux_md_section_update (GduPageDrive *page, gboolean reset_page)
{
        char *s;
        GduDevice *device;
        GduPresentable *presentable;
        GduActivatableDrive *activatable_drive;
        GList *l;
        GList *slaves;
        GduDevice *component;
        const char *uuid;
        const char *name;
        const char *level;
        int num_raid_devices;
        int num_slaves;
        char *level_str;
        guint64 component_size;
        guint64 raid_size;
        char *raid_size_str;
        char *components_str;
        char *state_str;

        slaves = NULL;
        device = NULL;
        level_str = NULL;
        raid_size_str = NULL;
        components_str = NULL;
        state_str = NULL;

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);
        device = gdu_presentable_get_device (presentable);

        slaves = gdu_activatable_drive_get_slaves (activatable_drive);
        num_slaves = g_list_length (slaves);
        if (num_slaves == 0) {
                /* this fine; happens when the last component is yanked
                 * since remove_slave() emits "changed".
                 */
                /*g_warning ("%s: no slaves for activatable drive", __FUNCTION__);*/
                goto out;
        }
        component = GDU_DEVICE (slaves->data);

        if (!gdu_device_is_linux_md_component (component)) {
                g_warning ("%s: slave of activatable drive is not a linux md component", __FUNCTION__);
                goto out;
        }

        uuid = gdu_device_linux_md_component_get_uuid (component);
        name = gdu_device_linux_md_component_get_name (component);
        if (name == NULL || strlen (name) == 0) {
                name = _("-");
        }
        level = gdu_device_linux_md_component_get_level (component);
        num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (component);
        component_size = gdu_device_get_size (component);

        /*g_warning ("activatable drive:\n"
                   "uuid=%s\n"
                   "name=%s\n"
                   "level=%s\n"
                   "num_raid_devices=%d\n"
                   "num_slaves=%d\n"
                   "device=%p",
                   uuid, name, level, num_raid_devices, num_slaves, device);*/

        raid_size = gdu_presentable_get_size (presentable);

        if (strcmp (level, "raid0") == 0) {
                level_str = g_strdup (_("Striped (RAID-0)"));
        } else if (strcmp (level, "raid1") == 0) {
                level_str = g_strdup (_("Mirrored (RAID-1)"));
        } else if (strcmp (level, "raid4") == 0) {
                level_str = g_strdup (_("RAID-4"));
        } else if (strcmp (level, "raid5") == 0) {
                level_str = g_strdup (_("RAID-5"));
        } else if (strcmp (level, "raid6") == 0) {
                level_str = g_strdup (_("RAID-6"));
        } else if (strcmp (level, "linear") == 0) {
                level_str = g_strdup (_("Linear (Just a Bunch Of Disks)"));
        } else {
                level_str = g_strdup (level);
        }

        s = gdu_util_get_size_for_display (component_size, FALSE);
        if (strcmp (level, "linear") == 0) {
                components_str = g_strdup_printf (_("%d Components"), num_raid_devices);
        } else {
                components_str = g_strdup_printf (_("%d Components (%s each)"), num_raid_devices, s);
        }
        g_free (s);

        if (raid_size == 0) {
                raid_size_str = g_strdup_printf (_("-"));
        } else {
                raid_size_str = gdu_util_get_size_for_display (raid_size, TRUE);
        }

        if (device == NULL) {
                if (gdu_activatable_drive_can_activate (activatable_drive)) {
                        state_str = g_strdup (_("Not running"));
                } else if (gdu_activatable_drive_can_activate_degraded (activatable_drive)) {
                        state_str = g_strdup (_("Not running, can only start degraded"));
                } else {
                        state_str = g_strdup (_("Not running, not enough components to start"));
                }
        } else {
                gboolean is_degraded;
                const char *sync_action;
                double sync_percentage;
                guint64 sync_speed;
                char *sync_speed_str;
                GString *str;

                is_degraded = gdu_device_linux_md_is_degraded (device);
                sync_action = gdu_device_linux_md_get_sync_action (device);
                sync_percentage = gdu_device_linux_md_get_sync_percentage (device);
                sync_speed = gdu_device_linux_md_get_sync_speed (device);

                str = g_string_new (NULL);
                if (is_degraded)
                        g_string_append (str, _("<span foreground='red'><b>Degraded</b></span>"));
                else
                        g_string_append (str, _("Running"));

                if (strcmp (sync_action, "idle") != 0) {
                        if (strcmp (sync_action, "reshape") == 0)
                                g_string_append (str, _(", Reshaping"));
                        else if (strcmp (sync_action, "resync") == 0)
                                g_string_append (str, _(", Resyncing"));
                        else if (strcmp (sync_action, "repair") == 0)
                                g_string_append (str, _(", Repairing"));
                        else if (strcmp (sync_action, "recover") == 0)
                                g_string_append (str, _(", Recovering"));

                        sync_speed_str = gdu_util_get_speed_for_display (sync_speed);
                        g_string_append_printf (str, _(" @ %3.01f%% (%s)"), sync_percentage, sync_speed_str);
                        g_free (sync_speed_str);
                }

                state_str = g_string_free (str, FALSE);
        }

        gtk_label_set_text (GTK_LABEL (page->priv->linux_md_name_label), name);
        gtk_label_set_text (GTK_LABEL (page->priv->linux_md_type_label), level_str);
        gtk_label_set_text (GTK_LABEL (page->priv->linux_md_size_label), raid_size_str);
        gtk_label_set_text (GTK_LABEL (page->priv->linux_md_components_label), components_str);
        gtk_label_set_markup (GTK_LABEL (page->priv->linux_md_state_label), state_str);

        /* only build a new model if rebuilding the page */
        //if (reset_page) {
        {
                GtkTreeStore *store;

                if (page->priv->linux_md_tree_store != NULL)
                        g_object_unref (page->priv->linux_md_tree_store);

                store = gtk_tree_store_new (MD_LINUX_N_COLUMNS,
                                            GDK_TYPE_PIXBUF,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_INT,
                                            G_TYPE_STRING);
                page->priv->linux_md_tree_store = store;

                gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                                      MD_LINUX_OBJPATH_COLUMN,
                                                      GTK_SORT_ASCENDING);

                /* add all slaves */
                for (l = slaves; l != NULL; l = l->next) {
                        GduDevice *sd = GDU_DEVICE (l->data);
                        GdkPixbuf *pixbuf;
                        char *name;
                        GtkTreeIter iter;
                        GduPool *pool;
                        GduPresentable *p;
                        GduActivableDriveSlaveState slave_state;
                        const char *slave_state_str;

                        pool = gdu_device_get_pool (sd);
                        p = gdu_pool_get_volume_by_device (pool, sd);
                        g_object_unref (pool);

                        if (p == NULL) {
                                g_warning ("Cannot find volume for device");
                                continue;
                        }

                        s = gdu_presentable_get_name (p);
                        name = g_strdup_printf ("%s (%s)", s, gdu_device_get_device_file (sd));
                        g_free (s);
                        pixbuf = gdu_util_get_pixbuf_for_presentable (p, GTK_ICON_SIZE_LARGE_TOOLBAR);

                        slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, sd);

                        switch (slave_state) {
                        case GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING:
                                slave_state_str = _("Running");
                                break;
                        case GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_SYNCING:
                                slave_state_str = _("Running, Syncing to array");
                                break;
                        case GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE:
                                slave_state_str = _("Running, Hot Spare");
                                break;
                        case GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY:
                                slave_state_str = _("Ready");
                                break;
                        case GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_NOT_FRESH:
                                if (device != NULL) {
                                        slave_state_str = _("Not Running, Stale");
                                } else {
                                        slave_state_str = _("Stale");
                                }
                                break;
                        default:
                                break;
                        }

                        gtk_tree_store_append (store, &iter, NULL);
                        gtk_tree_store_set (store,
                                            &iter,
                                            MD_LINUX_ICON_COLUMN, pixbuf,
                                            MD_LINUX_NAME_COLUMN, name,
                                            MD_LINUX_STATE_STRING_COLUMN, slave_state_str,
                                            MD_LINUX_STATE_COLUMN, slave_state,
                                            MD_LINUX_OBJPATH_COLUMN, gdu_device_get_object_path (sd),
                                            -1);

                        g_free (name);
                        if (pixbuf != NULL)
                                g_object_unref (pixbuf);

                        g_object_unref (p);
                }

                gtk_tree_view_set_model (GTK_TREE_VIEW (page->priv->linux_md_tree_view),
                                         GTK_TREE_MODEL (store));

                gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (page->priv->linux_md_tree_view),
                                                      dnd_targets,
                                                      num_dnd_targets,
                                                      GDK_ACTION_COPY);


        }

        linux_md_buttons_update (page);

out:
        g_free (state_str);
        g_free (level_str);
        g_free (raid_size_str);
        g_free (components_str);
        g_list_foreach (slaves, (GFunc) g_object_unref, NULL);
        g_list_free (slaves);
        if (device != NULL)
                g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
gdu_page_drive_update (GduPage *_page, GduPresentable *presentable, gboolean reset_page)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (_page);
        GduDevice *device;
        int page_to_show;

        page_to_show = -1;

        device = gdu_presentable_get_device (presentable);

        if (device != NULL) {
                if (gdu_device_is_media_available (device))
                        page_to_show = 0;
                else
                        page_to_show = 1;
        }

        if (GDU_IS_ACTIVATABLE_DRIVE (presentable)) {
                if (gdu_activatable_drive_get_kind (GDU_ACTIVATABLE_DRIVE (presentable)) ==
                    GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD)
                        page_to_show = 2;
        }

        if (page_to_show == -1) {
                g_warning ("%s: don't know what page to show", __FUNCTION__);
                goto out;
        }

        gtk_notebook_set_current_page (GTK_NOTEBOOK (page->priv->notebook), page_to_show);

        if (page_to_show == 0) {
                if (reset_page) {
                        int age;
                        if (gdu_device_smart_data_is_cached (device, &age) && age < 5 * 60) {
                                gdu_device_retrieve_smart_data_from_cache (device, retrieve_smart_data_cb, page);
                        } else {
                                smart_data_set_pending (page);
                                gdu_device_retrieve_smart_data (device, retrieve_smart_data_cb, page);
                        }

                        gtk_combo_box_set_active (GTK_COMBO_BOX (page->priv->create_part_table_type_combo_box), 0);
                }

                gtk_widget_set_sensitive (page->priv->create_part_table_vbox, !gdu_device_is_read_only (device));

        } else if (page_to_show == 2) {
                linux_md_section_update (page, reset_page);
        }

out:
        if (device != NULL) {
                g_object_unref (device);
        }
        return TRUE;
}

static GtkWidget *
gdu_page_drive_get_widget (GduPage *_page)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (_page);
        return page->priv->notebook;
}

static void
gdu_page_drive_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_drive_get_widget;
        iface->update = gdu_page_drive_update;
}
