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
#include <gnome-keyring.h>

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
                                s = g_strdup (_("FAT (12-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT16") == 0) {
                        if (long_string) {
                                s = g_strdup (_("FAT (16-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT32") == 0) {
                        if (long_string) {
                                s = g_strdup (_("FAT (32-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else {
                        if (long_string) {
                                s = g_strdup (_("FAT"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                }
        } else if (strcmp (fstype, "ntfs") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                s = g_strdup_printf (_("NTFS version %s"), fsversion);
                        else
                                s = g_strdup_printf (_("NTFS"));
                } else {
                        s = g_strdup (_("NTFS"));
                }
        } else if (strcmp (fstype, "hfs") == 0) {
                if (long_string) {
                        s = g_strdup (_("HFS"));
                } else {
                        s = g_strdup (_("HFS"));
                }
        } else if (strcmp (fstype, "hfsplus") == 0) {
                if (long_string) {
                        s = g_strdup (_("HFS+"));
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
        } else if (strcmp (fstype, "swap") == 0) {
                if (long_string) {
                        s = g_strdup (_("Swap Space"));
                } else {
                        s = g_strdup (_("swap"));
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

static struct {
        const char *scheme;
        const char *type;
        char *name;
} part_type[] = {
        /* see http://en.wikipedia.org/wiki/GUID_Partition_Table */

        {"gpt", "024DEE41-33E7-11D3-9D69-0008C781F39F", N_("MBR Partition Scheme")},
        {"gpt", "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", N_("EFI System Partition")},
        /* Microsoft */
        {"gpt", "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", N_("Microsoft Reserved Partition")},
        /* {"gpt", "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")}, */
        {"gpt", "5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", N_("LDM meta data Partition")},
        {"gpt", "AF9B60A0-1431-4F62-BC68-3311714A69AD", N_("LDM data Partition")},
        /* Linux */
        {"gpt", "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")}, /* Same GUID as MS! */
        {"gpt", "A19D880F-05FC-4D3B-A006-743F0F84911E", N_("Linux RAID Partition")},
        {"gpt", "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", N_("Linux Swap Partition")},
        {"gpt", "E6D6D379-F507-44C2-A23C-238F2A3DF928", N_("Linux LVM Partition")},
        {"gpt", "8DA63339-0007-60C0-C436-083AC8230908", N_("Linux Reserved Partition")},
        /* Mac OS X */
        {"gpt", "48465300-0000-11AA-AA11-00306543ECAC", N_("Apple HFS/HFS+ Partition")},
        {"gpt", "55465300-0000-11AA-AA11-00306543ECAC", N_("Apple UFS Partition")},
        {"gpt", "52414944-0000-11AA-AA11-00306543ECAC", N_("Apple RAID Partition")},


        /* see http://developer.apple.com/documentation/mac/Devices/Devices-126.html
         *     http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
        {"apm", "Apple_Unix_SVR2", N_("Apple UFS Partition")},
        {"apm", "Apple_HFS", N_("Apple HFS/HFS+ Partition")},
        {"apm", "Apple_partition_map", N_("Apple Partition Map")},
        {"apm", "Apple_Free", N_("Unused Partition")},
        {"apm", "Apple_Scratch", N_("Empty Partition")},
        {"apm", "Apple_Driver", N_("Driver Partition")},
        {"apm", "Apple_Driver43", N_("Driver 4.3 Partition")},
        {"apm", "Apple_PRODOS", N_("ProDOS file system")},
        {"apm", "DOS_FAT_12", N_("FAT 12")},
        {"apm", "DOS_FAT_16", N_("FAT 16")},
        {"apm", "DOS_FAT_32", N_("FAT 32")},
        {"apm", "Windows_FAT_16", N_("FAT 16 (Windows)")},
        {"apm", "Windows_FAT_32", N_("FAT 32 (Windows)")},

        /* see http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
        {"mbr", "0x00",  N_("Empty (0x00)")},
        {"mbr", "0x01",  N_("FAT12 (0x01)")},
        {"mbr", "0x04",  N_("FAT16 <32M (0x04)")},
        {"mbr", "0x05",  N_("Extended (0x05)")},
        {"mbr", "0x06",  N_("FAT16 (0x06)")},
        {"mbr", "0x07",  N_("HPFS/NTFS (0x07)")},
        {"mbr", "0x0b",  N_("W95 FAT32 (0x0b)")},
        {"mbr", "0x0c",  N_("W95 FAT32 (LBA) (0x0c)")},
        {"mbr", "0x0e",  N_("W95 FAT16 (LBA) (0x0e)")},
        {"mbr", "0x0f",  N_("W95 Ext d (LBA) (0x0f)")},
        {"mbr", "0x10",  N_("OPUS (0x10)")},
        {"mbr", "0x11",  N_("Hidden FAT12 (0x11)")},
        {"mbr", "0x12",  N_("Compaq diagnostics (0x12)")},
        {"mbr", "0x14",  N_("Hidden FAT16 <32M (0x14)")},
        {"mbr", "0x16",  N_("Hidden FAT16 (0x16)")},
        {"mbr", "0x17",  N_("Hidden HPFS/NTFS (0x17)")},
        {"mbr", "0x1b",  N_("Hidden W95 FAT32 (0x1b)")},
        {"mbr", "0x1c",  N_("Hidden W95 FAT32 (LBA) (0x1c)")},
        {"mbr", "0x1e",  N_("Hidden W95 FAT16 (LBA) (0x1e)")},
        {"mbr", "0x3c",  N_("PartitionMagic (0x3c)")},
        {"mbr", "0x82",  N_("Linux swap (0x82)")},
        {"mbr", "0x83",  N_("Linux (0x83)")},
        {"mbr", "0x84",  N_("Hibernation (0x84)")},
        {"mbr", "0x85",  N_("Linux Extended (0x85)")},
        {"mbr", "0x8e",  N_("Linux LVM (0x8e)")},
        {"mbr", "0xa0",  N_("Hibernation (0xa0)")},
        {"mbr", "0xa5",  N_("FreeBSD (0xa5)")},
        {"mbr", "0xa6",  N_("OpenBSD (0xa6)")},
        {"mbr", "0xa8",  N_("Mac OS X (0xa8)")},
        {"mbr", "0xaf",  N_("Mac OS X (0xaf)")},
        {"mbr", "0xbe",  N_("Solaris boot (0xbe)")},
        {"mbr", "0xbf",  N_("Solaris (0xbf)")},
        {"mbr", "0xeb",  N_("BeOS BFS (0xeb)")},
        {"mbr", "0xec",  N_("SkyOS SkyFS (0xec)")},
        {"mbr", "0xee",  N_("EFI GPT (0xee)")},
        {"mbr", "0xef",  N_("EFI (FAT-12/16/32 (0xef)")},
        {"mbr", "0xfd",  N_("Linux RAID autodetect (0xfd)")},

        {NULL,  NULL, NULL}
};

char *
gdu_util_get_desc_for_part_type (const char *scheme, const char *type)
{
        int n;

        for (n = 0; part_type[n].name != NULL; n++) {
                if (g_ascii_strcasecmp (part_type[n].type, type) == 0)
                        return g_strdup (part_type[n].name);
        }

        return g_strdup_printf (_("Unknown (%s)"), type);
}



/* TODO: retrieve this list from DeviceKit-disks */
static GduCreatableFilesystem creatable_fstypes[] = {
        {"vfat", 11},
        {"ext3", 16},
        {"swap", 0},
        {"ntfs", 255},
        {"empty", 0},
};

/* TODO: retrieve from daemon */
gboolean
gdu_util_can_create_encrypted_device (void)
{
        return TRUE;
}

static int num_creatable_fstypes = sizeof (creatable_fstypes) / sizeof (GduCreatableFilesystem);

static char *
gdu_util_fstype_get_description (char *fstype)
{
        g_return_val_if_fail (fstype != NULL, NULL);

        if (strcmp (fstype, "vfat") == 0)
                return g_strdup (_("A popular format compatible with almost any device or system, typically "
                                   "used for file exchange. Maximum file size is limited to 2GB and the file "
                                   "system doesn't support file permissions."));

        else if (strcmp (fstype, "ext3") == 0)
                return g_strdup (_("This file system is compatible with Linux systems only and provides classic "
                                   "UNIX file permissions support. Not ideal if you plan to move the disk "
                                   "around between different systems."));

        else if (strcmp (fstype, "swap") == 0)
                return g_strdup (_("Swap area used by the operating system for virtual memory."));

        else if (strcmp (fstype, "ntfs") == 0)
                return g_strdup (_("The native Windows file system. Not widely compatible with other "
                                   "operating systems than Windows."));

        else if (strcmp (fstype, "empty") == 0)
                return g_strdup (_("No file system will be created."));

        else if (strcmp (fstype, "msdos_extended_partition") == 0)
                return g_strdup (_("Create an Extended Partition for housing logical partitions. Typically "
                                   "used to overcome the inherent limitation of four primary partitions when "
                                   "using the Master Boot Record partitioning scheme."));

        else
                return NULL;
}

static void
gdu_util_fstype_combo_box_update_desc_label (GtkWidget *combo_box)
{
        GtkWidget *desc_label;

        desc_label = g_object_get_data (G_OBJECT (combo_box), "gdu-desc-label");
        if (desc_label != NULL) {
                char *s;
                char *fstype;
                char *desc;

                fstype = gdu_util_fstype_combo_box_get_selected (combo_box);
                if (fstype != NULL) {
                        desc = gdu_util_fstype_get_description (fstype);
                        if (desc != NULL) {
                                s = g_strdup_printf ("<small><i>%s</i></small>", desc);
                                gtk_label_set_markup (GTK_LABEL (desc_label), s);
                                g_free (desc);
                        } else {
                                gtk_label_set_markup (GTK_LABEL (desc_label), "");
                        }
                } else {
                        gtk_label_set_markup (GTK_LABEL (desc_label), "");
                }
                g_free (fstype);
        }
}

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
gdu_util_fstype_combo_box_set_desc_label (GtkWidget *combo_box, GtkWidget *desc_label)
{
        g_object_set_data_full (G_OBJECT (combo_box),
                                "gdu-desc-label",
                                g_object_ref (desc_label),
                                g_object_unref);

        gdu_util_fstype_combo_box_update_desc_label (combo_box);
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

static void
gdu_util_fstype_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        gdu_util_fstype_combo_box_update_desc_label (combo_box);
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

        g_signal_connect (combo_box, "changed", (GCallback) gdu_util_fstype_combo_box_changed, NULL);

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


/* ---------------------------------------------------------------------------------------------------- */

static GtkListStore *
gdu_util_part_type_combo_box_create_store (const char *part_scheme)
{
        int n;
        GtkListStore *store;
        GtkTreeIter iter;

        store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
        if (part_scheme == NULL)
                goto out;

        for (n = 0; part_type[n].scheme != NULL; n++) {
                if (strcmp (part_type[n].scheme, part_scheme) != 0)
                        continue;

                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter,
                                    0, part_type[n].type,
                                    1, part_type[n].name,
                                    -1);
        }

out:
        return store;
}

void
gdu_util_part_type_combo_box_rebuild (GtkWidget  *combo_box, const char *part_scheme)
{
        GtkListStore *store;
        store = gdu_util_part_type_combo_box_create_store (part_scheme);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
        g_object_unref (store);
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
}


/**
 * gdu_util_part_type_combo_box_create:
 * @part_scheme: Partitioning scheme to get partitions types for.
 *
 * Get a combo box with the partition types for a given scheme.
 *
 * Returns: A #GtkComboBox widget
 **/
GtkWidget *
gdu_util_part_type_combo_box_create (const char *part_scheme)
{
        GtkListStore *store;
	GtkCellRenderer *renderer;
        GtkWidget *combo_box;

        combo_box = gtk_combo_box_new ();
        store = gdu_util_part_type_combo_box_create_store (part_scheme);
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
gdu_util_part_type_combo_box_select (GtkWidget *combo_box, const char *part_type)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        gboolean ret;

        ret = FALSE;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
        if (gtk_tree_model_get_iter_first (model, &iter)) {
                do {
                        char *iter_part_type;

                        gtk_tree_model_get (model, &iter, 0, &iter_part_type, -1);
                        if (iter_part_type != NULL && strcmp (part_type, iter_part_type) == 0) {
                                gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo_box), &iter);
                                ret = TRUE;
                        }
                        g_free (iter_part_type);
                } while (!ret && gtk_tree_model_iter_next (model, &iter));
        }

        return ret;
}

char *
gdu_util_part_type_combo_box_get_selected (GtkWidget *combo_box)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        char *part_type;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
        part_type = NULL;
        if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), &iter))
                gtk_tree_model_get (model, &iter, 0, &part_type, -1);

        return part_type;
}

/* ---------------------------------------------------------------------------------------------------- */

static GtkListStore *
gdu_util_part_table_type_combo_box_create_store (void)
{
        GtkListStore *store;
        GtkTreeIter iter;

        store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

        /* TODO: get from daemon */
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            0, "mbr",
                            1, _("Master Boot Record"),
                            -1);
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            0, "gpt",
                            1, _("GUID Partition Table"),
                            -1);
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            0, "apm",
                            1, _("Apple Partition Map"),
                            -1);

        return store;
}

