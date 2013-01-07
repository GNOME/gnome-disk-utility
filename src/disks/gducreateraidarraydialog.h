/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_CREATE_RAID_ARRAY_DIALOG_H__
#define __GDU_CREATE_RAID_ARRAY_DIALOG_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

gboolean gdu_create_raid_array_dialog_show (GduWindow *window,
                                            GList     *objects);

G_END_DECLS

#endif /* __GDU_CREATE_RAID_ARRAY_DIALOG_H__ */
