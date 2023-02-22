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

#define GDU_TYPE_PARTITION_DIALOG (gdu_partition_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduPartitionDialog, gdu_partition_dialog, GDU, PARTITION_DIALOG, GtkDialog)

void     gdu_partition_dialog_show (GduWindow    *window,
                                    UDisksObject *object);

G_END_DECLS
