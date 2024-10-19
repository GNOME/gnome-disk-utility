/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_BENCHMARK_DIALOG (gdu_benchmark_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduBenchmarkDialog, gdu_benchmark_dialog, GDU, BENCHMARK_DIALOG, AdwDialog)

void   gdu_benchmark_dialog_show (GtkWindow    *window,
                                  UDisksObject *object,
                                  UDisksClient *client);

G_END_DECLS
