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

void gdu_disk_settings_dialog_show (GtkWindow *window, UDisksObject *object, UDisksClient *client);

gboolean gdu_disk_settings_dialog_should_show (UDisksObject *object);

G_END_DECLS
