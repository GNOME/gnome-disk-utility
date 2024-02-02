/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#pragma once

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_FILESYSTEM_PAGE (gdu_create_filesystem_page_get_type ())
G_DECLARE_FINAL_TYPE (GduCreateFilesystemPage, gdu_create_filesystem_page, GDU, CREATE_FILESYSTEM_PAGE, AdwBin)

GType gdu_fs_type_get_type (void) G_GNUC_CONST;
#define GDU_TYPE_FS_TYPE (gdu_fs_type_get_type ())

typedef enum
{
  GDU_FS_TYPE_EXT4,
  GDU_FS_TYPE_NTFS,
  GDU_FS_TYPE_FAT,
  GDU_FS_TYPE_OTHER
} GduFsType;

GduCreateFilesystemPage *gdu_create_filesystem_page_new          (UDisksClient *client,
                                                                  UDisksDrive  *drive);

const gchar *            gdu_create_filesystem_page_get_name     (GduCreateFilesystemPage *page);

const gchar *            gdu_create_filesystem_page_get_fs       (GduCreateFilesystemPage *page);

gboolean                 gdu_create_filesystem_page_is_other     (GduCreateFilesystemPage *page);

gboolean                 gdu_create_filesystem_page_is_encrypted (GduCreateFilesystemPage *page);

const gchar *            gdu_create_filesystem_page_get_erase    (GduCreateFilesystemPage *page);

G_END_DECLS

