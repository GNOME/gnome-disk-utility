/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#ifndef __GDU_CREATE_OTHER_PAGE_H__
#define __GDU_CREATE_OTHER_PAGE_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_OTHER_PAGE gdu_create_other_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreateOtherPage, gdu_create_other_page, GDU, CREATE_OTHER_PAGE, GtkBox)

GduCreateOtherPage *gdu_create_other_page_new          (UDisksClient *client);

gboolean            gdu_create_other_page_is_encrypted (GduCreateOtherPage *page);

const gchar *       gdu_create_other_page_get_fs       (GduCreateOtherPage *page);

G_END_DECLS

#endif /* __GDU_CREATE_OTHER_PAGE_H__ */
