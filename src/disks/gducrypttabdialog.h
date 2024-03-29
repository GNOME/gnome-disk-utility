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

#define GDU_TYPE_CRYPTTAB_DIALOG (gdu_crypttab_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduCrypttabDialog, gdu_crypttab_dialog, GDU, CRYPTTAB_DIALOG, GtkDialog)

void   gdu_crypttab_dialog_show (GtkWindow    *parent_window,
                                 UDisksObject *object,
                                 UDisksClient *client);

G_END_DECLS
