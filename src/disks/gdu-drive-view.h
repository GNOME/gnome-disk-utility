/* gdu-drive-view.h
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "gdu-drive.h"

G_BEGIN_DECLS

#define GDU_TYPE_DRIVE_VIEW (gdu_drive_view_get_type ())
G_DECLARE_FINAL_TYPE (GduDriveView, gdu_drive_view, GDU, DRIVE_VIEW, AdwBin)

void         gdu_drive_view_set_drive  (GduDriveView *self,
                                        GduDrive    *drive);
GduDrive     *gdu_drive_view_get_drive (GduDriveView *self);

G_END_DECLS
