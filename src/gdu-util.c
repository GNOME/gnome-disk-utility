/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-main.c
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

#include "gdu-util.h"

#define KILOBYTE_FACTOR 1024.0
#define MEGABYTE_FACTOR (1024.0 * 1024.0)
#define GIGABYTE_FACTOR (1024.0 * 1024.0 * 1024.0)

char *
gdu_util_get_size_for_display (guint64 size, gboolean long_string)
{
        char *str;
        gdouble displayed_size;

        if (size < MEGABYTE_FACTOR) {
                displayed_size = (double) size / KILOBYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f KB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f KB"), displayed_size);
        } else if (size < GIGABYTE_FACTOR) {
                displayed_size = (double) size / MEGABYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f MB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f MB"), displayed_size);
        } else {
                displayed_size = (double) size / GIGABYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f GB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f GB"), displayed_size);
        }

        return str;
}

char *
gdu_util_get_fstype_for_display (const char *fstype, const char *fsversion, gboolean long_string)
{
        char *s;

        if (fstype == NULL) {
                fstype = "";
        }

        if (fsversion == NULL) {
                fsversion = "";
        }

        if (strcmp (fstype, "vfat") == 0) {
                /* version = FAT12 | FAT16 | FAT32 */

                if (strcmp (fsversion, "FAT12") == 0) {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT (12-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT16") == 0) {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT (16-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT32") == 0) {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT (32-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                }
        } else if (strcmp (fstype, "ntfs") == 0) {
                if (long_string) {
                        s = g_strdup_printf (_("Microsoft NTFS version %s"), fsversion);
                } else {
                        s = g_strdup (_("NTFS"));
                }
        } else if (strcmp (fstype, "hfs") == 0) {
                if (long_string) {
                        s = g_strdup (_("Apple HFS"));
                } else {
                        s = g_strdup (_("HFS"));
                }
        } else if (strcmp (fstype, "hfsplus") == 0) {
                if (long_string) {
                        s = g_strdup (_("Apple HFS+"));
                } else {
                        s = g_strdup (_("HFS+"));
                }
        } else if (strcmp (fstype, "crypto_LUKS") == 0) {
                if (long_string) {
                        s = g_strdup (_("Linux Unified Key Setup"));
                } else {
                        s = g_strdup (_("LUKS"));
                }
        } else if (strcmp (fstype, "ext2") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                s = g_strdup_printf (_("Linux Second Ext. FS (version %s)"), fsversion);
                        else
                                s = g_strdup_printf (_("Linux Second Ext. FS"));
                } else {
                        s = g_strdup (_("ext2"));
                }
        } else if (strcmp (fstype, "ext3") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                s = g_strdup_printf (_("Linux Third Ext. FS (version %s)"), fsversion);
                        else
                                s = g_strdup_printf (_("Linux Third Ext. FS"));
                } else {
                        s = g_strdup (_("ext3"));
                }
        } else if (strcmp (fstype, "jbd") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                s = g_strdup_printf (_("Journal for Linux ext3 (version %s)"), fsversion);
                        else
                                s = g_strdup_printf (_("Journal for Linux ext3"));
                } else {
                        s = g_strdup (_("jbd"));
                }
        } else if (strcmp (fstype, "iso9660") == 0) {
                if (long_string) {
                        s = g_strdup (_("ISO 9660"));
                } else {
                        s = g_strdup (_("iso9660"));
                }
        } else if (strcmp (fstype, "udf") == 0) {
                if (long_string) {
                        s = g_strdup (_("Universal Disk Format"));
                } else {
                        s = g_strdup (_("udf"));
                }
        } else {
                s = g_strdup (fstype);
        }

        return s;
}

