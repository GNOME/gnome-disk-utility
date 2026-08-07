/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Programejt <programejt@proton.me>
 */
#pragma once

#include <adwaita.h>

#include "gdu-manager.h"

G_BEGIN_DECLS

#define GDU_TYPE_ATTACH_DISK_IMAGE_DIALOG (gdu_attach_disk_image_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduAttachDiskImageDialog, gdu_attach_disk_image_dialog, GDU, ATTACH_DISK_IMAGE_DIALOG, AdwDialog)

void gdu_attach_disk_image_dialog_show (GtkWindow *parent_window, GduManager *manager);

G_END_DECLS
