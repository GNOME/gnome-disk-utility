/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page.h
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include "gdu-presentable.h"

#ifndef GDU_PAGE_H
#define GDU_PAGE_H

#define GDU_TYPE_PAGE            (gdu_page_get_type ())
#define GDU_PAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_PAGE, GduPage))
#define GDU_IS_PAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_PAGE))
#define GDU_PAGE_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GDU_TYPE_PAGE, GduPageIface))

typedef struct _GduPage GduPage;

typedef struct _GduPageIface    GduPageIface;

struct _GduPageIface
{
        GTypeInterface g_iface;

        /* virtual table */
        GtkWidget *      (*get_widget)   (GduPage *page);
        char *           (*get_name)     (GduPage *page);
        gboolean         (*update)       (GduPage *page, GduPresentable *presentable);
};

GType           gdu_page_get_type      (void) G_GNUC_CONST;

GtkWidget      *gdu_page_get_widget    (GduPage *page);
char           *gdu_page_get_name      (GduPage *page);
gboolean        gdu_page_update        (GduPage *page, GduPresentable *presentable);

#endif /* GDU_PAGE_H */