char *
gdu_get_job_description (const char *job_id)
{
        char *s;
        if (strcmp (job_id, "Erase") == 0) {
                s = g_strdup (_("Erasing"));
        } else if (strcmp (job_id, "CreateFilesystem") == 0) {
                s = g_strdup (_("Creating File System"));
        } else if (strcmp (job_id, "Mount") == 0) {
                s = g_strdup (_("Mounting"));
        } else if (strcmp (job_id, "Unmount") == 0) {
                s = g_strdup (_("Unmounting"));
        } else {
                s = g_strdup_printf ("%s", job_id);
        }
        return s;
}

char *
gdu_get_task_description (const char *task_id)
{
        char *s;
        if (strcmp (task_id, "zeroing") == 0) {
                s = g_strdup (_("Zeroing data"));
        } else if (strcmp (task_id, "sync") == 0) {
                s = g_strdup (_("Flushing data to disk"));
        } else if (strcmp (task_id, "mkfs") == 0) {
                s = g_strdup (_("Creating File System"));
        } else if (strlen (task_id) == 0) {
                s = g_strdup ("");
        } else {
                s = g_strdup_printf ("%s", task_id);
        }
        return s;
}

char *
gdu_util_get_desc_for_part_type (const char *scheme, const char *type)
{
        int n;
        static struct {
                const char *type;
                char *name;
        } part_type[] = {
                /* see http://en.wikipedia.org/wiki/GUID_Partition_Table */

                {"024DEE41-33E7-11D3-9D69-0008C781F39F", N_("MBR Partition Scheme")},
                {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", N_("EFI System Partition")},
                /* Microsoft */
                {"E3C9E316-0B5C-4DB8-817D-F92DF00215AE", N_("Microsoft Reserved Partition")},
                {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")},
                {"5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", N_("LDM meta data Partition")},
                {"AF9B60A0-1431-4F62-BC68-3311714A69AD", N_("LDM data Partition")},
                /* Linux */
                {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")}, /* Same GUID as MS! */
                {"A19D880F-05FC-4D3B-A006-743F0F84911E", N_("Linux RAID Partition")},
                {"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", N_("Linux Swap Partition")},
                {"E6D6D379-F507-44C2-A23C-238F2A3DF928", N_("Linux LVM Partition")},
                {"8DA63339-0007-60C0-C436-083AC8230908", N_("Linux Reserved Partition")},
                /* Mac OS X */
                {"48465300-0000-11AA-AA11-00306543ECAC", N_("Apple HFS/HFS+ Partition")},
                {"55465300-0000-11AA-AA11-00306543ECAC", N_("Apple UFS Partition")},
                {"52414944-0000-11AA-AA11-00306543ECAC", N_("Apple RAID Partition")},


                /* see http://developer.apple.com/documentation/mac/Devices/Devices-126.html
                 *     http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
                {"Apple_Unix_SVR2", N_("Apple UFS Partition")},
                {"Apple_HFS", N_("Apple HFS/HFS+ Partition")},
                {"Apple_partition_map", N_("Apple Partition Map")},
                {"DOS_FAT_12", N_("FAT 12")},
                {"DOS_FAT_16", N_("FAT 16")},
                {"DOS_FAT_32", N_("FAT 32")},
                {"Windows_FAT_16", N_("FAT 16")},
                {"Windows_FAT_32", N_("FAT 32")},

                /* see also http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
                {"0x00",  N_("Empty")},
                {"0x01",  N_("FAT12")},
                {"0x04",  N_("FAT16 <32M")},
                {"0x05",  N_("Extended")},
                {"0x06",  N_("FAT16")},
                {"0x07",  N_("HPFS/NTFS")},
                {"0x0b",  N_("W95 FAT32")},
                {"0x0c",  N_("W95 FAT32 (LBA)")},
                {"0x0e",  N_("W95 FAT16 (LBA)")},
                {"0x0f",  N_("W95 Ext d (LBA)")},
                {"0x10",  N_("OPUS")},
                {"0x11",  N_("Hidden FAT12")},
                {"0x12",  N_("Compaq diagnostics")},
                {"0x14",  N_("Hidden FAT16 <32M")},
                {"0x16",  N_("Hidden FAT16")},
                {"0x17",  N_("Hidden HPFS/NTFS")},
                {"0x1b",  N_("Hidden W95 FAT32")},
                {"0x1c",  N_("Hidden W95 FAT32 (LBA)")},
                {"0x1e",  N_("Hidden W95 FAT16 (LBA)")},
                {"0x3c",  N_("PartitionMagic")},
                {"0x82",  N_("Linux swap")},
                {"0x83",  N_("Linux")},
                {"0x84",  N_("Hibernation")},
                {"0x85",  N_("Linux Extended")},
                {"0x8e",  N_("Linux LVM")},
                {"0xa0",  N_("Hibernation")},
                {"0xa5",  N_("FreeBSD")},
                {"0xa6",  N_("OpenBSD")},
                {"0xa8",  N_("Mac OS X")},
                {"0xaf",  N_("Mac OS X")},
                {"0xbe",  N_("Solaris boot")},
                {"0xbf",  N_("Solaris")},
                {"0xeb",  N_("BeOS BFS")},
                {"0xec",  N_("SkyOS SkyFS")},
                {"0xee",  N_("EFI GPT")},
                {"0xef",  N_("EFI (FAT-12/16/32")},
                {"0xfd",  N_("Linux RAID autodetect")},

                {NULL,  NULL}
        };

        for (n = 0; part_type[n].name != NULL; n++) {
                if (g_ascii_strcasecmp (part_type[n].type, type) == 0)
                        return g_strdup (part_type[n].name);
        }

        return g_strdup (type);
}

/* TODO: retrieve this list from DeviceKit-disks */
static GduCreatableFilesystem creatable_fstypes[] = {
        {"vfat", 11},
        {"ext3", 16},
        {"empty", 0},
};

static int num_creatable_fstypes = sizeof (creatable_fstypes) / sizeof (GduCreatableFilesystem);

GduCreatableFilesystem *
gdu_util_find_creatable_filesystem_for_fstype (const char *fstype)
{
        int n;
        GduCreatableFilesystem *ret;

        ret = NULL;
        for (n = 0; n < num_creatable_fstypes; n++) {
                if (strcmp (fstype, creatable_fstypes[n].id) == 0) {
                        ret = &(creatable_fstypes[n]);
                        break;
                }
        }

        return ret;
}

GList *
gdu_util_get_creatable_filesystems (void)
{
        int n;
        GList *ret;

        ret = NULL;
        for (n = 0; n < num_creatable_fstypes; n++) {
                ret = g_list_append (ret, &creatable_fstypes[n]);
        }
        return ret;
}

static GtkListStore *
gdu_util_fstype_combo_box_create_store (const char *include_extended_partitions_for_scheme)
{
        GList *l;
        GtkListStore *store;
        GList *creatable_filesystems;
        GtkTreeIter iter;

        store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

        creatable_filesystems = gdu_util_get_creatable_filesystems ();
        for (l = creatable_filesystems; l != NULL; l = l->next) {
                GduCreatableFilesystem *f = l->data;
                const char *fstype;
                char *fstype_name;

                fstype = f->id;

                if (strcmp (fstype, "empty") == 0) {
                        fstype_name = g_strdup (_("Empty (don't create a file system)"));
                } else {
                        fstype_name = gdu_util_get_fstype_for_display (fstype, NULL, TRUE);
                }

                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter,
                                    0, fstype,
                                    1, fstype_name,
                                    -1);

                g_free (fstype_name);
        }
        g_list_free (creatable_filesystems);

        if (include_extended_partitions_for_scheme != NULL &&
            strcmp  (include_extended_partitions_for_scheme, "mbr") == 0) {
                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter,
                                    0, "msdos_extended_partition",
                                    1, "Extended Partition",
                                    -1);
        }

        return store;
}

void
gdu_util_fstype_combo_box_rebuild (GtkWidget  *combo_box,
                                   const char *include_extended_partitions_for_scheme)
{
        GtkListStore *store;
        store = gdu_util_fstype_combo_box_create_store (include_extended_partitions_for_scheme);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
        g_object_unref (store);
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
}

/**
 * gdu_util_fstype_combo_box_create:
 * @include_extended_partitions_for_scheme: if not #NULL, includes
 * extended partition types. This is currently only relevant for
 * Master Boot Record ("mbr") where a single item "Extended Partition"
 * will be returned.
 *
 * Get a combo box with the file system types that the DeviceKit-disks
 * daemon can create.
 *
 * Returns: A #GtkComboBox widget
 **/
GtkWidget *
gdu_util_fstype_combo_box_create (const char *include_extended_partitions_for_scheme)
{
        GtkListStore *store;
	GtkCellRenderer *renderer;
        GtkWidget *combo_box;


        combo_box = gtk_combo_box_new ();
        store = gdu_util_fstype_combo_box_create_store (include_extended_partitions_for_scheme);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
        g_object_unref (store);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
					"text", 1,
					NULL);

        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

        return combo_box;
}

gboolean
gdu_util_fstype_combo_box_select (GtkWidget *combo_box, const char *fstype)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        gboolean ret;

        ret = FALSE;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
        gtk_tree_model_get_iter_first (model, &iter);
        do {
                char *iter_fstype;

                gtk_tree_model_get (model, &iter, 0, &iter_fstype, -1);
                if (iter_fstype != NULL && strcmp (fstype, iter_fstype) == 0) {
                        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo_box), &iter);
                        ret = TRUE;
                }
                g_free (iter_fstype);
        } while (!ret && gtk_tree_model_iter_next (model, &iter));

        return ret;
}

