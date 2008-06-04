/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-known-filesystem.h
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

#ifndef GDU_KNOWN_FILESYSTEM_H
#define GDU_KNOWN_FILESYSTEM_H

#include <glib-object.h>

#define GDU_TYPE_KNOWN_FILESYSTEM             (gdu_known_filesystem_get_type ())
#define GDU_KNOWN_FILESYSTEM(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_KNOWN_FILESYSTEM, GduKnownFilesystem))
#define GDU_KNOWN_FILESYSTEM_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_KNOWN_FILESYSTEM,  GduKnownFilesystemClass))
#define GDU_IS_KNOWN_FILESYSTEM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_KNOWN_FILESYSTEM))
#define GDU_IS_KNOWN_FILESYSTEM_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_KNOWN_FILESYSTEM))
#define GDU_KNOWN_FILESYSTEM_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_KNOWN_FILESYSTEM, GduKnownFilesystemClass))

typedef struct _GduKnownFilesystemClass       GduKnownFilesystemClass;
typedef struct _GduKnownFilesystem            GduKnownFilesystem;

struct _GduKnownFilesystemPrivate;
typedef struct _GduKnownFilesystemPrivate     GduKnownFilesystemPrivate;

struct _GduKnownFilesystem
{
        GObject parent;

        /* private */
        GduKnownFilesystemPrivate *priv;
};

struct _GduKnownFilesystemClass
{
        GObjectClass parent_class;
};

GType       gdu_known_filesystem_get_type                           (void);
const char *gdu_known_filesystem_get_id                             (GduKnownFilesystem *known_filesystem);
const char *gdu_known_filesystem_get_name                           (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_unix_owners           (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_can_mount                      (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_can_create                     (GduKnownFilesystem *known_filesystem);
guint       gdu_known_filesystem_get_max_label_len                  (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_label_rename          (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_online_label_rename   (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_fsck                  (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_online_fsck           (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_resize_enlarge        (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_online_resize_enlarge (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_resize_shrink         (GduKnownFilesystem *known_filesystem);
gboolean    gdu_known_filesystem_get_supports_online_resize_shrink  (GduKnownFilesystem *known_filesystem);

#endif /* GDU_KNOWN_FILESYSTEM_H */
