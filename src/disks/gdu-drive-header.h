/* gdu-drive-header.h
 *
 * Copyright 2024 Christopher Davis <christopherdavis@gnome.org>
 *
 * Author(s)
 *   Christopher Davis <christopherdavis@gnome.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define GDU_TYPE_DRIVE_HEADER (gdu_drive_header_get_type())

G_DECLARE_FINAL_TYPE (GduDriveHeader, gdu_drive_header, GDU, DRIVE_HEADER, AdwBin)

void gdu_drive_header_set_layout_name (GduDriveHeader *self,
                                       const char     *name);

void gdu_drive_header_set_icon (GduDriveHeader *self,
                                GIcon          *icon);

void gdu_drive_header_set_drive_name (GduDriveHeader *self,
                                      const char     *name);

void gdu_drive_header_set_drive_path (GduDriveHeader *self,
                                      const char     *path);

G_END_DECLS