char *
gdu_util_fstype_combo_box_get_selected (GtkWidget *combo_box)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        char *fstype;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
        fstype = NULL;
        if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), &iter))
                gtk_tree_model_get (model, &iter, 0, &fstype, -1);

        return fstype;
}


GtkWidget *
gdu_util_secure_erase_combo_box_create (void)
{
        GtkWidget *combo_box;

        combo_box = gtk_combo_box_new_text ();
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Don't overwrite data"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Overwrite data"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Overwrite data 3 times"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Overwrite data 7 times"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (combo_box), _("Overwrite data 35 times"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0); /* read default from gconf; use lockdown too */
        return combo_box;
}

char *
gdu_util_secure_erase_combo_box_get_selected (GtkWidget *combo_box)
{
        const char *result[] = {"none", "full", "full3pass", "full7pass", "full35pass"};
        int active;

        active = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
        g_assert (active >= 0 && active < sizeof (result));
        return g_strdup (result[active]);
}

char *
gdu_util_get_default_part_type_for_scheme_and_fstype (const char *scheme, const char *fstype, guint64 size)
{
        const char *type;

        type = NULL;

        /* TODO: this function needs work: handle swap partitions, msdos extended, raid, LVM, EFI GPT etc. */

        if (strcmp (scheme, "mbr") == 0) {
                if (strcmp (fstype, "vfat") == 0) {
                        /* TODO: maybe consider size */
                        type = "0x0c";
                } else {
                        type = "0x83";
                }
        } else if (strcmp (scheme, "gpt") == 0) {
                type = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7";
        } else if (strcmp (scheme, "apm") == 0) {
                type = "Windows_FAT_32";
        }

        return g_strdup (type);
}

/**
 * gdu_util_find_toplevel_presentable:
 * @presentable: a #GduPresentable.
 *
 * Finds the top-level presentable for a given presentable.
 *
 * Returns: The presentable; caller must unref when done with it
 **/
GduPresentable *
gdu_util_find_toplevel_presentable (GduPresentable *presentable)
{
        GduPresentable *parent;
        GduPresentable *maybe_parent;

        parent = presentable;
        do {
                maybe_parent = gdu_presentable_get_enclosing_presentable (parent);
                if (maybe_parent != NULL) {
                        g_object_unref (maybe_parent);
                        parent = maybe_parent;
                }
        } while (maybe_parent != NULL);

        return g_object_ref (parent);
}