static char *
gdu_util_part_table_type_get_description (char *part_type)
{
        g_return_val_if_fail (part_type != NULL, NULL);

        if (strcmp (part_type, "mbr") == 0)
                return g_strdup (_("The Master Boot Record partitioning scheme is compatible with almost any "
                                   "device or system but has a number of limitations with respect to to disk "
                                   "size and number of partitions."));

        else if (strcmp (part_type, "apm") == 0)
                return g_strdup (_("A legacy partitioning scheme with roots in legacy Apple hardware. "
                                   "Compatible with most Linux systems."));

        else if (strcmp (part_type, "gpt") == 0)
                return g_strdup (_("The GUID partitioning scheme is compatible with most modern systems but "
                                   "may be incompatible with some devices and legacy systems."));

        else
                return NULL;
}

static void
gdu_util_part_table_type_combo_box_update_desc_label (GtkWidget *combo_box)
{
        GtkWidget *desc_label;

        desc_label = g_object_get_data (G_OBJECT (combo_box), "gdu-desc-label");
        if (desc_label != NULL) {
                char *s;
                char *fstype;
                char *desc;

                fstype = gdu_util_part_table_type_combo_box_get_selected (combo_box);
                if (fstype != NULL) {
                        desc = gdu_util_part_table_type_get_description (fstype);
                        if (desc != NULL) {
                                s = g_strdup_printf ("<small><i>%s</i></small>", desc);
                                gtk_label_set_markup (GTK_LABEL (desc_label), s);
                                g_free (desc);
                        } else {
                                gtk_label_set_markup (GTK_LABEL (desc_label), "");
                        }
                } else {
                        gtk_label_set_markup (GTK_LABEL (desc_label), "");
                }
                g_free (fstype);
        }
}

