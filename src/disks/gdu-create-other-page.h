/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#ifndef __GDU_CREATE_OTHER_PAGE_H__
#define __GDU_CREATE_OTHER_PAGE_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_OTHER_PAGE gdu_create_other_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreateOtherPage, gdu_create_other_page, GDU, CREATE_OTHER_PAGE, AdwBin)

GType gdu_other_fs_type_get_type (void) G_GNUC_CONST;
#define GDU_TYPE_OTHER_FS_TYPE (gdu_other_fs_type_get_type ())

typedef enum
{
  GDU_OTHER_FS_TYPE_XFS,
  GDU_OTHER_FS_TYPE_SWAP,
  GDU_OTHER_FS_TYPE_BTRFS,
  GDU_OTHER_FS_TYPE_F2FS,
  GDU_OTHER_FS_TYPE_EXFAT,
  GDU_OTHER_FS_TYPE_UDF,
  GDU_OTHER_FS_TYPE_EMPTY,
  N_OTHER_FS
} GduOtherFsType;

GduCreateOtherPage *gdu_create_other_page_new          (UDisksClient *client);

gboolean            gdu_create_other_page_is_encrypted (GduCreateOtherPage *page);

const gchar *       gdu_create_other_page_get_fs       (GduCreateOtherPage *page);

G_END_DECLS

#endif /* __GDU_CREATE_OTHER_PAGE_H__ */
