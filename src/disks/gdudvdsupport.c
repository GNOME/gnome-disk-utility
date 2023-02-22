/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <gmodule.h>
#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <dvdread/dvd_reader.h>
#include <dvdread/dvd_udf.h>

#include "gdudvdsupport.h"


/* ---------------------------------------------------------------------------------------------------- */
/* libdvdcss support - see http://www.videolan.org/developers/libdvdcss.html */

#define DVDCSS_BLOCK_SIZE     2048
#define DVDCSS_READ_DECRYPT   (1 << 0)
#define DVDCSS_SEEK_KEY       (1 << 1)

struct dvdcss_s;
typedef struct dvdcss_s* dvdcss_t;

static dvdcss_t (*dvdcss_open)         (const char *psz_target) = NULL;
static int      (*dvdcss_close)        (dvdcss_t ctx) = NULL;
static int      (*dvdcss_seek)         (dvdcss_t ctx,
                                        int i_blocks,
                                        int i_flags ) = NULL;
static int      (*dvdcss_read)         (dvdcss_t ctx,
                                        void *p_buffer,
                                        int i_blocks,
                                        int i_flags ) = NULL;
static int      (*dvdcss_readv)        (dvdcss_t ctx,
                                        void *p_iovec,
                                        int   i_blocks,
                                        int   i_flags ) = NULL;
static char *   (*dvdcss_error)        (dvdcss_t ctx) = NULL;

