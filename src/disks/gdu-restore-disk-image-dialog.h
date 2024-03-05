/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_RESTORE_DISK_IMAGE_DIALOG (gdu_restore_disk_image_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduRestoreDiskImageDialog, gdu_restore_disk_image_dialog, GDU, RESTORE_DISK_IMAGE_DIALOG, AdwWindow)

void     gdu_restore_disk_image_dialog_show (GtkWindow    *parent_window,
                                             UDisksObject *object,
                                             UDisksClient *client,
                                             const gchar  *disk_image_filename);

G_END_DECLS

