/* gdu-drive-partition-row.h
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <handy.h>

#include "gdu-block.h"

G_BEGIN_DECLS

#define GDU_TYPE_BLOCK_ROW (gdu_block_row_get_type ())
G_DECLARE_FINAL_TYPE (GduBlockRow, gdu_block_row, GDU, BLOCK_ROW, HdyExpanderRow)

GduBlockRow *gdu_block_row_new       (GduBlock    *block);
GduBlock    *gdu_block_row_get_item  (GduBlockRow *self);

G_END_DECLS
