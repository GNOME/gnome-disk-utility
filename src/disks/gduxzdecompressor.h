/* XZ Decompressor - based on GLib's GZLibDecompressor
 *
 * Copyright (C) 2013 David Zeuthen
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 *         Alexander Larsson <alexl@redhat.com>
 */

#ifndef __GDU_XZ_DECOMPRESSOR_H__
#define __GDU_XZ_DECOMPRESSOR_H__

#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_XZ_DECOMPRESSOR         (gdu_xz_decompressor_get_type ())
#define GDU_XZ_DECOMPRESSOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_XZ_DECOMPRESSOR, GduXzDecompressor))
#define GDU_XZ_DECOMPRESSOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDU_TYPE_XZ_DECOMPRESSOR, GduXzDecompressorClass))
#define GDU_IS_XZ_DECOMPRESSOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_XZ_DECOMPRESSOR))
#define GDU_IS_XZ_DECOMPRESSOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_XZ_DECOMPRESSOR))
#define GDU_XZ_DECOMPRESSOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDU_TYPE_XZ_DECOMPRESSOR, GduXzDecompressorClass))

typedef struct GduXzDecompressorClass   GduXzDecompressorClass;

struct GduXzDecompressorClass
{
  GObjectClass parent_class;
};

GType              gdu_xz_decompressor_get_type      (void) G_GNUC_CONST;
GduXzDecompressor *gdu_xz_decompressor_new           (void);

gsize              gdu_xz_decompressor_get_uncompressed_size (GFile *compressed_file);

G_END_DECLS

#endif /* __GDU_XZ_DECOMPRESSOR_H__ */
