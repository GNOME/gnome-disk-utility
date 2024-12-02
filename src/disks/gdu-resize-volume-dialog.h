/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#pragma once

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_RESIZE_VOLUME_DIALOG (gdu_resize_volume_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduResizeVolumeDialog, gdu_resize_volume_dialog, GDU, RESIZE_VOLUME_DIALOG, AdwDialog)

void gdu_resize_dialog_show (GtkWindow    *parent_window,
                             UDisksObject *object,
                             UDisksClient *client);

G_END_DECLS
