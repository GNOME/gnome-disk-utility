/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_TYPES_H__
#define __GDU_TYPES_H__

#include <gtk/gtk.h>
#include <udisks/udisks.h>

#include <libgdu/libgdu.h>

#include "gduenums.h"

G_BEGIN_DECLS

struct _GduCreateFilesystemWidget;
typedef struct _GduCreateFilesystemWidget GduCreateFilesystemWidget;

struct _GduEstimator;
typedef struct _GduEstimator GduEstimator;

struct GduDVDSupport;
typedef struct GduDVDSupport GduDVDSupport;

struct _GduLocalJob;
typedef struct _GduLocalJob GduLocalJob;

struct _GduJobManager;
typedef struct _GduJobManager GduJobManager;

struct GduXzDecompressor;
typedef struct GduXzDecompressor GduXzDecompressor;

G_END_DECLS

#endif /* __GDU_TYPES_H__ */