static void
gdu_util_part_table_type_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        gdu_util_part_table_type_combo_box_update_desc_label (combo_box);
}

void
gdu_util_part_table_type_combo_box_set_desc_label (GtkWidget *combo_box, GtkWidget *desc_label)
{
        g_object_set_data_full (G_OBJECT (combo_box),
                                "gdu-desc-label",
                                g_object_ref (desc_label),
                                g_object_unref);

        gdu_util_part_table_type_combo_box_update_desc_label (combo_box);
}

/**
 * gdu_util_part_table_type_combo_box_create:
 *
 * Get a combo box with the partition tables types we can create.
 *
 * Returns: A #GtkComboBox widget
 **/
GtkWidget *
gdu_util_part_table_type_combo_box_create (void)
{
        GtkListStore *store;
	GtkCellRenderer *renderer;
        GtkWidget *combo_box;

        combo_box = gtk_combo_box_new ();
        store = gdu_util_part_table_type_combo_box_create_store ();
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
        g_object_unref (store);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
					"text", 1,
					NULL);

        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

        g_signal_connect (combo_box, "changed", (GCallback) gdu_util_part_table_type_combo_box_changed, NULL);

        return combo_box;
}

gboolean
gdu_util_part_table_type_combo_box_select (GtkWidget *combo_box, const char *part_table_type)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        gboolean ret;

        ret = FALSE;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
        gtk_tree_model_get_iter_first (model, &iter);
        do {
                char *iter_part_table_type;

                gtk_tree_model_get (model, &iter, 0, &iter_part_table_type, -1);
                if (iter_part_table_type != NULL && strcmp (part_table_type, iter_part_table_type) == 0) {
                        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo_box), &iter);
                        ret = TRUE;
                }
                g_free (iter_part_table_type);
        } while (!ret && gtk_tree_model_iter_next (model, &iter));

        return ret;
}

