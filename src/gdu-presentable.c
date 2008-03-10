/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-presentable.h
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

#include "gdu-presentable.h"

static void gdu_presentable_base_init (gpointer g_class);
static void gdu_presentable_class_init (gpointer g_class,
                                        gpointer class_data);

GType
gdu_presentable_get_type (void)
{
  static GType presentable_type = 0;

  if (! presentable_type)
    {
      static const GTypeInfo presentable_info =
      {
        sizeof (GduPresentableIface), /* class_size */
	gdu_presentable_base_init,   /* base_init */
	NULL,		/* base_finalize */
	gdu_presentable_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      presentable_type =
	g_type_register_static (G_TYPE_INTERFACE, "GduPresentableType",
				&presentable_info, 0);

      g_type_interface_add_prerequisite (presentable_type, G_TYPE_OBJECT);
    }

  return presentable_type;
}

static void
gdu_presentable_class_init (gpointer g_class,
                     gpointer class_data)
{
}

static void
gdu_presentable_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (! initialized)
    {
      g_signal_new ("changed",
                    GDU_TYPE_PRESENTABLE,
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GduPresentableIface, changed),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE, 0);

      g_signal_new ("removed",
                    GDU_TYPE_PRESENTABLE,
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GduPresentableIface, removed),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE, 0);

      initialized = TRUE;
    }
}

GduDevice *
gdu_presentable_get_device (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_device) (presentable);
}

GduPresentable *
gdu_presentable_get_enclosing_presentable (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_enclosing_presentable) (presentable);
}
