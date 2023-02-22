/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __LIB_GDU_ENUMS_H__
#define __LIB_GDU_ENUMS_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum
{
  GDU_FORMAT_DURATION_FLAGS_NONE                 = 0,
  GDU_FORMAT_DURATION_FLAGS_SUBSECOND_PRECISION  = (1<<0),
  GDU_FORMAT_DURATION_FLAGS_NO_SECONDS           = (1<<1)
} GduFormatDurationFlags;

G_END_DECLS

#endif /* __LIB_GDU_ENUMS_H__ */
