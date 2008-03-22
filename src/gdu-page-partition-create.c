/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-partition-create.c
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
#include "gdu-page-partition-create.h"

struct _GduPagePartitionCreatePrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;
        GtkWidget *disk_widget;

        GtkWidget *create_part_table_vbox;

        GtkWidget *create_part_vbox;
        GtkWidget *create_part_size_hscale;
        GtkWidget *create_part_fstype_combo_box;
        GtkWidget *create_part_fslabel_entry;
        GtkWidget *create_part_secure_erase_combo_box;

        GduPresentable *presentable;

        PolKitAction *pk_create_partition_action;
        PolKitGnomeAction *create_partition_action;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_partition_create_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPagePartitionCreate, gdu_page_partition_create, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_partition_create_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void
gdu_page_partition_create_finalize (GduPagePartitionCreate *page)
{
        polkit_action_unref (page->priv->pk_create_partition_action);
        g_object_unref (page->priv->create_partition_action);

        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);
        if (page->priv->presentable != NULL)
                g_object_unref (page->priv->presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_partition_create_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
        GduPagePartitionCreate *page = GDU_PAGE_PARTITION_CREATE (object);

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
gdu_page_partition_create_get_property (GObject     *object,
                                    guint        prop_id,
                                    GValue      *value,
                                    GParamSpec  *pspec)
{
        GduPagePartitionCreate *page = GDU_PAGE_PARTITION_CREATE (object);

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
gdu_page_partition_create_class_init (GduPagePartitionCreateClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_partition_create_finalize;
        obj_class->set_property = gdu_page_partition_create_set_property;
        obj_class->get_property = gdu_page_partition_create_get_property;

        /**
         * GduPagePartitionCreate:shell:
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
create_partition_callback (GtkAction *action, gpointer user_data)
{
        GduPagePartitionCreate *page = GDU_PAGE_PARTITION_CREATE (user_data);
        GduPresentable *presentable;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        GduDevice *device;
        guint64 offset;
        guint64 size;
        char *type;
        char *label;
        char **flags;
        char *fstype;
        char *fslabel;
        char *fserase;
        const char *scheme;

        type = NULL;
        label = NULL;
        flags = NULL;
        device = NULL;
        fstype = NULL;
        fslabel = NULL;
        fserase = NULL;
        toplevel_presentable = NULL;
        toplevel_device = NULL;

        presentable = gdu_shell_get_selected_presentable (page->priv->shell);
        g_assert (presentable != NULL);

        device = gdu_presentable_get_device (presentable);
        if (device != NULL) {
                g_warning ("%s: device is supposed to be NULL",  __FUNCTION__);
                goto out;
        }

        toplevel_presentable = gdu_util_find_toplevel_presentable (presentable);
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }
        if (!gdu_device_is_partition_table (toplevel_device)) {
                g_warning ("%s: device for toplevel presentable is not a partition table", __FUNCTION__);
                goto out;
        }

        offset = gdu_presentable_get_offset (presentable);
        size = (guint64) gtk_range_get_value (GTK_RANGE (page->priv->create_part_size_hscale));
        fstype = gdu_util_fstype_combo_box_get_selected (page->priv->create_part_fstype_combo_box);
        fslabel = g_strdup (gtk_entry_get_text (GTK_ENTRY (page->priv->create_part_fslabel_entry)));
        fserase = gdu_util_secure_erase_combo_box_get_selected (page->priv->create_part_secure_erase_combo_box);

        /* TODO: set flags */
        flags = NULL;

        /* default the partition type according to the kind of file system */
        scheme = gdu_device_partition_table_get_scheme (toplevel_device);

        /* see gdu_util.c:gdu_util_fstype_combo_box_create_store() */
        if (strcmp (fstype, "msdos_extended_partition") == 0) {
                type = g_strdup ("0x05");
                g_free (fstype);
                g_free (fslabel);
                g_free (fserase);
                fstype = g_strdup ("");
                fslabel = g_strdup ("");
                fserase = g_strdup ("");
        } else {
                type = gdu_util_get_default_part_type_for_scheme_and_fstype (scheme, fstype, size);
                if (type == NULL) {
                        g_warning ("Cannot determine default part type for scheme '%s' and fstype '%s'",
                                   scheme, fstype);
                        goto out;
                }
        }

        /* set partition label to the file system label (TODO: handle max len) */
        if (strcmp (scheme, "gpt") == 0 || strcmp (scheme, "apm") == 0) {
                label = g_strdup (label);
        } else {
                label = g_strdup ("");
        }

        gdu_device_op_create_partition (toplevel_device,
                                        offset,
                                        size,
                                        type,
                                        label,
                                        flags,
                                        fstype,
                                        fslabel,
                                        fserase);

        /* go to toplevel */
        gdu_shell_select_presentable (page->priv->shell, toplevel_presentable);

out:
        g_free (type);
        g_free (label);
        g_strfreev (flags);
        g_free (fstype);
        g_free (fslabel);
        g_free (fserase);
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
}

static void
create_part_fstype_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduPagePartitionCreate *page = GDU_PAGE_PARTITION_CREATE (user_data);
        char *fstype;
        GduCreatableFilesystem *creatable_fs;
        gboolean label_entry_sensitive;
        int max_label_len;

        label_entry_sensitive = FALSE;
        max_label_len = 0;

        fstype = gdu_util_fstype_combo_box_get_selected (combo_box);
        if (fstype != NULL) {
                creatable_fs = gdu_util_find_creatable_filesystem_for_fstype (fstype);
                /* Note: there may not have a creatable file system... e.g. the user could
                 *       select "Extended" on mbr partition tables.
                 */
                if (creatable_fs != NULL) {
                        max_label_len = creatable_fs->max_label_len;
                }
        }

        if (max_label_len > 0)
                label_entry_sensitive = TRUE;

        gtk_entry_set_max_length (GTK_ENTRY (page->priv->create_part_fslabel_entry), max_label_len);
        gtk_widget_set_sensitive (page->priv->create_part_fslabel_entry, label_entry_sensitive);

        g_free (fstype);
}

static char*
create_part_size_format_value_callback (GtkScale *scale, gdouble value)
{
        return gdu_util_get_size_for_display ((guint64) value, FALSE);
}

static void
gdu_page_partition_create_init (GduPagePartitionCreate *page)
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
        GtkWidget *hscale;
        GtkWidget *button_box;
        int row;

        page->priv = g_new0 (GduPagePartitionCreatePrivate, 1);

        page->priv->pk_create_partition_action = polkit_action_new ();
        polkit_action_set_action_id (page->priv->pk_create_partition_action, "org.freedesktop.devicekit.disks.erase");
        page->priv->create_partition_action = polkit_gnome_action_new_default (
                "create-partition",
                page->priv->pk_create_partition_action,
                _("_Create"),
                _("Create"));
        g_object_set (page->priv->create_partition_action,
                      "auth-label", _("_Create..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (page->priv->create_partition_action, "activate",
                          G_CALLBACK (create_partition_callback), page);

        page->priv->main_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        hbox = gtk_hbox_new (FALSE, 10);

        page->priv->disk_widget = gdu_disk_widget_new (NULL);
        gtk_widget_set_size_request (page->priv->disk_widget, 150, 150);

        vbox = gtk_vbox_new (FALSE, 10);

        /* ---------------- */
        /* Create partition */
        /* ---------------- */

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox3, FALSE, TRUE, 0);
        page->priv->create_part_vbox = vbox3;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Create Partition</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To create a new partition, select the size and whether to create "
                                                   "a file system. The partition type, label and flags can be changed "
                                                   "after creation."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* create size */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("S_ize:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        hscale = gtk_hscale_new_with_range (0, 100, 128 * 1024 * 1024);
        gtk_table_attach (GTK_TABLE (table), hscale, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        g_signal_connect (hscale, "format-value", (GCallback) create_part_size_format_value_callback, page);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), hscale);
        page->priv->create_part_size_hscale = hscale;

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
        page->priv->create_part_secure_erase_combo_box = combo_box;

        row++;

        /* secure erase desc */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gdu_util_secure_erase_combo_box_set_desc_label (combo_box, label);

        row++;

        /* _file system_ type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        combo_box = gdu_util_fstype_combo_box_create (NULL);
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        page->priv->create_part_fstype_combo_box = combo_box;

        row++;

        /* fstype desc */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gdu_util_fstype_combo_box_set_desc_label (combo_box, label);

        row++;

        /* _file system_ label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        page->priv->create_part_fslabel_entry = entry;

        /* create button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = polkit_gnome_action_create_button (page->priv->create_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* update sensivity and length of fs label and ensure it's set initially */
        g_signal_connect (page->priv->create_part_fstype_combo_box, "changed",
                          G_CALLBACK (create_part_fstype_combo_box_changed), page);
        create_part_fstype_combo_box_changed (page->priv->create_part_fstype_combo_box, page);


        gtk_box_pack_start (GTK_BOX (hbox), page->priv->disk_widget, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), hbox, TRUE, TRUE, 0);
}


