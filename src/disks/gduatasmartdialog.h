/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_ATA_SMART_DIALOG_H_H__
#define __GDU_ATA_SMART_DIALOG_H_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

void   gdu_ata_smart_dialog_show (GtkWindow    *parent_window,
                                  UDisksObject *object,
                                  UDisksClient *client);

gchar *gdu_ata_smart_get_one_liner_assessment (UDisksDriveAta *ata,
                                               gboolean       *out_smart_is_supported,
                                               gboolean       *out_warn);

G_END_DECLS

#endif /* __GDU_ATA_SMART_DIALOG_H__ */
