/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-util.c
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
#include <glib-object.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gnome-keyring.h>
#include <dbus/dbus-glib.h>

#include "gdu-util.h"
#include "gdu-pool.h"
#include "gdu-device.h"

#define KILOBYTE_FACTOR 1000.0
#define MEGABYTE_FACTOR (1000.0 * 1000.0)
#define GIGABYTE_FACTOR (1000.0 * 1000.0 * 1000.0)

#define KIBIBYTE_FACTOR 1024.0
#define MEBIBYTE_FACTOR (1024.0 * 1024.0)
#define GIBIBYTE_FACTOR (1024.0 * 1024.0 * 1024.0)

static char *
get_pow2_size (guint64 size)
{
        gchar *str;
        gdouble displayed_size;
        const gchar *unit;
        guint digits;

        if (size < MEBIBYTE_FACTOR) {
                displayed_size = (double) size / KIBIBYTE_FACTOR;
                unit = "KiB";
        } else if (size < GIBIBYTE_FACTOR) {
                displayed_size = (double) size / MEBIBYTE_FACTOR;
                unit = "MiB";
        } else {
                displayed_size = (double) size / GIBIBYTE_FACTOR;
                unit = "GiB";
        }

        if (displayed_size < 10.0)
                digits = 1;
        else
                digits = 0;

        str = g_strdup_printf ("%.*f %s", digits, displayed_size, unit);

        return str;
}

static char *
get_pow10_size (guint64 size)
{
        gchar *str;
        gdouble displayed_size;
        const gchar *unit;
        guint digits;

        if (size < MEGABYTE_FACTOR) {
                displayed_size = (double) size / KILOBYTE_FACTOR;
                unit = "KB";
        } else if (size < GIGABYTE_FACTOR) {
                displayed_size = (double) size / MEGABYTE_FACTOR;
                unit = "MB";
        } else {
                displayed_size = (double) size / GIGABYTE_FACTOR;
                unit = "GB";
        }

        if (displayed_size < 10.0)
                digits = 1;
        else
                digits = 0;

        str = g_strdup_printf ("%.*f %s", digits, displayed_size, unit);

        return str;
}


