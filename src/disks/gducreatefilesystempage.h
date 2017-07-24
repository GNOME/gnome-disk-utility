/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#ifndef __GDU_CREATE_FILESYSTEM_PAGE_H__
#define __GDU_CREATE_FILESYSTEM_PAGE_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_FILESYSTEM_PAGE gdu_create_filesystem_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreateFilesystemPage, gdu_create_filesystem_page, GDU, CREATE_FILESYSTEM_PAGE, GtkGrid)

GduCreateFilesystemPage *gdu_create_filesystem_page_new          (gboolean      show_custom,
                                                                  UDisksDrive  *drive);

const gchar *            gdu_create_filesystem_page_get_name     (GduCreateFilesystemPage *page);

const gchar *            gdu_create_filesystem_page_get_fs       (GduCreateFilesystemPage *page);

gboolean                 gdu_create_filesystem_page_is_other     (GduCreateFilesystemPage *page);

gboolean                 gdu_create_filesystem_page_is_encrypted (GduCreateFilesystemPage *page);

const gchar *            gdu_create_filesystem_page_get_erase    (GduCreateFilesystemPage *page);

G_END_DECLS

#endif /* __GDU_CREATE_FILESYSTEM_PAGE_H__ */
