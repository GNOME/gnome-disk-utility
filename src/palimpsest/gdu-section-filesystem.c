/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-filesystem.c
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

#include <gdu/gdu.h>
#include "gdu-section-filesystem.h"

struct _GduSectionFilesystemPrivate
{
        gboolean init_done;

        GtkWidget *modify_fs_vbox;
        GtkWidget *modify_fs_label_entry;
        GtkWidget *modify_button;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionFilesystem, gdu_section_filesystem, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
update (GduSectionFilesystem *section)
{
        GduDevice *device;
        GduPool *pool;
        GduKnownFilesystem *kfs;
        const char *fstype;
        int max_label_len;
        gboolean changed;
        const char *fslabel;
        const char *new_fslabel;

        max_label_len = 0;
        device = NULL;
        kfs = NULL;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto error;
        }

        pool = gdu_shell_get_pool (gdu_section_get_shell (GDU_SECTION (section)));

        fstype = gdu_device_id_get_type (device);
        if (fstype == NULL)
                goto out;

        kfs = gdu_pool_get_known_filesystem_by_id (pool, fstype);
        if (kfs == NULL)
                goto out;

        if (!gdu_known_filesystem_get_supports_label_rename (kfs))
                goto out;

        if (!gdu_known_filesystem_get_supports_online_label_rename (kfs) && gdu_device_is_mounted (device)) {
                /* TODO: we could show a helpful warning explaining
                 *       why the user can't change the name
                 */
                goto out;
        }

        max_label_len = gdu_known_filesystem_get_max_label_len (kfs);

out:
        gtk_widget_set_sensitive (section->priv->modify_fs_vbox, !gdu_device_is_read_only (device));

        fslabel = gdu_device_id_get_label (device);
        new_fslabel = gtk_entry_get_text (GTK_ENTRY (section->priv->modify_fs_label_entry));

        if (!section->priv->init_done) {
                section->priv->init_done = TRUE;
                gtk_entry_set_text (GTK_ENTRY (section->priv->modify_fs_label_entry), fslabel != NULL ? fslabel : "");
        }


        changed = FALSE;
        if (fslabel != NULL && new_fslabel != NULL && strcmp (fslabel, new_fslabel) != 0)
                changed = TRUE;

        gtk_entry_set_max_length (GTK_ENTRY (section->priv->modify_fs_label_entry), max_label_len);
        gtk_widget_set_sensitive (section->priv->modify_fs_label_entry, max_label_len > 0);
        gtk_widget_set_sensitive (section->priv->modify_button, max_label_len > 0 && changed);

error:
        if (kfs != NULL)
                g_object_unref (kfs);
        if (device != NULL)
                g_object_unref (device);
}

static void
modify_fs_label_entry_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduSectionFilesystem *section = GDU_SECTION_FILESYSTEM (user_data);
        update (section);
}

static void
change_filesystem_label_callback (GduDevice *device,
                                  GError *error,
                                  gpointer user_data)
{
        GduSection *section = GDU_SECTION (user_data);
        if (error != NULL) {
                gdu_shell_raise_error (gdu_section_get_shell (section),
                                       gdu_section_get_presentable (section),
                                       error,
                                       _("Error setting file system label"));
        }
        g_object_unref (section);
}

static void
on_change_clicked (GtkButton *button,
                   gpointer   user_data)
{
        GduSectionFilesystem *section = GDU_SECTION_FILESYSTEM (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL)
                goto out;

        gdu_device_op_filesystem_set_label (
                device,
                gtk_entry_get_text (GTK_ENTRY (section->priv->modify_fs_label_entry)),
                change_filesystem_label_callback,
                g_object_ref (section));

out:

        if (device != NULL)
                g_object_unref (device);
}


/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_filesystem_finalize (GduSectionFilesystem *section)
{
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_filesystem_class_init (GduSectionFilesystemClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_filesystem_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionFilesystemPrivate));
}

static void
gdu_section_filesystem_init (GduSectionFilesystem *section)
{
        GtkWidget *vbox2;
        GtkWidget *vbox3;
        GtkWidget *button;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *entry;
        int row;
        char *s;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_FILESYSTEM, GduSectionFilesystemPrivate);

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (section), vbox3, FALSE, TRUE, 0);
        section->priv->modify_fs_vbox = vbox3;

        label = gtk_label_new (NULL);
        s = g_strconcat ("<b>", _("Mountable Filesystem"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, FALSE, 6);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The volume contains a mountable filesystem."));
        gtk_label_set_width_chars (GTK_LABEL (label), 60);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (1, 3, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);

        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

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
        section->priv->modify_fs_label_entry = entry;

        button = gtk_button_new_with_mnemonic (_("_Change"));
        gtk_widget_set_tooltip_text (button, _("Change"));
        g_signal_connect (button, "clicked", G_CALLBACK (on_change_clicked), section);
        section->priv->modify_button = button;
        gtk_table_attach (GTK_TABLE (table), button, 2, 3, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        g_signal_connect (section->priv->modify_fs_label_entry, "changed",
                          G_CALLBACK (modify_fs_label_entry_changed), section);

        row++;
}

GtkWidget *
gdu_section_filesystem_new (GduShell       *shell,
                            GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_FILESYSTEM,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
