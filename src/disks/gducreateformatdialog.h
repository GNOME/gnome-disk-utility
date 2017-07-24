/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#ifndef __GDU_CREATE_FORMAT_DIALOG_H__
#define __GDU_CREATE_FORMAT_DIALOG_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

void gdu_create_format_show (UDisksClient *client,
                             GtkWindow    *parent_window,
                             UDisksObject *object,
                             gboolean      show_custom,
                             gboolean      add_partition,
                             guint64       add_partition_offset,
                             guint64       add_partition_maxsize,
                             GCallback     finished_cb,
                             gpointer      cb_data);

G_END_DECLS

#endif /* __GDU_CREATE_FORMAT_DIALOG_H__ */
