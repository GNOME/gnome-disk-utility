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

#define GDU_TYPE_CREATE_DISK_IMAGE_DIALOG (gdu_create_disk_image_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduCreateDiskImageDialog, gdu_create_disk_image_dialog, GDU, CREATE_DISK_IMAGE_DIALOG, AdwDialog)

void     gdu_create_disk_image_dialog_show (GtkWindow    *parent_window,
                                            UDisksObject *object,
                                            UDisksClient *client);

G_END_DECLS