char *
gdu_util_get_size_for_display (guint64 size, gboolean long_string)
{
        char *str;

        if (long_string) {
                char *pow2_str;
                char *pow10_str;
                char *size_str;

                pow2_str = get_pow2_size (size);
                pow10_str = get_pow10_size (size);
                size_str = g_strdup_printf ("%'" G_GINT64_FORMAT, size);

                /* Translators: The first %s is the size in power-of-10 units, e.g. 100 KB
                 * the second %s is the size in power-of-2 units, e.g. 20 MiB
                 * the third %s is the size as a number
                 */
                str = g_strdup_printf (_("%s / %s / %s bytes"), pow10_str, pow2_str, size_str);

                g_free (size_str);
                g_free (pow10_str);
                g_free (pow2_str);
        } else {
                str = get_pow10_size (size);
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
                                /* Translators: FAT is a filesystem type */
                                s = g_strdup (_("FAT (12-bit version)"));
                        } else {
                                /* Translators: FAT is a filesystem type */
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT16") == 0) {
                        if (long_string) {
                                /* Translators: FAT is a filesystem type */
                                s = g_strdup (_("FAT (16-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT32") == 0) {
                        if (long_string) {
                                /* Translators: FAT is a filesystem type */
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
                                /* Translators: NTFS is a filesystem type */
                                s = g_strdup_printf (_("NTFS (version %s)"), fsversion);
                        else
                                /* Translators: NTFS is a filesystem type */
                                s = g_strdup_printf (_("NTFS"));
                } else {
                        s = g_strdup (_("NTFS"));
                }
        } else if (strcmp (fstype, "hfs") == 0) {
                if (long_string) {
                        /* Translators: HFS is a filesystem type */
                        s = g_strdup (_("HFS"));
                } else {
                        s = g_strdup (_("HFS"));
                }
        } else if (strcmp (fstype, "hfsplus") == 0) {
                if (long_string) {
                        /* Translators: HFS+ is a filesystem type */
                        s = g_strdup (_("HFS+"));
                } else {
                        s = g_strdup (_("HFS+"));
                }
        } else if (strcmp (fstype, "crypto_LUKS") == 0) {
                if (long_string) {
                        s = g_strdup (_("Linux Unified Key Setup"));
                } else {
                        /* Translators: LUKS is 'Linux Unified Key Setup', a disk encryption format */
                        s = g_strdup (_("LUKS"));
                }
        } else if (strcmp (fstype, "ext2") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                /* Translators: Ext2 is a filesystem type */
                                s = g_strdup_printf (_("Linux Ext2 (version %s)"), fsversion);
                        else
                                /* Translators: Ext2 is a filesystem type */
                                s = g_strdup_printf (_("Linux Ext2"));
                } else {
                        /* Translators: Ext2 is a filesystem type */
                        s = g_strdup (_("ext2"));
                }
        } else if (strcmp (fstype, "ext3") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                /* Translators: Ext3 is a filesystem type */
                                s = g_strdup_printf (_("Linux Ext3 (version %s)"), fsversion);
                        else
                                /* Translators: Ext3 is a filesystem type */
                                s = g_strdup_printf (_("Linux Ext3"));
                } else {
                        /* Translators: Ext3 is a filesystem type */
                        s = g_strdup (_("ext3"));
                }
        } else if (strcmp (fstype, "jbd") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                /* Translators: 'Journal' refers to a filesystem technology here, see 'journaling filesystems' */
                                s = g_strdup_printf (_("Journal for Linux ext3 (version %s)"), fsversion);
                        else
                                /* Translators: 'Journal' refers to a filesystem technology here, see 'journaling filesystems' */
                                s = g_strdup_printf (_("Journal for Linux ext3"));
                } else {
                        /* Translators: jbd is a filesystem type */
                        s = g_strdup (_("jbd"));
                }
        } else if (strcmp (fstype, "ext4") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                /* Translators: ext4 is a filesystem type */
                                s = g_strdup_printf (_("Linux Ext4 (version %s)"), fsversion);
                        else
                                /* Translators: ext4 is a filesystem type */
                                s = g_strdup_printf (_("Linux Ext4"));
                } else {
                        /* Translators: Ext4 is a filesystem type */
                        s = g_strdup (_("ext4"));
                }
        } else if (strcmp (fstype, "xfs") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                /* Translators: xfs is a filesystem type */
                                s = g_strdup_printf (_("Linux XFS (version %s)"), fsversion);
                        else
                                /* Translators: xfs is a filesystem type */
                                s = g_strdup_printf (_("Linux XFS"));
                } else {
                        /* Translators: xfs is a filesystem type */
                        s = g_strdup (_("xfs"));
                }
        } else if (strcmp (fstype, "iso9660") == 0) {
                if (long_string) {
                        /* Translators: iso9660 is a filesystem type */
                        s = g_strdup (_("ISO 9660"));
                } else {
                        /* Translators: iso9660 is a filesystem type */
                        s = g_strdup (_("iso9660"));
                }
        } else if (strcmp (fstype, "udf") == 0) {
                if (long_string) {
                        /* Translators: udf is a filesystem type */
                        s = g_strdup (_("Universal Disk Format"));
                } else {
                        /* Translators: udf is a filesystem type */
                        s = g_strdup (_("udf"));
                }
        } else if (strcmp (fstype, "swap") == 0) {
                if (long_string) {
                        s = g_strdup (_("Swap Space"));
                } else {
                        /* Translators: filesystem type for swap space */
                        s = g_strdup (_("swap"));
                }
        } else if (strcmp (fstype, "LVM2_member") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                s = g_strdup_printf (_("LVM2 Physical Volume (version %s)"), fsversion);
                        else
                                s = g_strdup_printf (_("LVM2 Physical Volume"));
                } else {
                        /* Translators: short name for LVM2 Physical Volume */
                        s = g_strdup (_("lvm2_pv"));
                }

        } else if (strcmp (fstype, "linux_raid_member") == 0) {
                if (long_string) {
                        if (strlen (fsversion) > 0)
                                s = g_strdup_printf (_("RAID Component (version %s)"), fsversion);
                        else
                                s = g_strdup_printf (_("RAID Component"));
                } else {
                        /* Translators: short name for 'RAID Component' */
                        s = g_strdup (_("raid"));
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
        if (strcmp (job_id, "FilesystemCreate") == 0) {
                s = g_strdup (_("Creating File System"));
        } else if (strcmp (job_id, "FilesystemMount") == 0) {
                s = g_strdup (_("Mounting File System"));
        } else if (strcmp (job_id, "FilesystemUnmount") == 0) {
                s = g_strdup (_("Unmounting File System"));
        } else if (strcmp (job_id, "FilesystemCheck") == 0) {
                s = g_strdup (_("Checking File System"));
        } else if (strcmp (job_id, "LuksFormat") == 0) {
                s = g_strdup (_("Creating LUKS Device"));
        } else if (strcmp (job_id, "LuksUnlock") == 0) {
                s = g_strdup (_("Unlocking LUKS Device"));
        } else if (strcmp (job_id, "LuksLock") == 0) {
                s = g_strdup (_("Locking LUKS Device"));
        } else if (strcmp (job_id, "PartitionTableCreate") == 0) {
                s = g_strdup (_("Creating Partition Table"));
        } else if (strcmp (job_id, "PartitionDelete") == 0) {
                s = g_strdup (_("Deleting Partition"));
        } else if (strcmp (job_id, "PartitionCreate") == 0) {
                s = g_strdup (_("Creating Partition"));
        } else if (strcmp (job_id, "PartitionModify") == 0) {
                s = g_strdup (_("Modifying Partition"));
        } else if (strcmp (job_id, "FilesystemSetLabel") == 0) {
                s = g_strdup (_("Setting Label for Device"));
        } else if (strcmp (job_id, "LuksChangePassphrase") == 0) {
                s = g_strdup (_("Changing Passphrase for Encrypted LUKS Device"));
        } else if (strcmp (job_id, "LinuxMdAddComponent") == 0) {
                s = g_strdup (_("Adding Component to RAID Array"));
        } else if (strcmp (job_id, "LinuxMdRemoveComponent") == 0) {
                s = g_strdup (_("Removing Component from RAID Array"));
        } else if (strcmp (job_id, "LinuxMdStop") == 0) {
                s = g_strdup (_("Stopping RAID Array"));
        } else if (strcmp (job_id, "LinuxMdStart") == 0) {
                s = g_strdup (_("Starting RAID Array"));
        } else if (strcmp (job_id, "LinuxMdCheck") == 0) {
                s = g_strdup (_("Checking RAID Array"));
        } else if (strcmp (job_id, "LinuxMdRepair") == 0) {
                s = g_strdup (_("Repairing RAID Array"));
        } else if (strcmp (job_id, "DriveAtaSmartSelftestShort") == 0) {
                s = g_strdup (_("Running Short SMART Self-Test"));
        } else if (strcmp (job_id, "DriveAtaSmartSelftestExtended") == 0) {
                s = g_strdup (_("Running Extended SMART Self-Test"));
        } else if (strcmp (job_id, "DriveAtaSmartSelftestConveyance") == 0) {
                s = g_strdup (_("Running Conveyance SMART Self-Test"));
        } else if (strcmp (job_id, "DriveEject") == 0) {
                s = g_strdup (_("Ejecting Media"));
        } else if (strcmp (job_id, "DriveDetach") == 0) {
                s = g_strdup (_("Detaching Device"));
        } else if (strcmp (job_id, "ForceUnmount") == 0) {
                s = g_strdup (_("Forcibly Unmounting Filesystem"));
        } else if (strcmp (job_id, "ForceLuksTeardown") == 0) {
                s = g_strdup (_("Forcibly Locking LUKS device"));
        } else {
                s = g_strdup_printf ("%s", job_id);
                g_warning ("No friendly string for job with id '%s'", job_id);
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

void
gdu_util_part_type_foreach (GduUtilPartTypeForeachFunc callback, gpointer user_data)
{
        int n;
        for (n = 0; part_type[n].scheme != NULL; n++) {
                callback (part_type[n].scheme, part_type[n].type, part_type[n].name, user_data);
        }
}

char *
gdu_util_get_desc_for_part_type (const char *scheme, const char *type)
{
        int n;

        for (n = 0; part_type[n].name != NULL; n++) {
                if (g_ascii_strcasecmp (part_type[n].type, type) == 0)
                        return g_strdup (part_type[n].name);
        }

        /* Translators: Shown for unknown partition types.
         * %s is the partition type name
         */
        return g_strdup_printf (_("Unknown (%s)"), type);
}


char *
gdu_util_fstype_get_description (char *fstype)
{
        g_return_val_if_fail (fstype != NULL, NULL);

        if (strcmp (fstype, "vfat") == 0)
                return g_strdup (_("A popular format compatible with almost any device or system, typically "
                                   "used for file exchange."));

        else if (strcmp (fstype, "ext2") == 0)
                return g_strdup (_("This file system is compatible with Linux systems only and provides classic "
                                   "UNIX file permissions support. This file system does not use a journal."));

        else if (strcmp (fstype, "ext3") == 0 ||
                 strcmp (fstype, "ext4") == 0 ||
                 strcmp (fstype, "xfs") == 0)
                return g_strdup (_("This file system is compatible with Linux systems only and provides classic "
                                   "UNIX file permissions support."));

        else if (strcmp (fstype, "swap") == 0)
                return g_strdup (_("Swap area used by the operating system for virtual memory."));

        else if (strcmp (fstype, "ntfs") == 0)
                return g_strdup (_("The native Windows file system. Not widely compatible with other "
                                   "operating systems than Windows."));

        else if (strcmp (fstype, "empty") == 0)
                return g_strdup (_("No file system will be created."));

        else if (strcmp (fstype, "msdos_extended_partition") == 0)
                return g_strdup (_("Create an Extended Partition for logical partitions."));

        else
                return NULL;
}


char *
gdu_util_part_table_type_get_description (char *part_type)
{
        g_return_val_if_fail (part_type != NULL, NULL);

        if (strcmp (part_type, "mbr") == 0)
                return g_strdup (_("The Master Boot Record scheme is compatible with almost any "
                                   "device or system but has a number of limitations with respect to disk "
                                   "size and number of partitions."));

        else if (strcmp (part_type, "apm") == 0)
                return g_strdup (_("A legacy scheme that is incompatible with most systems "
                                   "except Apple systems and most Linux systems. Not recommended for "
                                   "removable media."));

        else if (strcmp (part_type, "gpt") == 0)
                return g_strdup (_("The GUID scheme is compatible with most modern systems but "
                                   "may be incompatible with some devices and legacy systems."));

        else if (strcmp (part_type, "none") == 0)
                return g_strdup (_("Marks the entire disk as unused. Use this option only if you want "
                                   "to avoid partitioning the disk for e.g. whole disk use or "
                                   "floppy / Zip disks."));

        else
                return NULL;
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

static GnomeKeyringPasswordSchema encrypted_device_password_schema = {
        GNOME_KEYRING_ITEM_GENERIC_SECRET,
        {
                { "luks-device-uuid", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
                { NULL, 0 }
        }
};

gchar *
gdu_util_get_secret (GduDevice *device)
{
        const char *usage;
        const char *uuid;
        char *password;
        gchar *ret;

        ret = NULL;

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
                                               "luks-device-uuid", uuid,
                                               NULL) == GNOME_KEYRING_RESULT_OK)
                goto out;

        ret = g_strdup (password);
        gnome_keyring_free_password (password);

out:
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
                                               "luks-device-uuid", uuid,
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
                                                  "luks-device-uuid", uuid,
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
        gchar *name;
        gboolean ret;

        ret = FALSE;
        name = NULL;

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

        name = g_strdup_printf (_("LUKS Passphrase for UUID %s"), uuid);

        if (gnome_keyring_store_password_sync (&encrypted_device_password_schema,
                                               keyring,
                                               name,
                                               secret,
                                               "luks-device-uuid", uuid,
                                               NULL) != GNOME_KEYRING_RESULT_OK) {
                g_warning ("%s: couldn't store passphrase in keyring", __FUNCTION__);
                goto out;
        }

        ret = TRUE;

out:
        g_free (name);
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
                        /* Translators: interface name for serial ATA disks */
                        name = _("SATA");
                } else if (strcmp (connection_interface, "ata_serial_esata") == 0) {
                        /* Translators: interface name for serial ATA disks */
                        name = _("eSATA");
                } else if (strcmp (connection_interface, "ata_parallel") == 0) {
                        /* Translators: interface name for parallel ATA disks */
                        name = _("PATA");
                } else if (g_str_has_prefix (connection_interface, "ata")) {
                        /* Translators: interface name for ATA disks */
                        name = _("ATA");
                } else if (g_str_has_prefix (connection_interface, "scsi")) {
                        /* Translators: interface name for SCSI disks */
                        name = _("SCSI");
                } else if (strcmp (connection_interface, "usb") == 0) {
                        /* Translators: interface name for USB disks */
                        name = _("USB");
                } else if (strcmp (connection_interface, "firewire") == 0) {
                        /* Translators: interface name for firewire disks */
                        name = _("Firewire");
                } else if (strcmp (connection_interface, "sdio") == 0) {
                        /* Translators: interface name for SDIO disks */
                        name = _("SDIO");
                } else if (strcmp (connection_interface, "virtual") == 0) {
                        /* Translators: interface name for virtual disks */
                        name = _("Virtual");
                }
        }

        if (name == NULL)
                /* Translators: name shown for unknown disk connection interfaces */
                name = C_("connection name", "Unknown");

        if (connection_speed > 0) {
                char *speed;

                speed = gdu_util_get_speed_for_display (connection_speed);
                /* Translators: Connection with speed information.
                 * First %s is the connection name, like 'SATA' or 'USB'
                 * second %s is the speed, like '2 Mbit/s'
                 */
                result = g_strdup_printf (_("%s at %s"), name, speed);
                g_free (speed);
        } else {
                result = g_strdup (name);
        }

        return result;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
gdu_linux_md_get_raid_level_for_display (const gchar *linux_md_raid_level,
                                         gboolean long_string)
{
        gchar *ret;

        if (strcmp (linux_md_raid_level, "raid0") == 0) {
                if (long_string)
                        ret = g_strdup (C_("RAID level", "Stripe (RAID-0)"));
                else
                        ret = g_strdup (C_("RAID level", "RAID-0"));
        } else if (strcmp (linux_md_raid_level, "raid1") == 0) {
                if (long_string)
                        ret = g_strdup (C_("RAID level", "Mirror (RAID-1)"));
                else
                        ret = g_strdup (C_("RAID level", "RAID-1"));
        } else if (strcmp (linux_md_raid_level, "raid4") == 0) {
                if (long_string)
                        ret = g_strdup (C_("RAID level", "Parity Disk (RAID-4)"));
                else
                        ret = g_strdup (C_("RAID level", "RAID-4"));
        } else if (strcmp (linux_md_raid_level, "raid5") == 0) {
                if (long_string)
                        ret = g_strdup (C_("RAID level", "Distributed Parity (RAID-5)"));
                else
                        ret = g_strdup (C_("RAID level", "RAID-5"));
        } else if (strcmp (linux_md_raid_level, "raid6") == 0) {
                if (long_string)
                        ret = g_strdup (C_("RAID level", "Dual Distributed Parity (RAID-6)"));
                else
                        ret = g_strdup (C_("RAID level", "RAID-6"));
        } else if (strcmp (linux_md_raid_level, "raid10") == 0) {
                if (long_string)
                        ret = g_strdup (C_("RAID level", "Stripe of Mirrors (RAID-10)"));
                else
                        ret = g_strdup (C_("RAID level", "RAID-10"));
        } else if (strcmp (linux_md_raid_level, "linear") == 0) {
                if (long_string)
                        ret = g_strdup (C_("RAID level", "Concatenated (Linear)"));
                else
                        ret = g_strdup (C_("RAID level", "Linear"));
        } else {
                ret = g_strdup (linux_md_raid_level);
        }

        return ret;
}

char *
gdu_linux_md_get_raid_level_description (const gchar *linux_md_raid_level)
{
        gchar *ret;

        if (strcmp (linux_md_raid_level, "raid0") == 0) {
                ret = g_strdup (_("Striped set without parity. "
                                  "Provides improved performance but no fault tolerance. "
                                  "If a single disk in the array fails, the entire RAID-0 array fails."));
        } else if (strcmp (linux_md_raid_level, "raid1") == 0) {
                ret = g_strdup (_("Mirrored set without parity. "
                                  "Provides fault tolerance and improved performance for reading. "
                                  "RAID-1 arrays can sustain all but one disk failing."));
        } else if (strcmp (linux_md_raid_level, "raid4") == 0) {
                ret = g_strdup (_("Striped set with parity on a single disk. "
                                  "Provides improved performance and fault tolerance. "
                                  "RAID-4 arrays can sustain a single disk failure."));
        } else if (strcmp (linux_md_raid_level, "raid5") == 0) {
                ret = g_strdup (_("Striped set with distributed parity. "
                                  "Provides improved performance and fault tolerance. "
                                  "RAID-5 arrays can sustain a single disk failure."));
        } else if (strcmp (linux_md_raid_level, "raid6") == 0) {
                ret = g_strdup (_("Striped set with dual distributed parity. "
                                  "Provides improved performance and fault tolerance. "
                                  "RAID-6 arrays can sustain two disk failures."));
        } else if (strcmp (linux_md_raid_level, "raid10") == 0) {
                ret = g_strdup (_("Striped set with distributed parity. "
                                  "Provides improved performance and fault tolerance. "
                                  "RAID-10 arrays can sustain multiple drive losses so long as no mirror loses all its drives."));
        /*} else if (strcmp (linux_md_raid_level, "linear") == 0) {*/

        } else {
                ret = g_strdup_printf (_("Unknown RAID level %s."), linux_md_raid_level);
                g_warning ("Please add support: %s", ret);
        }

        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * gdu_util_ata_smart_status_to_desc:
 * @status: Status from libatasmart, e.g. <quote>BAD_STATUS</quote>.
 * @out_highlight: Return value (or %NULL) for suggesting whether the status should be highlighted.
 * @out_action_text: Return value (or %NULL) for suggested action. Free with g_free().
 * @out_icon: Return value (or %NULL) for icon to show. Free with g_object_unref().
 *
 * Gets a human readable representation of @status.
 *
 * Returns: The human readable status. Free with g_free().
 */
gchar *
gdu_util_ata_smart_status_to_desc (const gchar  *status,
                                   gboolean     *out_highlight,
                                   gchar       **out_action_text,
                                   GIcon       **out_icon)
{
        const gchar *desc;
        const gchar *action_text;
        gchar *ret;
        gboolean highlight;
        GIcon *icon;

        highlight = FALSE;
        action_text = NULL;
        if (g_strcmp0 (status, "GOOD") == 0) {
                /* Translators: Overall description of the GOOD status */
                desc = _("Disk is healthy");
                icon = g_themed_icon_new ("gdu-smart-healthy");
        } else if (g_strcmp0 (status, "BAD_ATTRIBUTE_IN_THE_PAST") == 0) {
                /* Translators: Overall description of the BAD_ATTRIBUTE_IN_THE_PAST status */
                desc = _("Disk was used outside of design parameters in the past");
                icon = g_themed_icon_new ("gdu-smart-healthy");
        } else if (g_strcmp0 (status, "BAD_SECTOR") == 0) {
                /* Translators: Overall description of the BAD_SECTOR status */
                desc = _("Disk has a few bad sectors");
                icon = g_themed_icon_new ("gdu-smart-healthy");
        } else if (g_strcmp0 (status, "BAD_ATTRIBUTE_NOW") == 0) {
                /* Translators: Overall description of the BAD_ATTRIBUTE_NOW status */
                desc = _("DISK IS BEING USED OUTSIDE DESIGN PARAMETERS");
                /* Translators: Suggested action for the BAD_ATTRIBUTE_NOW status */
                action_text = _("Backup all data and replace the disk");
                highlight = TRUE;
                icon = g_themed_icon_new ("gdu-smart-threshold");
        } else if (g_strcmp0 (status, "BAD_SECTOR_MANY") == 0) {
                /* Translators: Overall description of the BAD_SECTOR_MANY status */
                desc = _("DISK HAS MANY BAD SECTORS");
                highlight = TRUE;
                /* Translators: Suggested action for the BAD_SECTOR_MANY status */
                action_text = _("Backup all data and replace the disk");
                icon = g_themed_icon_new ("gdu-smart-threshold");
        } else if (g_strcmp0 (status, "BAD_STATUS") == 0) {
                /* Translators: Overall description of the BAD_STATUS status */
                desc = _("DISK FAILURE IS IMMINENT");
                /* Translators: Suggested action for the BAD_STATUS status */
                action_text = _("Backup all data and replace the disk");
                highlight = TRUE;
                icon = g_themed_icon_new ("gdu-smart-failing");
        } else if (status == NULL || strlen (status) == 0) {
                /* Translators: Description when the overall SMART status is unknown */
                desc = _("Unknown");
                icon = g_themed_icon_new ("gdu-smart-unknown");
        } else {
                desc = status;
                icon = g_themed_icon_new ("gdu-smart-unknown");
        }

        if (out_highlight != NULL)
                *out_highlight = highlight;

        if (out_action_text != NULL)
                *out_action_text = g_strdup (action_text);

        if (out_icon != NULL)
                *out_icon = g_object_ref (icon);
        g_object_unref (icon);

        ret = g_strdup (desc);

        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
