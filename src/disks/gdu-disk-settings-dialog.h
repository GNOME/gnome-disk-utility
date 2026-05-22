/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <adwaita.h>

#include "gdu-drive.h"

G_BEGIN_DECLS

#define GDU_TYPE_DISK_SETTINGS_DIALOG (gdu_disk_settings_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduDiskSettingsDialog, gdu_disk_settings_dialog, GDU, DISK_SETTINGS_DIALOG, AdwDialog)

void gdu_disk_settings_dialog_show (GtkWindow *window, GduDrive *drive);

G_END_DECLS
