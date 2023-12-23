/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/x11/gdkx.h>
#include <stdlib.h>

#include "gduestimator.h"

//#define MAX_SAMPLES 100
#define MAX_SAMPLES 50

typedef struct
{
  gint64 time_usec;
  guint64 value;
} Sample;

struct _GduEstimator
{
  GObject parent;

  guint64 target_bytes;
  guint64 completed_bytes;
  guint64 bytes_per_sec;
  guint64 usec_remaining;

  Sample samples[MAX_SAMPLES];
  guint num_samples;
};

enum
{
  PROP_0,
  PROP_TARGET_BYTES,
  PROP_COMPLETED_BYTES,
  PROP_BYTES_PER_SEC,
  PROP_USEC_REMAINING,
};

G_DEFINE_TYPE (GduEstimator, gdu_estimator, G_TYPE_OBJECT)

static void
gdu_estimator_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GduEstimator *estimator = GDU_ESTIMATOR (object);

  switch (property_id)
    {
    case PROP_TARGET_BYTES:
      g_value_set_uint64 (value, gdu_estimator_get_target_bytes (estimator));
      break;

    case PROP_COMPLETED_BYTES:
      g_value_set_uint64 (value, gdu_estimator_get_completed_bytes (estimator));
      break;

    case PROP_BYTES_PER_SEC:
      g_value_set_uint64 (value, gdu_estimator_get_bytes_per_sec (estimator));
      break;

    case PROP_USEC_REMAINING:
      g_value_set_uint64 (value, gdu_estimator_get_usec_remaining (estimator));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_estimator_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GduEstimator *estimator = GDU_ESTIMATOR (object);

  switch (property_id)
    {
    case PROP_TARGET_BYTES:
      estimator->target_bytes = g_value_get_uint64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update (GduEstimator *estimator)
{
  guint n;
  gdouble sum_of_speeds;
  guint num_speeds;

  num_speeds = 0;
  sum_of_speeds = 0.0;
  for (n = 1; n < estimator->num_samples; n++)
    {
      Sample *a = &estimator->samples[n-1];
      Sample *b = &estimator->samples[n];
      gdouble speed;
      speed = (b->value - a->value) / (((gdouble) (b->time_usec - a->time_usec)) / G_USEC_PER_SEC);
      sum_of_speeds += speed;
      num_speeds++;
    }
  estimator->bytes_per_sec = 0;
  estimator->usec_remaining = 0;
  if (num_speeds > 0)
    {
      gdouble speed;
      speed = sum_of_speeds / num_speeds;
      estimator->bytes_per_sec = speed;
      if (estimator->bytes_per_sec > 0)
        {
          guint64 remaining_bytes = estimator->target_bytes - estimator->completed_bytes;
          estimator->usec_remaining = G_USEC_PER_SEC * remaining_bytes / estimator->bytes_per_sec;
        }
    }

  g_object_freeze_notify (G_OBJECT (estimator));
  g_object_notify (G_OBJECT (estimator), "bytes-per-sec");
  g_object_notify (G_OBJECT (estimator), "usec-remaining");
  g_object_thaw_notify (G_OBJECT (estimator));
}

static void
gdu_estimator_class_init (GduEstimatorClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = gdu_estimator_get_property;
  gobject_class->set_property = gdu_estimator_set_property;

  g_object_class_install_property (gobject_class, PROP_TARGET_BYTES,
                                   g_param_spec_uint64 ("target-bytes", NULL, NULL,
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COMPLETED_BYTES,
                                   g_param_spec_uint64 ("completed-bytes", NULL, NULL,
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BYTES_PER_SEC,
                                   g_param_spec_uint64 ("bytes-per-sec", NULL, NULL,
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USEC_REMAINING,
                                   g_param_spec_uint64 ("usec-remaining", NULL, NULL,
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
gdu_estimator_init (GduEstimator *estimator)
{
}

GduEstimator *
gdu_estimator_new (guint64  target_bytes)
{
  return GDU_ESTIMATOR (g_object_new (GDU_TYPE_ESTIMATOR,
                                      "target-bytes", target_bytes,
                                      NULL));
}

guint64
gdu_estimator_get_target_bytes (GduEstimator *estimator)
{
  g_return_val_if_fail (GDU_IS_ESTIMATOR (estimator), 0);
  return estimator->target_bytes;
}

guint64
gdu_estimator_get_completed_bytes (GduEstimator *estimator)
{
  g_return_val_if_fail (GDU_IS_ESTIMATOR (estimator), 0);
  return estimator->completed_bytes;
}

guint64
gdu_estimator_get_bytes_per_sec (GduEstimator *estimator)
{
  g_return_val_if_fail (GDU_IS_ESTIMATOR (estimator), 0);
  return estimator->bytes_per_sec;
}

guint64
gdu_estimator_get_usec_remaining (GduEstimator *estimator)
{
  g_return_val_if_fail (GDU_IS_ESTIMATOR (estimator), 0);
  return estimator->usec_remaining;
}

void
gdu_estimator_add_sample (GduEstimator    *estimator,
                          guint64          completed_bytes)
{
  Sample *sample;
  g_return_if_fail (GDU_IS_ESTIMATOR (estimator));
  g_return_if_fail (completed_bytes >= estimator->completed_bytes);

  estimator->completed_bytes = completed_bytes;

  if (estimator->num_samples == MAX_SAMPLES)
    {
      memmove (estimator->samples, estimator->samples + 1, sizeof (Sample) * (MAX_SAMPLES - 1));
      estimator->num_samples -= 1;
    }
  sample = &estimator->samples[estimator->num_samples++];

  sample->time_usec = g_get_real_time ();
  sample->value = completed_bytes;

  update (estimator);
}
