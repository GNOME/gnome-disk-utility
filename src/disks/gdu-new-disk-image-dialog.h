/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_NEW_DISK_IMAGE_DIALOG (gdu_new_disk_image_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduNewDiskImageDialog, gdu_new_disk_image_dialog, GDU, NEW_DISK_IMAGE_DIALOG, AdwDialog)

void gdu_new_disk_image_dialog_show (UDisksClient *client, GtkWindow *window);

G_END_DECLS
