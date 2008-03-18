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

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "gdu-page.h"

static void gdu_page_base_init (gpointer g_class);
static void gdu_page_class_init (gpointer g_class,
                                        gpointer class_data);

GType
gdu_page_get_type (void)
{
  static GType page_type = 0;

  if (! page_type)
    {
      static const GTypeInfo page_info =
      {
        sizeof (GduPageIface), /* class_size */
	gdu_page_base_init,   /* base_init */
	NULL,		/* base_finalize */
	gdu_page_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      page_type =
	g_type_register_static (G_TYPE_INTERFACE, "GduPageType",
				&page_info, 0);

      g_type_interface_add_prerequisite (page_type, G_TYPE_OBJECT);
    }

  return page_type;
}

static void
gdu_page_class_init (gpointer g_class,
                     gpointer class_data)
{
}

static void
gdu_page_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (! initialized)
    {
      initialized = TRUE;
    }
}

gboolean
gdu_page_update (GduPage *page, GduPresentable *presentable)
{
  GduPageIface *iface;

  g_return_val_if_fail (GDU_IS_PAGE (page), FALSE);

  iface = GDU_PAGE_GET_IFACE (page);

  return (* iface->update) (page, presentable);
}

char *
gdu_page_get_name (GduPage *page)
{
  GduPageIface *iface;

  g_return_val_if_fail (GDU_IS_PAGE (page), NULL);

  iface = GDU_PAGE_GET_IFACE (page);

  return (* iface->get_name) (page);
}

GtkWidget *
gdu_page_get_widget (GduPage *page)
{
  GduPageIface *iface;

  g_return_val_if_fail (GDU_IS_PAGE (page), NULL);

  iface = GDU_PAGE_GET_IFACE (page);

  return (* iface->get_widget) (page);
}
