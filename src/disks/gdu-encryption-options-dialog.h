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

#define GDU_TYPE_ENCRYPTION_OPTIONS_DIALOG (gdu_encryption_options_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduEncryptionOptionsDialog, gdu_encryption_options_dialog, GDU, ENCRYPTION_OPTIONS_DIALOG, AdwDialog)

void   gdu_encryption_options_dialog_show (GtkWindow    *parent_window,
                                           UDisksClient *client,
                                           UDisksObject *object);

G_END_DECLS
