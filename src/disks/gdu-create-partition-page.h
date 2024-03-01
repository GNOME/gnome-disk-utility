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

#define GDU_TYPE_CREATE_PARTITION_PAGE gdu_create_partition_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreatePartitionPage, gdu_create_partition_page, GDU, CREATE_PARTITION_PAGE, AdwBin)

GduCreatePartitionPage *gdu_create_partition_page_new         (UDisksClient         *client,
                                                               UDisksPartitionTable *table,
                                                               guint64               max_size,
                                                               guint64               offset);

gboolean                gdu_create_partition_page_is_extended (GduCreatePartitionPage *page);

guint64                 gdu_create_partition_page_get_size    (GduCreatePartitionPage *page);

guint64                 gdu_create_partition_page_get_offset  (GduCreatePartitionPage *page);

G_END_DECLS
