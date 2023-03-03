/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_BENCHMARK_DIALOG_H__
#define __GDU_BENCHMARK_DIALOG_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

void   gdu_benchmark_dialog_show (GduWindow    *window,
                                  UDisksObject *object);

G_END_DECLS

#endif /* __GDU_BENCHMARK_DIALOG_H__ */
