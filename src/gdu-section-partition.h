/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-partition.h
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
#include "gdu-section.h"

#ifndef GDU_SECTION_PARTITION_H
#define GDU_SECTION_PARTITION_H

#define GDU_TYPE_SECTION_PARTITION             (gdu_section_partition_get_type ())
#define GDU_SECTION_PARTITION(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_SECTION_PARTITION, GduSectionPartition))
#define GDU_SECTION_PARTITION_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_SECTION_PARTITION,  GduSectionPartitionClass))
#define GDU_IS_SECTION_PARTITION(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_SECTION_PARTITION))
#define GDU_IS_SECTION_PARTITION_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_SECTION_PARTITION))
#define GDU_SECTION_PARTITION_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_SECTION_PARTITION, GduSectionPartitionClass))

typedef struct _GduSectionPartitionClass       GduSectionPartitionClass;
typedef struct _GduSectionPartition            GduSectionPartition;

struct _GduSectionPartitionPrivate;
typedef struct _GduSectionPartitionPrivate     GduSectionPartitionPrivate;

struct _GduSectionPartition
{
        GduSection parent;

        /* private */
        GduSectionPartitionPrivate *priv;
};

struct _GduSectionPartitionClass
{
        GduSectionClass parent_class;
};

GType            gdu_section_partition_get_type (void);
GtkWidget       *gdu_section_partition_new      (GduShell       *shell,
                                                 GduPresentable *presentable);

#endif /* GDU_SECTION_PARTITION_H */
