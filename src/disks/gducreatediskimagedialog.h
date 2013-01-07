/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_CREATE_DISK_IMAGE_DIALOG_H__
#define __GDU_CREATE_DISK_IMAGE_DIALOG_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

void     gdu_create_disk_image_dialog_show (GduWindow    *window,
                                            UDisksObject *object);

G_END_DECLS

#endif /* __GDU_CREATE_DISK_IMAGE_DIALOG_H__ */
