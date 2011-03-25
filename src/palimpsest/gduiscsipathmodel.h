/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#ifndef __GDU_ISCSI_PATH_MODEL_H__
#define __GDU_ISCSI_PATH_MODEL_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_ISCSI_PATH_MODEL         (gdu_iscsi_path_model_get_type ())
#define GDU_ISCSI_PATH_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_ISCSI_PATH_MODEL, GduIScsiPathModel))
#define GDU_IS_ISCSI_PATH_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_ISCSI_PATH_MODEL))

enum
{
  GDU_ISCSI_PATH_MODEL_COLUMN_ACTIVE,
  GDU_ISCSI_PATH_MODEL_COLUMN_PORTAL_ADDRESS,
  GDU_ISCSI_PATH_MODEL_COLUMN_PORTAL_PORT,
  GDU_ISCSI_PATH_MODEL_COLUMN_TPGT,
  GDU_ISCSI_PATH_MODEL_COLUMN_INTERFACE,
  GDU_ISCSI_PATH_MODEL_COLUMN_STATUS,
  GDU_ISCSI_PATH_MODEL_N_COLUMNS
};

GType               gdu_iscsi_path_model_get_type   (void) G_GNUC_CONST;
GduIScsiPathModel  *gdu_iscsi_path_model_new        (UDisksClient      *client,
                                                     GDBusObject       *object);
UDisksClient       *gdu_iscsi_path_model_get_client (GduIScsiPathModel *model);
GDBusObject        *gdu_iscsi_path_model_get_object (GduIScsiPathModel *model);


G_END_DECLS

#endif /* __GDU_ISCSI_PATH_MODEL_H__ */
