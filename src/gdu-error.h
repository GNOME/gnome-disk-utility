/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-error.h
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

#ifndef GDU_ERROR_H
#define GDU_ERROR_H

#include <glib-object.h>
#include <polkit/polkit.h>

typedef enum
{
        GDU_ERROR_FAILED,
        GDU_ERROR_BUSY,
        GDU_ERROR_CANCELLED,
        GDU_ERROR_INVALID_OPTION,
        GDU_ERROR_ALREADY_MOUNTED,
        GDU_ERROR_NOT_MOUNTED,
        GDU_ERROR_NOT_CANCELLABLE,
        GDU_ERROR_NOT_PARTITION,
        GDU_ERROR_NOT_PARTITION_TABLE,
        GDU_ERROR_NOT_FILESYSTEM,
        GDU_ERROR_NOT_LUKS,
        GDU_ERROR_NOT_LOCKED,
        GDU_ERROR_NOT_UNLOCKED,
        GDU_ERROR_NOT_LINUX_MD,
        GDU_ERROR_NOT_LINUX_MD_COMPONENT,
        GDU_ERROR_NOT_DRIVE,
        GDU_ERROR_NOT_SMART_CAPABLE,
        GDU_ERROR_NOT_SUPPORTED,
        GDU_ERROR_NOT_FOUND,
} GduError;

#define GDU_ERROR gdu_error_quark ()
GQuark      gdu_error_quark           (void);

gboolean gdu_error_check_polkit_not_authorized (GError        *error,
                                                PolKitAction **pk_action,
                                                PolKitResult  *pk_result);

#endif /* GDU_ERROR_H */
