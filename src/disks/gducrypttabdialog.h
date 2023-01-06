/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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

void   gdu_crypttab_dialog_show (GduWindow    *window,
                                 UDisksObject *object);

G_END_DECLS
