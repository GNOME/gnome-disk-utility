/* gdu-block.h
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>
#include <stdbool.h>

#include "gdu-item.h"

G_BEGIN_DECLS

#define GDU_TYPE_BLOCK (gdu_block_get_type ())
G_DECLARE_FINAL_TYPE (GduBlock, gdu_block, GDU, BLOCK, GduItem)

GduBlock          *gdu_block_new                      (gpointer              udisk_client,
                                                       gpointer              udisk_object,
                                                       GduItem              *parent);
GduBlock          *gdu_block_sized_new                (gpointer              udisk_client,
                                                       guint64               start_offset,
                                                       guint64               size,
                                                       GduItem              *parent);
guint64            gdu_block_get_offset               (GduBlock             *self);
guint64            gdu_block_get_number               (GduBlock             *self);
guint64            gdu_block_get_unused_size          (GduBlock             *self);
bool               gdu_block_is_extended              (GduBlock             *self);
char              *gdu_block_get_size_str             (GduBlock             *self);
const char        *gdu_block_get_uuid                 (GduBlock             *self);
const char        *gdu_block_get_device_id            (GduBlock             *self);
const char        *gdu_block_get_fs_label             (GduBlock             *self);
const char        *gdu_block_get_fs_type              (GduBlock             *self);
const char *const *gdu_block_get_mount_points         (GduBlock             *self);
bool               gdu_block_needs_unmount            (GduBlock             *self);
void               gdu_block_set_fs_label_async       (GduBlock             *self,
                                                       const char           *label,
                                                       GAsyncReadyCallback   callback,
                                                       gpointer              user_data);
gboolean           gdu_block_set_fs_label_finish      (GduBlock             *self,
                                                       GAsyncResult         *result,
                                                       GError              **error);
void               gdu_block_emit_updated             (GduBlock             *self);

/* xxx: to be removed once the dust settles */
gpointer      gdu_block_get_object                    (GduBlock              *self);

G_END_DECLS
