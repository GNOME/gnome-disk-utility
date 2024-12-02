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
#include "gdu-block.h"

G_BEGIN_DECLS

#define GDU_TYPE_EDIT_FILESYSTEM_DIALOG (gdu_edit_filesystem_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GduEditFilesystemDialog, gdu_edit_filesystem_dialog, GDU, EDIT_FILESYSTEM_DIALOG, AdwDialog)

void   gdu_edit_filesystem_dialog_show (GtkWindow    *parent_window,
                                        GduBlock     *block);

G_END_DECLS