static gboolean
have_dvdcss (void)
{
  static gsize once = 0;
  static gboolean available = FALSE;

  if (g_once_init_enter (&once))
    {
      GModule *module = NULL;

      module = g_module_open ("libdvdcss.so.2", G_MODULE_BIND_LOCAL);
      if (module == NULL)
        goto out;
      if (!g_module_symbol (module, "dvdcss_open", (gpointer*) &dvdcss_open) || dvdcss_open == NULL)
        goto out;
      if (!g_module_symbol (module, "dvdcss_close", (gpointer*) &dvdcss_close) || dvdcss_close == NULL)
        goto out;
      if (!g_module_symbol (module, "dvdcss_seek", (gpointer*) &dvdcss_seek) || dvdcss_seek == NULL)
        goto out;
      if (!g_module_symbol (module, "dvdcss_read", (gpointer*) &dvdcss_read) || dvdcss_read == NULL)
        goto out;
      if (!g_module_symbol (module, "dvdcss_readv", (gpointer*) &dvdcss_readv) || dvdcss_readv == NULL)
        goto out;
      if (!g_module_symbol (module, "dvdcss_error", (gpointer*) &dvdcss_error) || dvdcss_error == NULL)
        goto out;

      available = TRUE;

    out:
      if (!available)
        {
          if (module != NULL)
            g_module_close (module);
        }
      g_once_init_leave (&once, (gsize) 1);
    }
  return available;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct Range
{
  guint64 start;
  guint64 end;
  gboolean scrambled;
} Range;

static gint
range_compare_func (Range *a,
                    Range *b)
{
  if (a->start > b->start)
    return 1;
  else if (a->start < b->start)
    return -1;
  return 0;
}

/* ---------------------------------------------------------------------------------------------------- */

struct GduDVDSupport
{
  dvd_reader_t *dvd;
  dvdcss_t dvdcss;

  gboolean debug;

  Range *ranges;
  guint num_ranges;

  Range *last_read_range;
};

/* ---------------------------------------------------------------------------------------------------- */

GduDVDSupport *
gdu_dvd_support_new  (const gchar *device_file,
                      guint64      device_size)
{
  GduDVDSupport *support = NULL;
  guint title;
  GList *scrambled_ranges = NULL;
  GList *l;
  guint64 pos;
  GArray *a;
  Range *prev_range;

  /* We use dlopen() to access libdvdcss since it's normally not
   * shipped (so we can't hard-depend on it) but it may be installed
   * on the user's system anyway
   */
  if (!have_dvdcss ())
    goto out;

  support = g_new0 (GduDVDSupport, 1);

  if (g_getenv ("GDU_DEBUG") != NULL)
    support->debug = TRUE;

  support->dvd = DVDOpen (device_file);
  if (support->dvd == NULL)
    goto fail;

  support->dvdcss = dvdcss_open (device_file);
  if (support->dvdcss == NULL)
    goto fail;

  /* It follows from "6.9.1 Constraints imposed on UDF by DVD-Video"
   * of the OSTA UDF 2.60 spec (March 1, 2005) that
   *
   *  o  Video DVDs are using UDF 1.0.2
   *  o  Only VOB files are encrypted
   *  o  All VOB files of interest are in the VIDEO_TS/ directory
   *  o  VOB files are at most 2^30 bytes = 1.0 GB
   *  o  VOB files are a single extent.
   *  o  The same key is used everywhere in a VOB files
   *
   * This means we can simply go through all VOB files in the
   * VIDEO_TS/ directory and get their on-disc offset. Then for each
   * file, we retrieve the CSS key at said offset. We then build a
   * simple array of ranges
   *
   *  {range_start, range_end, range_is_scrambled}
   *
   * that covers the entire disc. Then when we're reading we can
   * consult this array to figure out when to change the key. Since
   * keys are cached, no slowdown will happen.
   *
   * This approach was inspired by Brasero's dvdcss plug-in, see
   *
   *  http://git.gnome.org/browse/brasero/tree/plugins/dvdcss/burn-dvdcss.c?id=BRASERO_3_6_0
   *
   * For the 'ls -l VIDEO_TS⁄*.VOB' part, we take advantage of the
   * fact that VOB files are in a known format, e.g. title 0 is always
   * VIDEO_TS.VOB and title 1 through 99 are always of the form
   * VTS_NN_M.VOB where 01 <= N <= 99 and 0 <= M <= 9. This way we can
   * simply use libdvdread's UDFFindFile() function on all 991
   * possible filenames...
   *
   * See http://en.wikipedia.org/wiki/VOB for how VOB files work.
   */
  for (title = 0; title <= 99; title++)
    {
      gint part;
      Range *range;

      for (part = 0; part <= 9; part++)
        {
          gchar vob_filename[64];
          uint32_t vob_sector_offset;
          uint32_t vob_size;
          guint64 rounded_vob_size;

          if (title == 0)
            {
              if (part > 0)
                break;
              snprintf (vob_filename, sizeof vob_filename, "/VIDEO_TS/VIDEO_TS.VOB");
            }
          else
            {
              snprintf (vob_filename, sizeof vob_filename, "/VIDEO_TS/VTS_%02u_%d.VOB", title, part);
            }

          vob_sector_offset = UDFFindFile (support->dvd, vob_filename, &vob_size);
          if (vob_sector_offset == 0)
            continue;

          if (dvdcss_seek (support->dvdcss, vob_sector_offset, DVDCSS_SEEK_KEY) != (int) vob_sector_offset)
            goto fail;

          if (vob_size == 0)
            continue;

          /* round up VOB size to nearest 2048-byte sector */
          rounded_vob_size = vob_size + 0x7ff;
          rounded_vob_size &= ~0x7ff;

          range = g_new0 (Range, 1);
          range->start = vob_sector_offset * 2048ULL;
          range->end = range->start + rounded_vob_size;
          range->scrambled = TRUE;

          if (G_UNLIKELY (support->debug))
            {
              g_print ("%s: %10" G_GUINT64_FORMAT " -> %10" G_GUINT64_FORMAT ": scrambled=%d\n",
                       vob_filename, range->start, range->end, range->scrambled);
            }

          scrambled_ranges = g_list_prepend (scrambled_ranges, range);
        }
    }

  /* If there are no VOB files on the disc, we don't need to decrypt - just bail */
  if (scrambled_ranges == NULL)
    goto fail;

  /* Otherwise, sort the ranges... */
  scrambled_ranges = g_list_sort (scrambled_ranges, (GCompareFunc) range_compare_func);

  /* ... remove overlapping ranges ... */
  prev_range = NULL;
  l = scrambled_ranges;
  while (l != NULL)
    {
      Range *range = l->data;
      GList *next = l->next;

      if (prev_range != NULL)
        {
          if (range->start >= prev_range->end)
            {
              prev_range = range;
            }
          else
            {
              if (G_UNLIKELY (support->debug))
                {
                  g_print ("Skipping overlapping range %" G_GUINT64_FORMAT " -> %" G_GUINT64_FORMAT " "
                           "(Prev %" G_GUINT64_FORMAT " -> %" G_GUINT64_FORMAT ")\n",
                           range->start, range->end, prev_range->start, prev_range->end);
                }
              scrambled_ranges = g_list_delete_link (scrambled_ranges, l);
              g_free (range);
            }
        }
      else
        {
          prev_range = range;
        }
      l = next;
    }

  /* ... and build an array of ranges covering the entire disc */
  a = g_array_new (FALSE, /* zero-terminated */
                   FALSE, /* clear */
                   sizeof (Range));
  pos = 0;
  for (l = scrambled_ranges; l != NULL; l = l->next)
    {
      Range *range = l->data;
      if (pos < range->start)
        {
          Range unscrambled_range = {0};
          unscrambled_range.start = pos;
          unscrambled_range.end = range->start;
          g_array_append_val (a, unscrambled_range);
        }
      g_array_append_val (a, *range);
      pos = range->end;
    }
  if (pos < device_size)
    {
      Range unscrambled_range = {0};
      unscrambled_range.start = pos;
      unscrambled_range.end = device_size;
      g_array_append_val (a, unscrambled_range);
    }
  support->num_ranges = a->len;
  support->ranges = (Range*) g_array_free (a, FALSE);

  if (G_UNLIKELY (support->debug))
    {
      guint n;
      for (n = 0; n < support->num_ranges; n++)
        {
          Range *range = support->ranges + n;
          g_print ("range %02u: %10" G_GUINT64_FORMAT " -> %10" G_GUINT64_FORMAT ": scrambled=%d\n",
                   n, range->start, range->end, range->scrambled);
        }
    }

 out:
  g_list_free_full (scrambled_ranges, g_free);
  return support;

 fail:
  gdu_dvd_support_free (support);
  support = NULL;
  goto out;
}

void
gdu_dvd_support_free (GduDVDSupport *support)
{
  g_free (support->ranges);
  if (support->dvdcss != NULL)
    dvdcss_close (support->dvdcss);
  if (support->dvd != NULL)
    DVDClose (support->dvd);
  g_free (support);
}

/* ---------------------------------------------------------------------------------------------------- */

gssize
gdu_dvd_support_read (GduDVDSupport *support,
                      int            fd,
                      guchar        *buffer,
                      guint64        offset,
                      guint64        size)
{
  guint n;
  gssize ret = -1;
  guint64 cur_offset = offset;
  guint64 num_left = size;
  guchar *cur_buffer = buffer;

  g_assert ((offset & 0x7ff) == 0);

  /* First find the range we're in
   *
   * Since callers are typically looping sequentially over the entire
   * data surface, first check if last used range still works
   */
  if (support->last_read_range != NULL &&
      offset >= support->last_read_range->start &&
      offset < support->last_read_range->end)
    {
      n = support->last_read_range - support->ranges;
    }
  else
    {
      /* Otherwise look through all ranges, starting form the first */
      for (n = 0; n < support->num_ranges; n++)
        {
          if (offset >= support->ranges[n].start &&
              offset < support->ranges[n].end)
            break;
        }
    }

  /* Break the read request into multiple requests not crossing any of
   * the ranges... we only want to use dvdcss_read() for the encrypted
   * bits
   */
  while (num_left > 0)
    {
      Range *r;
      guint64 num_left_in_range;
      guint64 num_to_read_in_range;
      ssize_t num_bytes_read;

      if (G_UNLIKELY (n == support->num_ranges))
        {
          g_warning ("Requested offset %" G_GUINT64_FORMAT " is out of range", offset);
          ret = -1;
          goto out;
        }

      r = support->ranges + n;

      g_assert (cur_offset >= r->start && cur_offset < r->end);
      num_left_in_range = r->end - cur_offset;

      num_to_read_in_range = MIN (num_left_in_range, num_left);

      if (G_UNLIKELY (support->debug))
        {
          g_print ("reading %" G_GUINT64_FORMAT " from %" G_GUINT64_FORMAT " (scrambled=%d) from range %u\n",
                   num_to_read_in_range, cur_offset, r->scrambled, n);
        }

      /* now read @num_to_read_in_range from @cur_offset into @cur_buffer */
      if (r->scrambled)
        {
          int flags = 0;
          int block_offset = cur_offset / 2048;
          int num_blocks_to_request = num_to_read_in_range / 2048;
          int num_blocks_read;

          g_assert ((cur_offset & 0x7ff) == 0);
          g_assert ((num_to_read_in_range & 0x7ff) == 0);

          /* see if we need to change the key? */
          if (support->last_read_range != r)
            {
              if (G_UNLIKELY (support->debug))
                {
                  g_print ("setting CSS key at offset %" G_GUINT64_FORMAT "\n", cur_offset);
                }
              flags |= DVDCSS_SEEK_KEY;
              support->last_read_range = r;
            }

          if (dvdcss_seek (support->dvdcss, block_offset, flags) != block_offset)
            goto out;

        dvdcss_read_again:
          num_blocks_read = dvdcss_read (support->dvdcss,
                                         cur_buffer,
                                         num_blocks_to_request,
                                         DVDCSS_READ_DECRYPT);
          if (num_blocks_read < 0)
            {
              if (errno == EAGAIN || errno == EINTR)
                goto dvdcss_read_again;
              /* treat as partial read */
              ret = size - num_left;
              goto out;
            }
          if (num_blocks_read == 0)
            {
              /* treat as partial read */
              ret = size - num_left;
              goto out;
            }
          g_assert (num_blocks_read <= num_blocks_to_request);
          num_bytes_read = num_blocks_read * 2048;
        }
      else
        {
          if (lseek (fd, cur_offset, SEEK_SET) == (off_t) -1)
            goto out;
        read_again:
          num_bytes_read = read (fd, cur_buffer, num_to_read_in_range);
          if (num_bytes_read < 0)
            {
              if (errno == EAGAIN || errno == EINTR)
                goto read_again;
              /* treat as partial read */
              ret = size - num_left;
              goto out;
            }
          if (num_bytes_read == 0)
            {
              /* treat as partial read */
              ret = size - num_left;
              goto out;
            }
        }

      cur_offset += num_bytes_read;
      cur_buffer += num_bytes_read;
      num_left -= num_bytes_read;

      /* the read could have been partial, in which case we're still in the same range */
      if (cur_offset >= r->end)
        n++;
    }

  ret = size - num_left;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
