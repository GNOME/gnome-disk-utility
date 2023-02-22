/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include "gduenums.h"
#include "gdulocaljob.h"

typedef struct GduLocalJobClass GduLocalJobClass;

struct GduLocalJob
{
  UDisksJobSkeleton parent;

  UDisksObject *object;
  gchar *description;
  gchar *extra_markup;
};

struct GduLocalJobClass
{
  UDisksJobSkeletonClass parent_class;

  /* signals */
  void (*canceled) (GduLocalJob *job);
};

enum
{
  PROP_0,
  PROP_OBJECT,
  PROP_DESCRIPTION,
  PROP_EXTRA_MARKUP,
};

enum
{
  CANCELED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (GduLocalJob, gdu_local_job, UDISKS_TYPE_JOB_SKELETON)

static void
gdu_local_job_finalize (GObject *object)
{
  GduLocalJob *job = GDU_LOCAL_JOB (object);

  g_object_unref (job->object);
  g_free (job->description);
  g_free (job->extra_markup);

  G_OBJECT_CLASS (gdu_local_job_parent_class)->finalize (object);
}

static void
gdu_local_job_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GduLocalJob *job = GDU_LOCAL_JOB (object);

  switch (property_id)
    {
    case PROP_OBJECT:
      g_value_set_object (value, gdu_local_job_get_object (job));
      break;

    case PROP_DESCRIPTION:
      g_value_set_string (value, gdu_local_job_get_description (job));
      break;

    case PROP_EXTRA_MARKUP:
      g_value_set_string (value, gdu_local_job_get_extra_markup (job));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_local_job_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GduLocalJob *job = GDU_LOCAL_JOB (object);

  switch (property_id)
    {
    case PROP_OBJECT:
      g_assert (job->object == NULL);
      job->object = g_value_dup_object (value);
      break;

    case PROP_DESCRIPTION:
      gdu_local_job_set_description (job, g_value_get_string (value));
      break;

    case PROP_EXTRA_MARKUP:
      gdu_local_job_set_extra_markup (job, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_local_job_class_init (GduLocalJobClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = gdu_local_job_get_property;
  gobject_class->set_property = gdu_local_job_set_property;
  gobject_class->finalize     = gdu_local_job_finalize;

  g_object_class_install_property (gobject_class, PROP_DESCRIPTION,
                                   g_param_spec_string ("description", NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EXTRA_MARKUP,
                                   g_param_spec_string ("extra-markup", NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OBJECT,
                                   g_param_spec_object ("object", NULL, NULL,
                                                        UDISKS_TYPE_OBJECT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  signals[CANCELED_SIGNAL] = g_signal_new ("canceled",
                                           G_TYPE_FROM_CLASS (klass),
                                           G_SIGNAL_RUN_LAST,
                                           G_STRUCT_OFFSET (GduLocalJobClass, canceled),
                                           NULL,
                                           NULL,
                                           g_cclosure_marshal_generic,
                                           G_TYPE_NONE,
                                           0);
}

static void
gdu_local_job_init (GduLocalJob *widget)
{
}

GduLocalJob *
gdu_local_job_new (UDisksObject *object)
{
  return GDU_LOCAL_JOB (g_object_new (GDU_TYPE_LOCAL_JOB,
                                      "object", object,
                                      NULL));
}

void
gdu_local_job_set_description (GduLocalJob *job,
                               const gchar *description)
{
  g_return_if_fail (GDU_IS_LOCAL_JOB (job));
  g_free (job->description);
  job->description = g_strdup (description);
  g_object_notify (G_OBJECT (job), "description");
}

const gchar *
gdu_local_job_get_description (GduLocalJob *job)
{
  g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);
  return job->description;
}

void
gdu_local_job_set_extra_markup (GduLocalJob *job,
                                const gchar *markup)
{
  g_return_if_fail (GDU_IS_LOCAL_JOB (job));
  g_free (job->extra_markup);
  job->extra_markup = g_strdup (markup);
  g_object_notify (G_OBJECT (job), "extra-markup");
}

const gchar *
gdu_local_job_get_extra_markup (GduLocalJob *job)
{
  g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);
  return job->extra_markup;
}

UDisksObject *
gdu_local_job_get_object (GduLocalJob *job)
{
  g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);
  return job->object;
}

void
gdu_local_job_canceled (GduLocalJob  *job)
{
  g_return_if_fail (GDU_IS_LOCAL_JOB (job));
  g_signal_emit (job, signals[CANCELED_SIGNAL], 0);
}


