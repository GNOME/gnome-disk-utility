/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#ifndef __GDU_RESIZE_DIALOG_H_H__
#define __GDU_RESIZE_DIALOG_H_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

void gdu_resize_dialog_show (GduWindow    *window,
                             UDisksObject *object);

G_END_DECLS

#endif /* __GDU_RESIZE_DIALOG_H__ */
