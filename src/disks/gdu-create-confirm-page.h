/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#pragma once

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_CONFIRM_PAGE gdu_create_confirm_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreateConfirmPage, gdu_create_confirm_page, GDU, CREATE_CONFIRM_PAGE, AdwBin)

GduCreateConfirmPage *gdu_create_confirm_page_new (UDisksClient *client,
                                                   UDisksObject *object,
                                                   UDisksBlock  *block);

G_END_DECLS
