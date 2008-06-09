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

/**
 * SECTION:gdu-presentable
 * @title: GduPresentable
 * @short_description: Interface for devices presentable to the end user
 *
 * All storage devices in <literal>UNIX</literal> and <literal>UNIX</literal>-like
 * operating systems are mostly represented by so-called <literal>block</literal>
 * devices at the kernel-level. This is abstracted mostly 1-1 in the #GduDevice
 * class.
 *
 * However, from an user-interface point of view, it's useful to make
 * a finer-grained distinction; for example it's useful to make a
 * distinction between drives (e.g. a phyiscal hard disk, optical
 * drives) and volumes (e.g. a mountable file system or other contents
 * of which several may reside on the same drive if it's partitioned)
 * or just plain unallocated space on a partition disk.
 *
 * As such, classes encapsulating aspects of a UNIX block device (such
 * as it being drive, volume, empty space) that are interesting to
 * present in the user interface all implement the #GduPresentable
 * interface. This interface provides lowest-common denominator
 * functionality assisting in the creation of user interfaces; name
 * and icons are easily available as well as hierarchical grouping
 * in terms of parent/child relationships.
 **/


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
	g_type_register_static (G_TYPE_INTERFACE, "GduPresentable",
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
                /**
                 * GduPresentable::changed
                 * @presentable: A #GduPresentable.
                 *
                 * Emitted when @presentable changes.
                 **/
                g_signal_new ("changed",
                              GDU_TYPE_PRESENTABLE,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPresentableIface, changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

                /**
                 * GduPresentable::job-changed
                 * @presentable: A #GduPresentable.
                 *
                 * Emitted when job status on @presentable changes.
                 **/
                g_signal_new ("job-changed",
                              GDU_TYPE_PRESENTABLE,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPresentableIface, job_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

                /**
                 * GduPresentable::removed
                 * @presentable: The #GduPresentable that was removed.
                 *
                 * Emitted when @presentable is removed. Recipients
                 * should release references to @presentable.
                 **/
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

/**
 * gdu_presentable_get_device:
 * @presentable: A #GduPresentable.
 *
 * Gets the underlying device for @presentable if one is available.
 *
 * Returns: A #GduDevice or #NULL if there are no underlying device of
 * @presentable. Caller must unref the object when done with it.
 **/
GduDevice *
gdu_presentable_get_device (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_device) (presentable);
}

/**
 * gdu_presentable_get_enclosing_presentable:
 * @presentable: A #GduPresentable.
 *
 * Gets the #GduPresentable that is the parent of @presentable or
 * #NULL if there is no parent.
 *
 * Returns: The #GduPresentable that is a parent of @presentable or
 * #NULL if @presentable is the top-most presentable already. Caller
 * must unref the object.
 **/
GduPresentable *
gdu_presentable_get_enclosing_presentable (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_enclosing_presentable) (presentable);
}

/**
 * gdu_presentable_get_name:
 * @presentable: A #GduPresentable.
 *
 * Gets a name for @presentable suitable for presentation in an user
 * interface.
 *
 * Returns: The name. Caller must free the string with g_free().
 **/
char *
gdu_presentable_get_name (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_name) (presentable);
}

/**
 * gdu_presentable_get_icon_name:
 * @presentable: A #GduPresentable.
 *
 * Gets a name for the icon suitable for display in an user interface.
 *
 * Returns: The icon name. Caller must free the string with g_free().
 **/
char *
gdu_presentable_get_icon_name (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_icon_name) (presentable);
}

/**
 * gdu_presentable_get_offset:
 * @presentable: A #GduPresentable.
 *
 * Gets where the data represented by the presentable starts on the
 * underlying main block device
 *
 * Returns: Offset of @presentable or 0 if @presentable has no underlying device.
 **/
guint64
gdu_presentable_get_offset (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), 0);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_offset) (presentable);
}

/**
 * gdu_presentable_get_size:
 * @presentable: A #GduPresentable.
 *
 * Gets the size of @presentable.
 *
 * Returns: The size of @presentable or 0 if @presentable has no underlying device.
 **/
guint64
gdu_presentable_get_size (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), 0);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_size) (presentable);
}

/**
 * gdu_presentable_get_pool:
 * @presentable: A #GduPresentable.
 *
 * Gets the #GduPool that @presentable stems from.
 *
 * Returns: A #GduPool. Caller must unref object when done with it.
 **/
GduPool *
gdu_presentable_get_pool (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->get_pool) (presentable);
}

/**
 * gdu_presentable_is_allocated:
 * @presentable: A #GduPresentable.
 *
 * Determines if @presentable represents an underlying block device with data.
 *
 * Returns: Whether @presentable is allocated.
 **/
gboolean
gdu_presentable_is_allocated (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), FALSE);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->is_allocated) (presentable);
}

/**
 * gdu_presentable_is_recognized:
 * @presentable: A #GduPresentable.
 *
 * Gets whether the contents of @presentable are recognized; e.g. if
 * it's a file system, encrypted data or swap space.
 *
 * Returns: Whether @presentable is recognized.
 **/
gboolean
gdu_presentable_is_recognized (GduPresentable *presentable)
{
  GduPresentableIface *iface;

  g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), FALSE);

  iface = GDU_PRESENTABLE_GET_IFACE (presentable);

  return (* iface->is_recognized) (presentable);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * gdu_presentable_get_toplevel:
 * @presentable: A #GduPresentable.
 *
 * Gets the top-level presentable for a given presentable.
 *
 * Returns: A #GduPresentable or #NULL if @presentable is the top-most presentable. Caller must
 * unref the object when done with it
 **/
GduPresentable *
gdu_presentable_get_toplevel (GduPresentable *presentable)
{
        GduPresentable *parent;
        GduPresentable *maybe_parent;

        parent = presentable;
        do {
                maybe_parent = gdu_presentable_get_enclosing_presentable (parent);
                if (maybe_parent != NULL) {
                        g_object_unref (maybe_parent);
                        parent = maybe_parent;
                }
        } while (maybe_parent != NULL);

        return g_object_ref (parent);
}