GduPagePartitionCreate *
gdu_page_partition_create_new (GduShell *shell)
{
        return GDU_PAGE_PARTITION_CREATE (g_object_new (GDU_TYPE_PAGE_PARTITION_CREATE, "shell", shell, NULL));
}

static gboolean
has_extended_partition (GduPagePartitionCreate *page, GduPresentable *presentable)
{
        GList *l;
        GList *enclosed_presentables;
        gboolean ret;
        GduPool *pool;

        ret = FALSE;

        pool = gdu_presentable_get_pool (presentable);
        enclosed_presentables = gdu_pool_get_enclosed_presentables (pool, presentable);
        g_object_unref (pool);

        for (l = enclosed_presentables; l != NULL; l = l->next) {
                GduPresentable *p = l->data;
                GduDevice *d;

                d = gdu_presentable_get_device (p);
                if (d == NULL)
                        continue;

                if (gdu_device_is_partition (d)) {
                        int partition_type;
                        partition_type = strtol (gdu_device_partition_get_type (d), NULL, 0);
                        if (partition_type == 0x05 ||
                            partition_type == 0x0f ||
                            partition_type == 0x85) {
                                ret = TRUE;
                                break;
                        }
                }

        }
        return ret;
}

static gboolean
gdu_page_partition_create_update (GduPage *_page, GduPresentable *presentable)
{
        GduPagePartitionCreate *page = GDU_PAGE_PARTITION_CREATE (_page);
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

        if (device == NULL) {
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

        gtk_range_set_range (GTK_RANGE (page->priv->create_part_size_hscale), 0, size);
        gtk_range_set_value (GTK_RANGE (page->priv->create_part_size_hscale), size);
        gtk_widget_show (page->priv->create_part_vbox);

        /* only allow creation of extended partitions if there currently are none */
        if (has_extended_partition (page, toplevel_presentable)) {
                gdu_util_fstype_combo_box_rebuild (page->priv->create_part_fstype_combo_box, NULL);
        } else {
                gdu_util_fstype_combo_box_rebuild (page->priv->create_part_fstype_combo_box, scheme);
        }

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
gdu_page_partition_create_get_widget (GduPage *_page)
{
        GduPagePartitionCreate *page = GDU_PAGE_PARTITION_CREATE (_page);
        return page->priv->main_vbox;
}

static char *
gdu_page_partition_create_get_name (GduPage *page)
{
        return g_strdup (_("Create Partition"));
}

static void
gdu_page_partition_create_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_partition_create_get_widget;
        iface->get_name = gdu_page_partition_create_get_name;
        iface->update = gdu_page_partition_create_update;
}
