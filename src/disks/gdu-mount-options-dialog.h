/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_FSTAB_DIALOG_H_H__
#define __GDU_FSTAB_DIALOG_H_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_MOUNT_OPTIONS_DIALOG (gdu_mount_options_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduMountOptionsDialog, gdu_mount_options_dialog, GDU, MOUNT_OPTIONS_DIALOG, AdwDialog)

void   gdu_mount_options_dialog_show (GtkWindow    *parent_window,
                              UDisksObject *object,
                              UDisksClient *client);

G_END_DECLS

#endif /* __GDU_FSTAB_DIALOG_H__ */