char *
gdu_util_part_table_type_combo_box_get_selected (GtkWidget *combo_box)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        char *part_table_type;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
        part_table_type = NULL;
        if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), &iter))
                gtk_tree_model_get (model, &iter, 0, &part_table_type, -1);

        return part_table_type;
}


/* ---------------------------------------------------------------------------------------------------- */

static char *
gdu_util_secure_erase_get_description (char *secure_erase_type)
{
        g_return_val_if_fail (secure_erase_type != NULL, NULL);

        if (strcmp (secure_erase_type, "none") == 0)
                return g_strdup (_("Data on the disk won't be erased. This option doesn't add any time to the "
                                   "requested operation but can pose a security threat since some data "
                                   "may be recovered."));

        else if (strcmp (secure_erase_type, "full") == 0)
                return g_strdup (_("All data on the disk will by overwritten by zeroes providing some "
                                   "security against data recovery. This operation may take a long time "
                                   "depending on the size and speed of the disk."));

        else if (strcmp (secure_erase_type, "full3pass") == 0)
                return g_strdup (_("Random data is written to the disk three times. This operation may "
                                   "take some time depending on the size and speed of the disk but "
                                   "provides good security against data recovery."));

        else if (strcmp (secure_erase_type, "full7pass") == 0)
                return g_strdup (_("Random data is written to the disk seven times. This operation may "
                                   "take a very long time depending on the size and speed of the disk "
                                   "but provides excellent security against data recovery."));

        else if (strcmp (secure_erase_type, "full35pass") == 0)
                return g_strdup (_("Random data is written to the disk 35 times before starting the "
                                   "operation. This operation may take extremely long time depending on the "
                                   "size and speed of the disk but provides the most effective "
                                   "security against data recovery."));

        else
                return NULL;
}

static void
gdu_util_secure_erase_combo_box_update_desc_label (GtkWidget *combo_box)
{
        GtkWidget *desc_label;

        desc_label = g_object_get_data (G_OBJECT (combo_box), "gdu-desc-label");
        if (desc_label != NULL) {
                char *s;
                char *secure_erase;
                char *secure_erase_desc;

                secure_erase = gdu_util_secure_erase_combo_box_get_selected (combo_box);
                secure_erase_desc = gdu_util_secure_erase_get_description (secure_erase);
                s = g_strdup_printf ("<small><i>%s</i></small>", secure_erase_desc);
                gtk_label_set_markup (GTK_LABEL (desc_label), s);
                g_free (s);
                g_free (secure_erase_desc);
                g_free (secure_erase);
        }
}

static void
gdu_util_secure_erase_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        gdu_util_secure_erase_combo_box_update_desc_label (combo_box);
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

        g_signal_connect (combo_box, "changed", (GCallback) gdu_util_secure_erase_combo_box_changed, NULL);
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

void
gdu_util_secure_erase_combo_box_set_desc_label (GtkWidget *combo_box, GtkWidget *desc_label)
{
        g_object_set_data_full (G_OBJECT (combo_box),
                                "gdu-desc-label",
                                g_object_ref (desc_label),
                                g_object_unref);

        gdu_util_secure_erase_combo_box_update_desc_label (combo_box);
}

/* ---------------------------------------------------------------------------------------------------- */

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
                } else if (strcmp (fstype, "swap") == 0) {
                        type = "0x82";
                } else if (strcmp (fstype, "ntfs") == 0) {
                        type = "0x07";
                } else {
                        type = "0x83";
                }
        } else if (strcmp (scheme, "gpt") == 0) {
                /* default to Basic Data Partition for now */
                type = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7";
        } else if (strcmp (scheme, "apm") == 0) {
                type = "Windows_FAT_32";
        }

        return g_strdup (type);
}

/* ---------------------------------------------------------------------------------------------------- */

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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
        gboolean is_new_password;
        GtkWidget *password_entry;
        GtkWidget *password_entry_new;
        GtkWidget *password_entry_verify;
        GtkWidget *warning_hbox;
        GtkWidget *warning_label;
        GtkWidget *button;
} DialogSecretData;

