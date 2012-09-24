/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_SELECT_DISK_DIALOG_H__
#define __GDU_SELECT_DISK_DIALOG_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

GList *gdu_select_disk_dialog_show (GduApplication     *application,
                                    GtkWindow          *parent_window,
                                    GduSelectDiskFlags  flags);

G_END_DECLS

#endif /* __GDU_SELECT_DISK_DIALOG_H__ */
