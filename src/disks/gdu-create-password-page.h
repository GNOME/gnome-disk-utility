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

#define GDU_TYPE_CREATE_PASSWORD_PAGE gdu_create_password_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreatePasswordPage, gdu_create_password_page, GDU, CREATE_PASSWORD_PAGE, AdwBin)

GduCreatePasswordPage *gdu_create_password_page_new          (void);

const gchar *          gdu_create_password_page_get_password (GduCreatePasswordPage *page);

gdouble                gdu_password_strength                 (const gchar  *password,
                                                              const gchar **hint,
                                                              gint         *strength_level);

G_END_DECLS
