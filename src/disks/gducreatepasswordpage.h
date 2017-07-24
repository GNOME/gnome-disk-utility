/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#ifndef __GDU_CREATE_PASSWORD_PAGE_H__
#define __GDU_CREATE_PASSWORD_PAGE_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_PASSWORD_PAGE gdu_create_password_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreatePasswordPage, gdu_create_password_page, GDU, CREATE_PASSWORD_PAGE, GtkGrid)

GduCreatePasswordPage *gdu_create_password_page_new          (void);

const gchar *          gdu_create_password_page_get_password (GduCreatePasswordPage *page);

G_END_DECLS

#endif /* __GDU_CREATE_PASSWORD_PAGE_H__ */
