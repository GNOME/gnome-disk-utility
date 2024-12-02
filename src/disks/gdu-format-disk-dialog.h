/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <gtk/gtk.h>
#include <adwaita.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_FORMAT_DISK_DIALOG (gdu_format_disk_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduFormatDiskDialog, gdu_format_disk_dialog, GDU, FORMAT_DISK_DIALOG, AdwDialog)

GType gdu_partitioning_type_get_type (void) G_GNUC_CONST;
#define GDU_TYPE_PARTITIONING_TYPE (gdu_partitioning_type_get_type ())

typedef enum
{
  GDU_PARTITIONING_TYPE_GPT,
  GDU_PARTITIONING_TYPE_DOS,
  GDU_PARTITIONING_TYPE_EMPTY,
} GduPartitioningType;

void     gdu_format_disk_dialog_show (GtkWindow    *parent,
                                      UDisksObject *object,
                                      UDisksClient *client);

G_END_DECLS
