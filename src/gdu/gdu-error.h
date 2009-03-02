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

#if !defined (__GDU_INSIDE_GDU_H) && !defined (GDU_COMPILATION)
#error "Only <gdu/gdu.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef __GDU_ERROR_H
#define __GDU_ERROR_H

#include <gdu/gdu-types.h>
#include <polkit/polkit.h>

G_BEGIN_DECLS

/**
 * GduError:
 * @GDU_ERROR_FAILED: The operation failed.
 * @GDU_ERROR_INHIBITED: The daemon is being inhibited.
 * @GDU_ERROR_BUSY: The device is busy
 * @GDU_ERROR_CANCELLED: The operation was cancelled
 * @GDU_ERROR_INVALID_OPTION: An invalid option was passed
 * @GDU_ERROR_ALREADY_MOUNTED: Device is already mounted.
 * @GDU_ERROR_NOT_MOUNTED: Device is not mounted.
 * @GDU_ERROR_NOT_CANCELLABLE: Operation is not cancellable.
 * @GDU_ERROR_NOT_PARTITION: Device is not a partition.
 * @GDU_ERROR_NOT_PARTITION_TABLE: Device is not a partition table.
 * @GDU_ERROR_NOT_FILESYSTEM: Device is not a file system.
 * @GDU_ERROR_NOT_LUKS: Device is not a LUKS encrypted device.
 * @GDU_ERROR_NOT_LOCKED: Device is not locked.
 * @GDU_ERROR_NOT_UNLOCKED: Device is not unlocked.
 * @GDU_ERROR_NOT_LINUX_MD: Device is not a Linux md Software RAID device.
 * @GDU_ERROR_NOT_LINUX_MD_COMPONENT: Device is not a Linux md Software RAID component.
 * @GDU_ERROR_NOT_DRIVE: Device is not a drive.
 * @GDU_ERROR_NOT_SMART_CAPABLE: Device is not S.M.A.R.T. capable.
 * @GDU_ERROR_NOT_SUPPORTED: Operation not supported.
 * @GDU_ERROR_NOT_FOUND: Given device does not exist.
 *
 * Error codes in the #GDU_ERROR domain.
 */
typedef enum
{
        GDU_ERROR_FAILED,
        GDU_ERROR_INHIBITED,
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

/**
 * GDU_ERROR:
 *
 * Error domain used for errors reported from DeviceKit-disks daemon
 * via D-Bus. Note that not all remote errors are mapped to this
 * domain. Errors in this domain will come from the #GduError
 * enumeration. See #GError for more information on error domains.
 */
#define GDU_ERROR gdu_error_quark ()

GQuark      gdu_error_quark           (void);

gboolean gdu_error_check_polkit_not_authorized (GError        *error,
                                                PolKitAction **pk_action,
                                                PolKitResult  *pk_result);

G_END_DECLS

#endif /* __GDU_ERROR_H */
