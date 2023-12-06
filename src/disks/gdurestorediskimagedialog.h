/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_RESTORE_DISK_IMAGE_DIALOG_H__
#define __GDU_RESTORE_DISK_IMAGE_DIALOG_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

void     gdu_restore_disk_image_dialog_show (GtkWindow    *parent_window,
                                             UDisksObject *object,
                                             UDisksClient *client,
                                             const gchar  *disk_image_filename);

G_END_DECLS

#endif /* __GDU_RESTORE_DISK_IMAGE_DIALOG_H__ */
