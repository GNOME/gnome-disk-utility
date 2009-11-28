/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-hba.h
 *
 * Copyright (C) 2009 David Zeuthen
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

#if !defined (__GDU_INSIDE_GDU_H) && !defined (GDU_COMPILATION)
#error "Only <gdu/gdu.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef __GDU_HBA_H
#define __GDU_HBA_H

#include <gdu/gdu-types.h>

G_BEGIN_DECLS

#define GDU_TYPE_HBA           (gdu_hba_get_type ())
#define GDU_HBA(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_HBA, GduHba))
#define GDU_HBA_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST ((k), GDU_HBA,  GduHbaClass))
#define GDU_IS_HBA(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_HBA))
#define GDU_IS_HBA_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_HBA))
#define GDU_HBA_GET_CLASS(k)   (G_TYPE_INSTANCE_GET_CLASS ((k), GDU_TYPE_HBA, GduHbaClass))

typedef struct _GduHbaClass       GduHbaClass;
typedef struct _GduHbaPrivate     GduHbaPrivate;

struct _GduHba
{
        GObject parent;

        /* private */
        GduHbaPrivate *priv;
};

struct _GduHbaClass
{
        GObjectClass parent_class;
};

GType       gdu_hba_get_type    (void);
GduAdapter *gdu_hba_get_adapter (GduHba *hba);

G_END_DECLS

#endif /* __GDU_HBA_H */
