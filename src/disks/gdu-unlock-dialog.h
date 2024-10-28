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

#define GDU_TYPE_UNLOCK_DIALOG (gdu_unlock_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduUnlockDialog, gdu_unlock_dialog, GDU, UNLOCK_DIALOG, AdwDialog)

void   gdu_unlock_dialog_show (GtkWindow    *parent_window,
                               UDisksObject *object);

G_END_DECLS
