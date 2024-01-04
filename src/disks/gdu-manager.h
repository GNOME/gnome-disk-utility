/* gdu-manager.h
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

G_BEGIN_DECLS

#define GDU_TYPE_MANAGER (gdu_manager_get_type ())
G_DECLARE_FINAL_TYPE (GduManager, gdu_manager, GDU, MANAGER, GObject)

GduManager   *gdu_manager_get_default         (GError              **error);
GListModel   *gdu_manager_get_drives          (GduManager           *self);
void          gdu_manager_open_loop_async     (GduManager           *self,
                                               GFile                *file,
                                               gboolean              read_only,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data);
gboolean      gdu_manager_open_loop_finish    (GduManager           *self,
                                               GAsyncResult         *result,
                                               GError              **error);



/* xxx: to be removed once dust settles */

#include <udisks/udisks.h>

UDisksClient *gdu_manager_get_client (GduManager *self);

G_END_DECLS
