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
