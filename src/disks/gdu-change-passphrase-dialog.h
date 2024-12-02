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

#define GDU_TYPE_CHANGE_PASSPHRASE_DIALOG (gdu_change_passphrase_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduChangePassphraseDialog, gdu_change_passphrase_dialog, GDU, CHANGE_PASSPHRASE_DIALOG, AdwDialog)

void     gdu_change_passphrase_dialog_show (GtkWindow    *window,
                                            UDisksObject *object,
                                            UDisksClient *client);

G_END_DECLS
