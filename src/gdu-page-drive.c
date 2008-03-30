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

#include "gdu-drive.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"

struct _GduPageDrivePrivate
{
        GduShell *shell;

        GtkWidget *notebook;

        GtkWidget *health_status_image;
        GtkWidget *health_status_label;
        GtkWidget *health_status_explanation_label;
        GtkWidget *health_power_on_hours_label;
        GtkWidget *health_temperature_label;
        GtkWidget *health_refresh_button;
        GtkWidget *health_selftest_button;

        GtkWidget *create_part_table_vbox;
        GtkWidget *create_part_table_type_combo_box;

        PolKitAction *pk_create_part_table_action;
        PolKitGnomeAction *create_part_table_action;

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
        gtk_label_set_markup (GTK_LABEL (label), _("Some hard disks supports S.M.A.R.T., a monitoring system for "
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

        /* -------- */
        /* No media */
        /* -------- */

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

        gtk_widget_set_sensitive (page->priv->health_refresh_button, FALSE);
        gtk_widget_set_sensitive (page->priv->health_selftest_button, FALSE);
        gtk_widget_hide (page->priv->health_status_image);
        gtk_widget_hide (page->priv->health_status_explanation_label);
}

static void
smart_data_set (GduPageDrive *page, gboolean passed, int power_on_hours, int temperature)
{
        char *s;

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

        s = g_strdup_printf (_("%d° Celcius"), temperature);
        gtk_label_set_text (GTK_LABEL (page->priv->health_temperature_label), s);
        g_free (s);
}

static void
retrieve_smart_data_cb (GduDevice  *device,
                        gboolean    passed,
                        int         power_on_hours,
                        int         temperature,
                        GError     *error,
                        gpointer    user_data)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (user_data);

        if (error != NULL) {
                smart_data_set_not_supported (page);
                g_error_free (error);
                goto out;
        }

        smart_data_set (page, passed, power_on_hours, temperature);

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

static gboolean
gdu_page_drive_update (GduPage *_page, GduPresentable *presentable, gboolean reset_page)
{
        GduPageDrive *page = GDU_PAGE_DRIVE (_page);
        GduDevice *device;
        int page_to_show;

        device = gdu_presentable_get_device (presentable);
        if (device == NULL)
                goto out;

        if (gdu_device_is_media_available (device))
                page_to_show = 0;
        else
                page_to_show = 1;

        gtk_notebook_set_current_page (GTK_NOTEBOOK (page->priv->notebook), page_to_show);

        gtk_widget_set_sensitive (page->priv->create_part_table_vbox, !gdu_device_is_read_only (device));

        if (reset_page) {
                if (page_to_show == 0) {
                        int age;
                        if (gdu_device_smart_data_is_cached (device, &age) && age < 5 * 60) {
                                gdu_device_retrieve_smart_data_from_cache (device, retrieve_smart_data_cb, page);
                        } else {
                                smart_data_set_pending (page);
                                gdu_device_retrieve_smart_data (device, retrieve_smart_data_cb, page);
                        }
                }

                gtk_combo_box_set_active (GTK_COMBO_BOX (page->priv->create_part_table_type_combo_box), 0);
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