static void
gdu_util_dialog_secret_update (DialogSecretData *data)
{
        if (strcmp (gtk_entry_get_text (GTK_ENTRY (data->password_entry_new)),
                    gtk_entry_get_text (GTK_ENTRY (data->password_entry_verify))) != 0) {
                gtk_widget_show (data->warning_hbox);
                gtk_label_set_markup (GTK_LABEL (data->warning_label), "<i>Passphrases do not match</i>");
                gtk_widget_set_sensitive (data->button, FALSE);
        } else if (!data->is_new_password &&
                   (strlen (gtk_entry_get_text (GTK_ENTRY (data->password_entry))) > 0 ||
                    strlen (gtk_entry_get_text (GTK_ENTRY (data->password_entry_new))) > 0) &&
                   strcmp (gtk_entry_get_text (GTK_ENTRY (data->password_entry)),
                           gtk_entry_get_text (GTK_ENTRY (data->password_entry_new))) == 0) {
                gtk_widget_show (data->warning_hbox);
                gtk_label_set_markup (GTK_LABEL (data->warning_label), "<i>Passphrases do not differ</i>");
                gtk_widget_set_sensitive (data->button, FALSE);
        } else {
                gtk_widget_hide (data->warning_hbox);
                gtk_widget_set_sensitive (data->button, TRUE);
        }
}

static void
gdu_util_dialog_secret_entry_changed (GtkWidget *entry, gpointer user_data)
{
        DialogSecretData *data = user_data;
        gdu_util_dialog_secret_update (data);
}

