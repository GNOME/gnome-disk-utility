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

#define GDU_TYPE_FORMAT_VOLUME_DIALOG (gdu_format_volume_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduFormatVolumeDialog, gdu_format_volume_dialog, GDU, FORMAT_VOLUME_DIALOG, AdwDialog)

void gdu_create_format_show (UDisksClient *client,
                             GtkWindow    *parent_window,
                             UDisksObject *object,
                             gboolean      add_partition,
                             guint64       add_partition_offset,
                             guint64       add_partition_maxsize);

G_END_DECLS
