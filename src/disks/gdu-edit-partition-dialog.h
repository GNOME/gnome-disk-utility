/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

G_BEGIN_DECLS

#define GDU_TYPE_EDIT_PARTITION_DIALOG (gdu_edit_partition_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduEditPartitionDialog, gdu_edit_partition_dialog, GDU, EDIT_PARTITION_DIALOG, AdwDialog)

void     gdu_edit_partition_dialog_show (GtkWindow    *parent_window,
                                         UDisksObject *object,
                                         UDisksClient *client);

G_END_DECLS
