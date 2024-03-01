/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_FILESYSTEM_DIALOG_H_H__
#define __GDU_FILESYSTEM_DIALOG_H_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_FILESYSTEM_DIALOG (gdu_filesystem_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GduFilesystemDialog, gdu_filesystem_dialog, GDU, FILESYSTEM_DIALOG, GtkDialog)

void   gdu_filesystem_dialog_show (GtkWindow    *parent_window,
                                   UDisksObject *object,
                                   UDisksClient *client);

G_END_DECLS

#endif /* __GDU_FILESYSTEM_DIALOG_H__ */
