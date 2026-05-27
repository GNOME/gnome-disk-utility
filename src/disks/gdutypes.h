/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <gtk/gtk.h>
#include <libgdu/libgdu.h>
#include <udisks/udisks.h>

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
