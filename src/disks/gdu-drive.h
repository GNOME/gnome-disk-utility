/* gdu-drive.h
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>

#include "gdu-item.h"

G_BEGIN_DECLS

#define GDU_TYPE_DRIVE (gdu_drive_get_type ())
G_DECLARE_FINAL_TYPE (GduDrive, gdu_drive, GDU, DRIVE, GduItem)

GduDrive     *gdu_drive_new                           (gpointer               udisk_client,
                                                       gpointer               udisk_object,
                                                       GduItem               *parent);
gboolean      gdu_drive_matches_object                (GduDrive              *self,
                                                       gpointer               udrive_object);
const char   *gdu_drive_get_name                      (GduDrive              *self);
const char   *gdu_drive_get_model                     (GduDrive              *self);
const char   *gdu_drive_get_serial                    (GduDrive              *self);
GList        *gdu_drive_get_siblings                  (GduDrive              *self);
void          gdu_drive_set_child                     (GduDrive              *self,
                                                       gpointer               udisk_object);

void          gdu_drive_standby_async                 (GduDrive              *self,
                                                       GCancellable          *cancellable,
                                                       GAsyncReadyCallback    callback,
                                                       gpointer               user_data);
gboolean      gdu_drive_standby_finish                (GduDrive              *self,
                                                       GAsyncResult          *result,
                                                       GError               **error);
void          gdu_drive_wakeup_async                  (GduDrive              *self,
                                                       GCancellable          *cancellable,
                                                       GAsyncReadyCallback    callback,
                                                       gpointer               user_data);
gboolean      gdu_drive_wakeup_finish                 (GduDrive              *self,
                                                       GAsyncResult          *result,
                                                       GError               **error);
void          gdu_drive_power_off_async               (GduDrive              *self,
                                                       gpointer               parent_window,
                                                       GCancellable          *cancellable,
                                                       GAsyncReadyCallback    callback,
                                                       gpointer               user_data);
gboolean      gdu_drive_power_off_finish              (GduDrive              *self,
                                                       GAsyncResult          *result,
                                                       GError               **error);
void          gdu_drive_block_changed                 (GduDrive              *self,
                                                       gpointer               block);

/* xxx: to be removed once the dust settles */
gpointer      gdu_drive_get_object                    (GduDrive              *self);
gpointer      gdu_drive_get_object_for_format         (GduDrive              *self);

G_END_DECLS
