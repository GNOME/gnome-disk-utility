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

#define GDU_TYPE_CREATE_FORMAT_DIALOG (gdu_create_format_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduCreateFormatDialog, gdu_create_format_dialog, GDU, CREATE_FORMAT_DIALOG, GtkDialog)

void gdu_create_format_show (UDisksClient *client,
                             GtkWindow    *parent_window,
                             UDisksObject *object,
                             gboolean      add_partition,
                             guint64       add_partition_offset,
                             guint64       add_partition_maxsize);

G_END_DECLS
