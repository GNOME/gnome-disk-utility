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

GduDVDSupport *gdu_dvd_support_new (const gchar *device_file, guint64 device_size);

void gdu_dvd_support_free (GduDVDSupport *support);

gssize gdu_dvd_support_read (GduDVDSupport *support, gint fd, guchar *buffer, guint64 offset, guint64 size);

G_END_DECLS
