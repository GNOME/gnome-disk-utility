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

#include "config.h"

#include <glib/gi18n.h>

#include "gduxzdecompressor.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>
#include <string.h>

#include <lzma.h>

static void gdu_xz_decompressor_iface_init          (GConverterIface *iface);

struct GduXzDecompressor
{
  GObject parent_instance;

  lzma_stream stream;
};

G_DEFINE_TYPE_WITH_CODE (GduXzDecompressor, gdu_xz_decompressor, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_CONVERTER,
						gdu_xz_decompressor_iface_init))

static void
gdu_xz_decompressor_finalize (GObject *object)
{
  GduXzDecompressor *decompressor = GDU_XZ_DECOMPRESSOR (object);

  lzma_end (&decompressor->stream);

  G_OBJECT_CLASS (gdu_xz_decompressor_parent_class)->finalize (object);
}

static void
init_lzma (GduXzDecompressor *decompressor)
{
  lzma_ret ret;
  memset (&decompressor->stream, 0, sizeof decompressor->stream);
  ret = lzma_stream_decoder (&decompressor->stream,
                             UINT64_MAX, /* memlimit */
                             0);         /* flags */
  if (ret != LZMA_OK)
    g_critical ("Error initalizing lzma decoder: %u", ret);
}

static void
gdu_xz_decompressor_init (GduXzDecompressor *decompressor)
{
  init_lzma (decompressor);
}

static void
gdu_xz_decompressor_class_init (GduXzDecompressorClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gdu_xz_decompressor_finalize;
}

GduXzDecompressor *
gdu_xz_decompressor_new (void)
{
  GduXzDecompressor *decompressor;

  decompressor = g_object_new (GDU_TYPE_XZ_DECOMPRESSOR,
			       NULL);

  return decompressor;
}

static void
gdu_xz_decompressor_reset (GConverter *converter)
{
  GduXzDecompressor *decompressor = GDU_XZ_DECOMPRESSOR (converter);
  lzma_end (&decompressor->stream);
  init_lzma (decompressor);
}

static GConverterResult
gdu_xz_decompressor_convert (GConverter *converter,
			     const void *inbuf,
			     gsize       inbuf_size,
			     void       *outbuf,
			     gsize       outbuf_size,
			     GConverterFlags flags,
			     gsize      *bytes_read,
			     gsize      *bytes_written,
			     GError    **error)
{
  GduXzDecompressor *decompressor = GDU_XZ_DECOMPRESSOR (converter);
  lzma_ret res;

  decompressor->stream.next_in = (void *)inbuf;
  decompressor->stream.avail_in = inbuf_size;

  decompressor->stream.next_out = outbuf;
  decompressor->stream.avail_out = outbuf_size;

  res = lzma_code (&decompressor->stream, LZMA_RUN);

  if (res == LZMA_DATA_ERROR)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			   _("Invalid compressed data"));
      return G_CONVERTER_ERROR;
    }

  if (res == LZMA_MEM_ERROR)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			   _("Not enough memory"));
      return G_CONVERTER_ERROR;
    }

    if (res == LZMA_FORMAT_ERROR)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		   _("Internal error"));
      return G_CONVERTER_ERROR;
    }

    if (res == LZMA_BUF_ERROR)
      {
	if (flags & G_CONVERTER_FLUSH)
	  return G_CONVERTER_FLUSHED;

	/* LZMA_FINISH not set, so this means no progress could be made
	 * We do have output space, so this should only happen if we
	 * have no input but need some.
         */

	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
			     _("Need more input"));
	return G_CONVERTER_ERROR;
      }

  g_assert (res == LZMA_OK || res == LZMA_STREAM_END);

  *bytes_read = inbuf_size - decompressor->stream.avail_in;
  *bytes_written = outbuf_size - decompressor->stream.avail_out;

  if (res == LZMA_STREAM_END)
    return G_CONVERTER_FINISHED;

  return G_CONVERTER_CONVERTED;
}

static void
gdu_xz_decompressor_iface_init (GConverterIface *iface)
{
  iface->convert = gdu_xz_decompressor_convert;
  iface->reset = gdu_xz_decompressor_reset;
}

gsize
gdu_xz_decompressor_get_uncompressed_size (GFile *compressed_file)
{
  gchar *path = NULL;
  gsize ret = 0;
  GMappedFile *mapped_file = NULL;
  size_t bufpos = 0;
  uint64_t memlimit = UINT64_MAX;
  lzma_index *index_object = NULL;
  lzma_ret res;
  GError *error = NULL;
  uint8_t *buf;
  gsize len;
  lzma_stream_flags stream_flags;
  uint8_t *footer, *index;

  path = g_file_get_path (compressed_file);
  if (path == NULL)
    {
      gchar *uri;
      uri = g_file_get_uri (compressed_file);
      g_warning ("No path for URI '%s'. Maybe you need to enable FUSE.", uri);
      g_free (uri);
      goto out;
    }

  mapped_file = g_mapped_file_new (path, FALSE /* writable */, &error);
  if (mapped_file == NULL)
    {
      g_warning ("Error mapping file '%s': %s",
                 path, error->message);
      g_clear_error (&error);
      goto out;
    }

  buf = (uint8_t*) g_mapped_file_get_contents (mapped_file);
  len = g_mapped_file_get_length (mapped_file);

  if (len < 12)
    goto out;
  footer = buf + len - 12;
  if (lzma_stream_footer_decode (&stream_flags, footer) != LZMA_OK)
    goto out;
  if (stream_flags.backward_size > len - 12)
    goto out;
  index = footer - stream_flags.backward_size;

  res = lzma_index_buffer_decode (&index_object,
                                  &memlimit,
                                  NULL /* allocator */,
                                  index,
                                  &bufpos,
                                  footer - index);
  if (res != LZMA_OK)
    goto out;

  ret = lzma_index_uncompressed_size (index_object);

 out:
  if (index_object != NULL)
    lzma_index_end (index_object, NULL);
  if (mapped_file != NULL)
    g_mapped_file_unref (mapped_file);
  g_free (path);
  return ret;
}
