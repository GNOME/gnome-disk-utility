/* gdu-window.c
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>

#include "gdu-manager.h"

G_BEGIN_DECLS

#define GDU_TYPE_WINDOW (gdu_window_get_type ())
G_DECLARE_FINAL_TYPE (GduWindow, gdu_window, GDU, WINDOW, AdwApplicationWindow)

GduWindow   *gdu_window_new                        (GApplication    *application,
                                                    GduManager      *manager);
void          gdu_window_show_attach_disk_image    (GduWindow      *self);

G_END_DECLS
