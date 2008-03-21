/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-partition-table.c
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
#include "gdu-page-partition-table.h"

struct _GduPagePartitionTablePrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;
        GtkWidget *disk_widget;

        GtkWidget *create_part_table_vbox;
        GtkWidget *create_part_table_type_combo_box;
        GtkWidget *create_part_table_secure_erase_combo_box;

        PolKitAction *pk_create_part_table_action;
        PolKitGnomeAction *create_part_table_action;

        GduPresentable *presentable;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_partition_table_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPagePartitionTable, gdu_page_partition_table, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_partition_table_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void
gdu_page_partition_table_finalize (GduPagePartitionTable *page)
{
        polkit_action_unref (page->priv->pk_create_part_table_action);
        g_object_unref (page->priv->create_part_table_action);

        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);
        if (page->priv->presentable != NULL)
                g_object_unref (page->priv->presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_partition_table_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
        GduPagePartitionTable *page = GDU_PAGE_PARTITION_TABLE (object);

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
gdu_page_partition_table_get_property (GObject     *object,
                                    guint        prop_id,
                                    GValue      *value,
                                    GParamSpec  *pspec)
{
        GduPagePartitionTable *page = GDU_PAGE_PARTITION_TABLE (object);

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
gdu_page_partition_table_class_init (GduPagePartitionTableClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_partition_table_finalize;
        obj_class->set_property = gdu_page_partition_table_set_property;
        obj_class->get_property = gdu_page_partition_table_get_property;

        /**
         * GduPagePartitionTable:shell:
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
        GduPagePartitionTable *page = GDU_PAGE_PARTITION_TABLE (user_data);
        int response;
        GtkWidget *dialog;
        GduDevice *device;
        char *secure_erase;
        char *scheme;

        scheme = NULL;
        secure_erase = NULL;

        device = gdu_presentable_get_device (page->priv->presentable);
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        /* TODO: mention what drive the partition is on etc. */
        dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (gdu_shell_get_toplevel (page->priv->shell)),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_MESSAGE_WARNING,
                                                     GTK_BUTTONS_CANCEL,
                                                     _("<b><big>Are you sure you want to create a new partition "
                                                       "table on the device?</big></b>"));

        gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                                    _("All data on the device will be irrecovably erased. "
                                                      "Make sure data important to you is backed up. "
                                                      "This action cannot be undone."));
        /* ... until we add data recovery to g-d-u! */

        gtk_dialog_add_button (GTK_DIALOG (dialog), _("Create"), 0);

        response = gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        if (response != 0)
                goto out;

        scheme = gdu_util_part_table_type_combo_box_get_selected (
                page->priv->create_part_table_type_combo_box);

        secure_erase = gdu_util_secure_erase_combo_box_get_selected (
                page->priv->create_part_table_secure_erase_combo_box);

        gdu_device_op_create_partition_table (device, scheme, secure_erase);

out:
        g_free (scheme);
        g_free (secure_erase);
        if (device != NULL)
                g_object_unref (device);
}


static void
gdu_page_partition_table_init (GduPagePartitionTable *page)
{
        GtkWidget *hbox;
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *vbox3;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *combo_box;
        GtkWidget *button;
        GtkWidget *button_box;
        int row;

        page->priv = g_new0 (GduPagePartitionTablePrivate, 1);

        page->priv->pk_create_part_table_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_create_part_table_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->create_part_table_action = polkit_gnome_action_new_default (
                "create-part-table",
                page->priv->pk_create_part_table_action,
                _("_Create"),
                _("Create"));
        g_object_set (page->priv->create_part_table_action,
                      "auth-label", _("_Create..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (page->priv->create_part_table_action, "activate",
                          G_CALLBACK (create_part_table_callback), page);

        page->priv->main_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        hbox = gtk_hbox_new (FALSE, 10);

        page->priv->disk_widget = gdu_disk_widget_new (NULL);
        gtk_widget_set_size_request (page->priv->disk_widget, 150, 150);

        vbox = gtk_vbox_new (FALSE, 10);

        /* -------------------------- */
        /* Create new partition table */
        /* -------------------------- */

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox3, FALSE, TRUE, 0);
        page->priv->create_part_table_vbox = vbox3;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Create Partition Table</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To create a new partition table, select the type. Note that all "
                                                   "data on the disk will be lost."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* secure erase */
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

        /* secure erase */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Secure Erase:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_secure_erase_combo_box_create ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->create_part_table_secure_erase_combo_box = combo_box;

        row++;

        /* create button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = polkit_gnome_action_create_button (page->priv->create_part_table_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* ---------------- */

        gtk_box_pack_start (GTK_BOX (hbox), page->priv->disk_widget, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), hbox, TRUE, TRUE, 0);
}


GduPagePartitionTable *
gdu_page_partition_table_new (GduShell *shell)
{
        return GDU_PAGE_PARTITION_TABLE (g_object_new (GDU_TYPE_PAGE_PARTITION_TABLE, "shell", shell, NULL));
}

static gboolean
gdu_page_partition_table_update (GduPage *_page, GduPresentable *presentable)
{
        GduPagePartitionTable *page = GDU_PAGE_PARTITION_TABLE (_page);
        GduDevice *device;
        gboolean show_page;
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

        if (device != NULL &&
            gdu_device_is_media_available (device) &&
            (gdu_device_is_partition_table (device) || toplevel_presentable == presentable)) {
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

        gdu_util_part_table_type_combo_box_select (page->priv->create_part_table_type_combo_box, scheme);

        gtk_widget_show (page->priv->create_part_table_vbox);

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
gdu_page_partition_table_get_widget (GduPage *_page)
{
        GduPagePartitionTable *page = GDU_PAGE_PARTITION_TABLE (_page);
        return page->priv->main_vbox;
}

static char *
gdu_page_partition_table_get_name (GduPage *page)
{
        return g_strdup (_("Partition Table"));
}

static void
gdu_page_partition_table_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_partition_table_get_widget;
        iface->get_name = gdu_page_partition_table_get_name;
        iface->update = gdu_page_partition_table_update;
}
