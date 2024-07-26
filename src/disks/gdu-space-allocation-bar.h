/* gdu-space-allocation-bar.c
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright 2024 Inam Ul Haq <inam123451@gmail.com>
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   Inam Ul Haq <inam123451@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

#define GDU_TYPE_SPACE_ALLOCATION_BAR (gdu_space_allocation_bar_get_type ())
G_DECLARE_FINAL_TYPE (GduSpaceAllocationBar, gdu_space_allocation_bar, GDU, SPACE_ALLOCATION_BAR, GtkWidget)

void
gdu_space_allocation_bar_set_drive (GduSpaceAllocationBar *self,
                                    GduDrive             *drive);

G_END_DECLS