static char *
gdu_util_dialog_secret_internal (GtkWidget *parent_window,
                                 gboolean is_new_password,
                                 gboolean is_change_password,
                                 const char *old_secret_for_change_password,
                                 char **old_secret_from_dialog,
                                 gboolean *save_in_keyring,
                                 gboolean *save_in_keyring_session)
{
        int row;
        int response;
        char *secret;
        GtkWidget *dialog;
        GtkWidget *image;
	GtkWidget *hbox;
        GtkWidget *main_vbox;
        GtkWidget *vbox;
        GtkWidget *label;
        GtkWidget *table_alignment;
        GtkWidget *table;
        GtkWidget *never_radio_button;
        GtkWidget *session_radio_button;
        GtkWidget *always_radio_button;
        DialogSecretData *data;
        const char *title;

        g_return_val_if_fail (parent_window == NULL || GTK_IS_WINDOW (parent_window), NULL);

        session_radio_button = NULL;
        always_radio_button = NULL;

        secret = NULL;
        data = g_new0 (DialogSecretData, 1);
        data->is_new_password = is_new_password;

        if (is_new_password)
                title = _("Enter Passphrase");
        else if (is_change_password)
                title = _("Change Passphrase");
        else
                title = _("Unlock Encrypted Device");
        dialog = gtk_dialog_new_with_buttons (title,
                                              GTK_WINDOW (parent_window),
                                              GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
                                              GTK_STOCK_CANCEL,
                                              GTK_RESPONSE_CANCEL,
                                              NULL);

        if (is_new_password) {
                data->button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Cr_eate"), 0);
        } else if (is_change_password) {
                data->button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Change _Passphrase"), 0);
        } else {
                data->button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Unlock"), 0);
        }
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), 0);

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_icon_name (GTK_WINDOW (dialog), GTK_STOCK_DIALOG_AUTHENTICATION);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	main_vbox = gtk_vbox_new (FALSE, 10);
	gtk_box_pack_start (GTK_BOX (hbox), main_vbox, TRUE, TRUE, 0);

	/* main message */
	label = gtk_label_new (NULL);
        if (is_new_password) {
                gtk_label_set_markup (GTK_LABEL (label),
                                      "<b><big>To create an encrypted device, choose a passphrase.</big></b>");
        } else if (is_change_password) {
                gtk_label_set_markup (GTK_LABEL (label),
                                      "<b><big>To change the passphrase, enter both the current and new passphrase.</big></b>");
        } else {
                gtk_label_set_markup (GTK_LABEL (label),
                                      "<b><big>To unlock the data, enter the passphrase for the device.</big></b>");
        }
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

	/* secondary message */
	label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("Data on this device is stored in an encrypted form "
                                                   "protected by a passphrase."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

	/* password entry */
	vbox = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (main_vbox), vbox, FALSE, FALSE, 0);

	table_alignment = gtk_alignment_new (0.0, 0.0, 1.0, 1.0);
	gtk_box_pack_start (GTK_BOX (vbox), table_alignment, FALSE, FALSE, 0);
	table = gtk_table_new (1, 2, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 12);
	gtk_table_set_row_spacings (GTK_TABLE (table), 6);
	gtk_container_add (GTK_CONTAINER (table_alignment), table);

        row = 0;

        if (is_change_password || is_new_password) {

                if (is_change_password) {
                        label = gtk_label_new_with_mnemonic (_("C_urrent Passphrase:"));
                        gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
                        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
                        gtk_table_attach (GTK_TABLE (table), label,
                                          0, 1, row, row + 1,
                                          GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
                        data->password_entry = gtk_entry_new ();
                        gtk_entry_set_visibility (GTK_ENTRY (data->password_entry), FALSE);
                        gtk_table_attach_defaults (GTK_TABLE (table), data->password_entry, 1, 2, row, row + 1);
                        gtk_label_set_mnemonic_widget (GTK_LABEL (label), data->password_entry);

                        row++;
                }

                label = gtk_label_new_with_mnemonic (_("_New Passphrase:"));
                gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
                gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
                gtk_table_attach (GTK_TABLE (table), label,
                                  0, 1, row, row + 1,
                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
                data->password_entry_new = gtk_entry_new ();
                gtk_entry_set_visibility (GTK_ENTRY (data->password_entry_new), FALSE);
                gtk_table_attach_defaults (GTK_TABLE (table), data->password_entry_new, 1, 2, row, row + 1);
                gtk_label_set_mnemonic_widget (GTK_LABEL (label), data->password_entry_new);

                row++;

                label = gtk_label_new_with_mnemonic (_("_Verify Passphrase:"));
                gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
                gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
                gtk_table_attach (GTK_TABLE (table), label,
                                  0, 1, row, row + 1,
                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
                data->password_entry_verify = gtk_entry_new ();
                gtk_entry_set_visibility (GTK_ENTRY (data->password_entry_verify), FALSE);
                gtk_table_attach_defaults (GTK_TABLE (table), data->password_entry_verify, 1, 2, row, row + 1);
                gtk_label_set_mnemonic_widget (GTK_LABEL (label), data->password_entry_verify);

                data->warning_hbox = gtk_hbox_new (FALSE, 12);
                image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_MENU);
                data->warning_label = gtk_label_new (NULL);
                gtk_box_pack_start (GTK_BOX (data->warning_hbox), image, FALSE, FALSE, 0);
                gtk_box_pack_start (GTK_BOX (data->warning_hbox), data->warning_label, FALSE, FALSE, 0);
                gtk_box_pack_start (GTK_BOX (vbox), data->warning_hbox, FALSE, FALSE, 0);

                g_signal_connect (data->password_entry_new, "changed",
                                  (GCallback) gdu_util_dialog_secret_entry_changed, data);
                g_signal_connect (data->password_entry_verify, "changed",
                                  (GCallback) gdu_util_dialog_secret_entry_changed, data);

                /* only the verify entry activates the default action */
                gtk_entry_set_activates_default (GTK_ENTRY (data->password_entry_verify), TRUE);

                /* if the old password is supplied (from e.g. the keyring), set it and focus on the new password */
                if (old_secret_for_change_password != NULL) {
                        gtk_entry_set_text (GTK_ENTRY (data->password_entry), old_secret_for_change_password);
                        gtk_widget_grab_focus (data->password_entry_new);
                } else if (is_new_password) {
                        gtk_widget_grab_focus (data->password_entry_new);
                }

        } else {
                label = gtk_label_new_with_mnemonic (_("_Passphrase:"));
                gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
                gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
                gtk_table_attach (GTK_TABLE (table), label,
                                  0, 1, row, row + 1,
                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
                data->password_entry = gtk_entry_new ();
                gtk_entry_set_visibility (GTK_ENTRY (data->password_entry), FALSE);
                gtk_entry_set_activates_default (GTK_ENTRY (data->password_entry), TRUE);
                gtk_table_attach_defaults (GTK_TABLE (table), data->password_entry, 1, 2, row, row + 1);
                gtk_label_set_mnemonic_widget (GTK_LABEL (label), data->password_entry);
        }

        never_radio_button = gtk_radio_button_new_with_mnemonic (
                NULL,
                _("_Forget passphrase immediately"));
        session_radio_button = gtk_radio_button_new_with_mnemonic_from_widget (
                GTK_RADIO_BUTTON (never_radio_button),
                _("Remember passphrase until you _log out"));
        always_radio_button = gtk_radio_button_new_with_mnemonic_from_widget (
                GTK_RADIO_BUTTON (never_radio_button),
                _("_Remember forever"));

        /* preselect Remember Forever if we've retrieved the existing key from the keyring */
        if (is_change_password && old_secret_for_change_password != NULL) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (always_radio_button), TRUE);
        }

        gtk_box_pack_start (GTK_BOX (vbox), never_radio_button, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), session_radio_button, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), always_radio_button, FALSE, FALSE, 0);

        gtk_widget_show_all (dialog);
        if (is_change_password || is_new_password)
                gdu_util_dialog_secret_update (data);
        response = gtk_dialog_run (GTK_DIALOG (dialog));
        if (response != 0)
                goto out;

        if (is_new_password) {
                secret = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->password_entry_new)));
        } else if (is_change_password) {
                *old_secret_from_dialog = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->password_entry)));
                secret = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->password_entry_new)));
        } else {
                secret = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->password_entry)));
        }

        if (save_in_keyring != NULL)
                *save_in_keyring = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (always_radio_button));
        if (save_in_keyring_session != NULL)
                *save_in_keyring_session = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (session_radio_button));

out:
        if (data == NULL)
                g_free (data);
        if (dialog != NULL)
                gtk_widget_destroy (dialog);
        return secret;
}

