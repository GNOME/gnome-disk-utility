/* gdu-drive-row.h
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>

#include "gdu-drive.h"

G_BEGIN_DECLS

#define GDU_TYPE_DRIVE_ROW (gdu_drive_row_get_type ())
G_DECLARE_FINAL_TYPE (GduDriveRow, gdu_drive_row, GDU, DRIVE_ROW, AdwActionRow)

GduDriveRow *gdu_drive_row_new      (GduDrive    *drive);
GduDrive    *gdu_drive_row_get_drive (GduDriveRow *self);

G_END_DECLS