static GnomeKeyringPasswordSchema encrypted_device_password_schema = {
        GNOME_KEYRING_ITEM_GENERIC_SECRET,
        {
                { "encrypted-device-uuid", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
                { NULL, 0 }
        }
};

char *
gdu_util_dialog_ask_for_new_secret (GtkWidget      *parent_window,
                                    gboolean       *save_in_keyring,
                                    gboolean       *save_in_keyring_session)
{
        return gdu_util_dialog_secret_internal (parent_window,
                                                TRUE,
                                                FALSE,
                                                NULL,
                                                NULL,
                                                save_in_keyring,
                                                save_in_keyring_session);
}

/**
 * gdu_util_dialog_ask_for_secret:
 * @parent_window: Parent window that dialog will be transient for or #NULL.
 * @device: A #GduDevice that is an encrypted device.
 * @bypass_keyring: Set to #TRUE to bypass the keyring.
 *
 * Retrieves a secret from the user or the keyring (unless
 * @bypass_keyring is set to #TRUE).
 *
 * Returns: the secret or #NULL if the user cancelled the dialog.
 **/
char *
gdu_util_dialog_ask_for_secret (GtkWidget *parent_window,
                                GduDevice *device,
                                gboolean bypass_keyring)
{
        char *secret;
        char *password;
        const char *usage;
        const char *uuid;
        gboolean save_in_keyring;
        gboolean save_in_keyring_session;

        secret = NULL;
        save_in_keyring = FALSE;
        save_in_keyring_session = FALSE;

        usage = gdu_device_id_get_usage (device);
        uuid = gdu_device_id_get_uuid (device);

        if (strcmp (usage, "crypto") != 0) {
                g_warning ("%s: device is not a crypto device", __FUNCTION__);
                goto out;
        }

        if (uuid == NULL || strlen (uuid) == 0) {
                g_warning ("%s: device has no UUID", __FUNCTION__);
                goto out;
        }

        if (!bypass_keyring) {
                if (gnome_keyring_find_password_sync (&encrypted_device_password_schema,
                                                      &password,
                                                      "encrypted-device-uuid", uuid,
                                                      NULL) == GNOME_KEYRING_RESULT_OK) {
                        /* By contract, the caller is responsible for scrubbing the password
                         * so dupping the string into pageable memory "fine". Or not?
                         */
                        secret = g_strdup (password);
                        gnome_keyring_free_password (password);
                        goto out;
                }
        }

        secret = gdu_util_dialog_secret_internal (parent_window,
                                                  FALSE,
                                                  FALSE,
                                                  NULL,
                                                  NULL,
                                                  &save_in_keyring,
                                                  &save_in_keyring_session);

        if (secret != NULL && (save_in_keyring || save_in_keyring_session)) {
                const char *keyring;

                keyring = NULL;
                if (save_in_keyring_session)
                        keyring = GNOME_KEYRING_SESSION;

                if (gnome_keyring_store_password_sync (&encrypted_device_password_schema,
                                                       keyring,
                                                       _("Encrypted Disk Passphrase"),
                                                       secret,
                                                       "encrypted-device-uuid", uuid,
                                                       NULL) != GNOME_KEYRING_RESULT_OK) {
                        g_warning ("%s: couldn't store passphrase in keyring", __FUNCTION__);
                }
        }

out:
        return secret;
}

/**
 * gdu_util_dialog_change_secret:
 * @parent_window: Parent window that dialog will be transient for or #NULL.
 * @device: A #GduDevice that is an encrypted device.
 * @old_secret: Return location for old secret.
 * @new_secret: Return location for new secret.
 * @save_in_keyring: Return location for whether the new secret should be saved in the keyring.
 * @save_in_keyring_session: Return location for whether the new secret should be saved in the session keyring.
 * @bypass_keyring: Set to #TRUE to bypass the keyring.
 *
 * Asks the user to change his secret. The secret in the keyring is
 * not updated; that needs to be done manually using the functions
 * gdu_util_delete_secret() and gdu_util_save_secret().
 *
 * Returns: #TRUE if the user agreed to change the secret.
 **/
gboolean
gdu_util_dialog_change_secret (GtkWidget *parent_window,
                               GduDevice *device,
                               char **old_secret,
                               char **new_secret,
                               gboolean *save_in_keyring,
                               gboolean *save_in_keyring_session,
                               gboolean bypass_keyring)
{
        char *password;
        const char *usage;
        const char *uuid;
        gboolean ret;
        char *old_secret_from_keyring;

        *old_secret = NULL;
        *new_secret = NULL;
        old_secret_from_keyring = NULL;
        ret = FALSE;

        usage = gdu_device_id_get_usage (device);
        uuid = gdu_device_id_get_uuid (device);

        if (strcmp (usage, "crypto") != 0) {
                g_warning ("%s: device is not a crypto device", __FUNCTION__);
                goto out;
        }

        if (uuid == NULL || strlen (uuid) == 0) {
                g_warning ("%s: device has no UUID", __FUNCTION__);
                goto out;
        }

        if (!bypass_keyring) {
                if (gnome_keyring_find_password_sync (&encrypted_device_password_schema,
                                                      &password,
                                                      "encrypted-device-uuid", uuid,
                                                      NULL) == GNOME_KEYRING_RESULT_OK) {
                        /* By contract, the caller is responsible for scrubbing the password
                         * so dupping the string into pageable memory "fine". Or not?
                         */
                        old_secret_from_keyring = g_strdup (password);
                        gnome_keyring_free_password (password);
                }
        }

        *new_secret = gdu_util_dialog_secret_internal (parent_window,
                                                       FALSE,
                                                       TRUE,
                                                       old_secret_from_keyring,
                                                       old_secret,
                                                       save_in_keyring,
                                                       save_in_keyring_session);

        if (old_secret_from_keyring != NULL) {
                memset (old_secret_from_keyring, '\0', strlen (old_secret_from_keyring));
                g_free (old_secret_from_keyring);
        }

        if (*new_secret == NULL)
                goto out;

        ret = TRUE;

out:
        if (!ret) {
                if (*old_secret != NULL) {
                        memset (*old_secret, '\0', strlen (*old_secret));
                        g_free (*old_secret);
                        *old_secret = NULL;
                }
                if (*new_secret != NULL) {
                        memset (*new_secret, '\0', strlen (*new_secret));
                        g_free (*new_secret);
                        *new_secret = NULL;
                }
        }

        return ret;
}

gboolean
gdu_util_have_secret (GduDevice *device)
{
        const char *usage;
        const char *uuid;
        char *password;
        gboolean ret;

        ret = FALSE;

        usage = gdu_device_id_get_usage (device);
        uuid = gdu_device_id_get_uuid (device);

        if (strcmp (usage, "crypto") != 0) {
                g_warning ("%s: device is not a crypto device", __FUNCTION__);
                goto out;
        }

        if (uuid == NULL || strlen (uuid) == 0) {
                g_warning ("%s: device has no UUID", __FUNCTION__);
                goto out;
        }

        if (!gnome_keyring_find_password_sync (&encrypted_device_password_schema,
                                               &password,
                                               "encrypted-device-uuid", uuid,
                                               NULL) == GNOME_KEYRING_RESULT_OK)
                goto out;

        ret = TRUE;
        gnome_keyring_free_password (password);

out:
        return ret;
}

gboolean
gdu_util_delete_secret (GduDevice *device)
{
        const char *usage;
        const char *uuid;
        gboolean ret;

        ret = FALSE;

        usage = gdu_device_id_get_usage (device);
        uuid = gdu_device_id_get_uuid (device);

        if (strcmp (usage, "crypto") != 0) {
                g_warning ("%s: device is not a crypto device", __FUNCTION__);
                goto out;
        }

        if (uuid == NULL || strlen (uuid) == 0) {
                g_warning ("%s: device has no UUID", __FUNCTION__);
                goto out;
        }

        ret = gnome_keyring_delete_password_sync (&encrypted_device_password_schema,
                                                  "encrypted-device-uuid", uuid,
                                                  NULL) == GNOME_KEYRING_RESULT_OK;

out:
        return ret;
}

gboolean
gdu_util_save_secret (GduDevice      *device,
                      const char     *secret,
                      gboolean        save_in_keyring_session)
{
        const char *keyring;
        const char *usage;
        const char *uuid;
        gboolean ret;

        ret = FALSE;

        usage = gdu_device_id_get_usage (device);
        uuid = gdu_device_id_get_uuid (device);

        if (strcmp (usage, "crypto") != 0) {
                g_warning ("%s: device is not a crypto device", __FUNCTION__);
                goto out;
        }

        if (uuid == NULL || strlen (uuid) == 0) {
                g_warning ("%s: device has no UUID", __FUNCTION__);
                goto out;
        }

        keyring = NULL;
        if (save_in_keyring_session)
                keyring = GNOME_KEYRING_SESSION;

        if (gnome_keyring_store_password_sync (&encrypted_device_password_schema,
                                               keyring,
                                               _("Encrypted Disk Passphrase"),
                                               secret,
                                               "encrypted-device-uuid", uuid,
                                               NULL) != GNOME_KEYRING_RESULT_OK) {
                g_warning ("%s: couldn't store passphrase in keyring", __FUNCTION__);
                goto out;
        }

        ret = TRUE;

out:
        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

char *
gdu_util_get_speed_for_display (guint64 speed)
{
        char *str;
        gdouble displayed_speed;

        if (speed < 1000 * 1000) {
                displayed_speed = (double) speed / 1000.0;
                str = g_strdup_printf (_("%.1f kbit/s"), displayed_speed);
        } else if (speed < 1000 * 1000 * 1000) {
                displayed_speed = (double) speed / 1000.0 / 1000.0;
                str = g_strdup_printf (_("%.1f Mbit/s"), displayed_speed);
        } else {
                displayed_speed = (double) speed / 1000.0 / 1000.0 / 1000.0;
                str = g_strdup_printf (_("%.1f Gbit/s"), displayed_speed);
        }

        return str;
}

char *
gdu_util_get_connection_for_display (const char *connection_interface, guint64 connection_speed)
{
        const char *name;
        char *result;

        name = NULL;
        if (connection_interface != NULL) {
                if (strcmp (connection_interface, "ata_serial") == 0) {
                        name = _("SATA");
                } else if (strcmp (connection_interface, "ata_serial_esata") == 0) {
                        name = _("eSATA");
                } else if (strcmp (connection_interface, "ata_parallel") == 0) {
                        name = _("PATA");
                } else if (g_str_has_prefix (connection_interface, "ata")) {
                        name = _("ATA");
                } else if (g_str_has_prefix (connection_interface, "scsi")) {
                        name = _("SCSI");
                } else if (strcmp (connection_interface, "usb") == 0) {
                        name = _("USB");
                } else if (strcmp (connection_interface, "firewire") == 0) {
                        name = _("Firewire");
                }
        }

        if (name == NULL)
                name = _("Unknown");

        if (connection_speed > 0) {
                char *speed;

                speed = gdu_util_get_speed_for_display (connection_speed);
                result = g_strdup_printf ("%s @ %s", name, speed);
                g_free (speed);
        } else {
                result = g_strdup (name);
        }

        return result;
}
