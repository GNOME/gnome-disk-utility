/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduatasmartdialog.h"

enum
{
  ID_COLUMN,
  DESC_COLUMN,
  LONG_DESC_COLUMN,
  ASSESSMENT_COLUMN,
  PRETTY_COLUMN,
  NORMALIZED_COLUMN,
  THRESHOLD_COLUMN,
  WORST_COLUMN,
  FLAGS_COLUMN,
  N_COLUMNS,
};

typedef struct
{
  UDisksObject *object;
  UDisksDriveAta *ata;

  GduWindow *window;
  GtkBuilder *builder;

  GtkListStore *attributes_list;

  GtkWidget *dialog;
  GtkWidget *updated_label;
  GtkWidget *temperature_label;
  GtkWidget *powered_on_label;
  GtkWidget *self_test_label;
  GtkWidget *self_assessment_label;
  GtkWidget *overall_assessment_label;

  GtkWidget *attr_value_label;
  GtkWidget *attr_type_label;
  GtkWidget *attr_long_description_label;

  GtkWidget *attributes_treeview;

  GtkWidget *start_selftest_button;
  GtkWidget *stop_selftest_button;

  GtkWidget *selftest_menu;
  GtkWidget *selftest_short_menuitem;
  GtkWidget *selftest_extended_menuitem;
  GtkWidget *selftest_conveyance_menuitem;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, updated_label), "updated-label"},
  {G_STRUCT_OFFSET (DialogData, temperature_label), "temperature-label"},
  {G_STRUCT_OFFSET (DialogData, powered_on_label), "powered-on-label"},
  {G_STRUCT_OFFSET (DialogData, self_test_label), "self-test-label"},
  {G_STRUCT_OFFSET (DialogData, self_assessment_label), "self-assessment-label"},
  {G_STRUCT_OFFSET (DialogData, overall_assessment_label), "overall-assessment-label"},
  {G_STRUCT_OFFSET (DialogData, attributes_treeview), "attributes-treeview"},
  {G_STRUCT_OFFSET (DialogData, selftest_menu), "selftest-menu"},
  {G_STRUCT_OFFSET (DialogData, selftest_short_menuitem), "selftest-short-menuitem"},
  {G_STRUCT_OFFSET (DialogData, selftest_extended_menuitem), "selftest-extended-menuitem"},
  {G_STRUCT_OFFSET (DialogData, selftest_conveyance_menuitem), "selftest-conveyance-menuitem"},
  {G_STRUCT_OFFSET (DialogData, attr_value_label), "attr-value-label"},
  {G_STRUCT_OFFSET (DialogData, attr_type_label), "attr-type-label"},
  {G_STRUCT_OFFSET (DialogData, attr_long_description_label), "attr-long-description-label"},
  {0, NULL}
};

static void
dialog_data_free (DialogData *data)
{
  if (data->dialog != NULL)
    {
      gtk_widget_hide (data->dialog);
      gtk_widget_destroy (data->dialog);
    }
  if (data->object != NULL)
    g_object_unref (data->object);
  if (data->window != NULL)
    g_object_unref (data->window);
  if (data->builder != NULL)
    g_object_unref (data->builder);

  if (data->attributes_list != NULL)
    g_object_unref (data->attributes_list);

  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct {
        const gchar *name;
        const gchar *pretty_name;
        const gchar *desc;
} SmartDetails;

/* See http://smartmontools.sourceforge.net/doc.html
 *     http://en.wikipedia.org/wiki/S.M.A.R.T
 *     http://www.t13.org/Documents/UploadedDocuments/docs2005/e05148r0-ACS-ATA_SMARTAttributesAnnex.pdf
 *
 *     Keep in sync with libatasmart. Last sync: Thu Aug 20 2009
 */
static const SmartDetails smart_details[] = {
{
  "raw-read-error-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Read Error Rate"),
  N_("Frequency of errors while reading raw data from the disk. "
     "A non-zero value indicates a problem with "
     "either the disk surface or read/write heads")
},
{
  "throughput-performance",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Throughput Performance"),
  N_("Average efficiency of the disk")
},
{
  "spin-up-time",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Spinup Time"),
  N_("Time needed to spin up the disk")
},
{
  "start-stop-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Start/Stop Count"),
  N_("Number of spindle start/stop cycles")
},
{
  "reallocated-sector-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Reallocated Sector Count"),
  N_("Count of remapped sectors. "
     "When the hard drive finds a read/write/verification error, it marks the sector "
     "as \"reallocated\" and transfers data to a special reserved area (spare area)")
},
{
  "read-channel-margin",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Read Channel Margin"),
  N_("Margin of a channel while reading data.")
},
{
  "seek-error-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Seek Error Rate"),
  N_("Frequency of errors while positioning")
},
{
  "seek-time-performance",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Seek Timer Performance"),
  N_("Average efficiency of operations while positioning")
},
{
  "power-on-hours",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Power-On Hours"),
  N_("Number of hours elapsed in the power-on state")
},
{
  "spin-retry-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Spinup Retry Count"),
  N_("Number of retry attempts to spin up")
},
{
  "calibration-retry-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Calibration Retry Count"),
  N_("Number of attempts to calibrate the device")
},
{
  "power-cycle-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Power Cycle Count"),
  N_("Number of power-on events")
},
{
  "read-soft-error-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Soft read error rate"),

  N_("Frequency of errors while reading from the disk")
},
{
  "reported-uncorrect",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Reported Uncorrectable Errors"),
  N_("Number of errors that could not be recovered using hardware ECC")
},
{
  "high-fly-writes",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("High Fly Writes"),
  N_("Number of times a recording head is flying outside its normal operating range")
},
{
  "airflow-temperature-celsius",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Airflow Temperature"),
  N_("Airflow temperature of the drive")
},
{
  "g-sense-error-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("G-sense Error Rate"),
  N_("Frequency of mistakes as a result of impact loads")
},
{
  "power-off-retract-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Power-off Retract Count"),
  N_("Number of power-off or emergency retract cycles")
},
{
  "load-cycle-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Load/Unload Cycle Count"),
  N_("Number of cycles into landing zone position")
},
{
  "temperature-celsius-2",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Temperature"),
  N_("Current internal temperature of the drive")
},
{
  "hardware-ecc-recovered",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Hardware ECC Recovered"),
  N_("Number of ECC on-the-fly errors")
},
{
  "reallocated-event-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Reallocation Count"),
  N_("Number of remapping operations. "
     "The raw value of this attribute shows the total number of (successful "
     "and unsuccessful) attempts to transfer data from reallocated sectors "
     "to a spare area")
},
{
  "current-pending-sector",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Current Pending Sector Count"),
  N_("Number of sectors waiting to be remapped. "
     "If the sector waiting to be remapped is subsequently written or read "
     "successfully, this value is decreased and the sector is not remapped. Read "
     "errors on the sector will not remap the sector, it will only be remapped on "
     "a failed write attempt")
},
{
  "offline-uncorrectable",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Uncorrectable Sector Count"),
  N_("The total number of uncorrectable errors when reading/writing a sector. "
     "A rise in the value of this attribute indicates defects of the "
     "disk surface and/or problems in the mechanical subsystem")
},
{
  "udma-crc-error-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("UDMA CRC Error Rate"),
  N_("Number of CRC errors during UDMA mode")
},
{
  "multi-zone-error-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Write Error Rate"),
  N_("Number of errors while writing to disk (or) multi-zone error rate (or) flying-height")
},
{
  "soft-read-error-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Soft Read Error Rate"),
  N_("Number of off-track errors")
},
{
  "ta-increase-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Data Address Mark Errors"),
  N_("Number of Data Address Mark (DAM) errors (or) vendor-specific")
},
{
  "run-out-cancel",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Run Out Cancel"),
  N_("Number of ECC errors")
},
{
  "shock-count-write-open",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Soft ECC correction"),
  N_("Number of errors corrected by software ECC")
},
{
  "shock-rate-write-open",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Thermal Asperity Rate"),
  N_("Number of Thermal Asperity Rate errors")
},
{
  "flying-height",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Flying Height"),
  N_("Height of heads above the disk surface")
},
{
  "spin-high-current",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Spin High Current"),
  N_("Amount of high current used to spin up the drive")
},
{
  "spin-buzz",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Spin Buzz"),
  N_("Number of buzz routines to spin up the drive")
},
{
  "offline-seek-performance",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Offline Seek Performance"),
  N_("Drive's seek performance during offline operations")
},
{
  "disk-shift",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Disk Shift"),
  N_("Shift of disk is possible as a result of strong shock loading in the store, "
     "as a result of falling (or) temperature")
},
{
  "g-sense-error-rate-2",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("G-sense Error Rate"),
  N_("Number of errors as a result of impact loads as detected by a shock sensor")
},
{
  "loaded-hours",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Loaded Hours"),
  N_("Number of hours in general operational state")
},
{
  "load-retry-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Load/Unload Retry Count"),
  N_("Loading on drive caused by numerous recurrences of operations, like reading, "
     "recording, positioning of heads, etc")
},
{
  "load-friction",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Load Friction"),
  N_("Load on drive caused by friction in mechanical parts of the store")
},
{
  "load-cycle-count-2",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Load/Unload Cycle Count"),
  N_("Total number of load cycles")
},
{
  "load-in-time",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Load-in Time"),
  N_("General time for loading in a drive")
},
{
  "torq-amp-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Torque Amplification Count"),
  N_("Quantity efforts of the rotating moment of a drive")
},
{
  "power-off-retract-count-2",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Power-off Retract Count"),
  N_("Number of power-off retract events")
},
{
  "head-amplitude",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("GMR Head Amplitude"),
  N_("Amplitude of heads trembling (GMR-head) in running mode")
},
{
  "temperature-celsius",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Temperature"),
  N_("Temperature of the drive")
},
{
  "endurance-remaining",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Endurance Remaining"),
  N_("Number of physical erase cycles completed on the drive as "
     "a percentage of the maximum physical erase cycles the drive supports")
},
{
  "power-on-seconds-2",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Power-On Hours"),
  N_("Number of hours elapsed in the power-on state")
},
{
  "uncorrectable-ecc-count",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Uncorrectable ECC Count"),
  N_("Number of uncorrectable ECC errors")
},
{
  "good-block-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Good Block Rate"),
  N_("Number of available reserved blocks as a percentage "
     "of the total number of reserved blocks"),
},
{
  "head-flying-hours",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Head Flying Hours"),
  N_("Time while head is positioning")
},
{
  "read-error-retry-rate",
  /* Translators: SMART attribute, see http://smartmontools.sourceforge.net/doc.html
   * or the next string for a longer explanation.
   */
  N_("Read Error Retry Rate"),
  N_("Number of errors while reading from a disk")
},
{
  NULL,
  NULL,
  NULL
}
};

/* TODO: move to libudisks2 */
static gboolean
attribute_get_details (const gchar  *name,
                       const gchar **out_name,
                       const gchar **out_description)
{
  SmartDetails *details;
  static volatile gsize have_hash = 0;
  static GHashTable *smart_details_map = NULL;
  gboolean ret;

  if (g_once_init_enter (&have_hash))
    {
      guint n;
      smart_details_map = g_hash_table_new (g_str_hash, g_str_equal);
      for (n = 0; smart_details[n].name != NULL; n++)
        {
          g_hash_table_insert (smart_details_map,
                               (gpointer) smart_details[n].name,
                               (gpointer) &(smart_details[n]));
        }
      g_once_init_leave (&have_hash, 1);
    }

  ret = FALSE;

  details = g_hash_table_lookup (smart_details_map, name);
  if (details != NULL)
    {
      if (out_name != NULL)
        *out_name = gettext (details->pretty_name);
      if (out_description != NULL)
        *out_description = gettext (details->desc);;
      ret = TRUE;
    }

  return ret;
}


/* ---------------------------------------------------------------------------------------------------- */

static gchar *
attr_format_long_desc (gint id, const gchar *name)
{
  const gchar *localized_name;
  const gchar *localized_desc;
  gchar *ret;

  if (!attribute_get_details (name, &localized_name, &localized_desc))
    {
      ret = g_strdup_printf (_("No description for attribute %d"), id);
    }
  else
    {
      ret = g_strdup (localized_desc);
    }

  return ret;
}

static gchar *
attr_format_desc (gint id, const gchar *name)
{
  const gchar *localized_name;
  const gchar *localized_desc;
  gchar *ret;

  if (!attribute_get_details (name, &localized_name, &localized_desc))
    {
      localized_name = name;
      localized_desc = "";
    }

  ret = g_strdup (localized_name);

  return ret;
}

static gchar *
attr_format_assessment (gint     current,
                        gint     worst,
                        gint     threshold,
                        guint16  flags)
{
  gchar *ret;
  gboolean failed = FALSE;
  gboolean failed_in_the_past = FALSE;

  if (current > 0 && threshold > 0 && current <= threshold)
    failed = TRUE;

  if (worst > 0 && threshold > 0 && worst <= threshold)
    failed_in_the_past = TRUE;

  if (failed)
    {
      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      ret = g_strdup_printf ("<span foreground=\"#ff0000\"><b>%s</b></span>",
                             /* Translators: Shown in the treeview for a failing attribute */
                             _("FAILING"));
    }
  else if (failed_in_the_past)
    {
      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      ret = g_strdup_printf ("<span foreground=\"#ff0000\">%s</span>",
                             /* Translators: Shown in the treeview for an attribute that failed in the past */
                             _("Failed in the past"));
    }
  else
    {
      ret = g_strdup (_("OK"));
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
pretty_to_string (guint64 pretty,
                  gint    pretty_unit)
{
  gchar *ret;
  gdouble celcius;
  gdouble fahrenheit;

  switch (pretty_unit)
    {
    case 2: /* SK_SMART_ATTRIBUTE_UNIT_MSECONDS */
      ret = gdu_utils_format_duration_usec (pretty * 1000,
                                            GDU_FORMAT_DURATION_FLAGS_SUBSECOND_PRECISION);
      break;

    case 3: /* SK_SMART_ATTRIBUTE_UNIT_SECTORS */
      /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
       * for a sector-based unit */
      ret = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                        "%d sector",
                                        "%d sectors",
                                        (gint) pretty),
                             (gint) pretty);
      break;

    case 4: /* SK_SMART_ATTRIBUTE_UNIT_MKELVIN */
      celcius = pretty / 1000.0 - 273.15;
      fahrenheit = 9.0 * celcius / 5.0 + 32.0;
      /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
       * for a temperature-based unit - first %f is the temperature in degrees Celcius, second %f
       * is the temperature in degrees Fahrenheit */
      ret = g_strdup_printf (_("%.0f° C / %.0f° F"), celcius, fahrenheit);
      break;

    case 1: /* SK_SMART_ATTRIBUTE_UNIT_NONE */
      ret = g_strdup_printf ("%" G_GUINT64_FORMAT, pretty);
      break;

    default:
    case 0: /* SK_SMART_ATTRIBUTE_UNIT_UNKNOWN */
      /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
       * where the value cannot be interpreted */
      ret = g_strdup (_("N/A"));
      break;
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* called whenever a new attribute is selected */
static void
update_attr (DialogData *data)
{
  gboolean prefail;
  gboolean online;
  const gchar *prefail_str;
  const gchar *online_str;
  gchar *pretty = NULL;
  gchar *type_str = NULL;
  gint normalized, threshold, worst;
  gint flags;
  GtkTreeIter tree_iter;
  gchar *value_str = NULL;
  gchar *long_description_str = NULL;

  if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->attributes_treeview)),
                                        NULL,
                                        &tree_iter))
    goto out;

  gtk_tree_model_get (GTK_TREE_MODEL (data->attributes_list),
                      &tree_iter,
                      PRETTY_COLUMN, &pretty,
                      NORMALIZED_COLUMN, &normalized,
                      THRESHOLD_COLUMN, &threshold,
                      WORST_COLUMN, &worst,
                      FLAGS_COLUMN, &flags,
                      LONG_DESC_COLUMN, &long_description_str,
                      -1);

  /* Translators: The first %s is the pretty value (such as '300
   * sectors' or '2.5 years' or '53° C / 127° F').
   *
   * The three %d are the normalized, threshold and worst values -
   * these are all decimal numbers.
   */
  value_str = g_strdup_printf (_("%s <span size=\"small\">(Normalized: %d, Threshold: %d, Worst: %d)</span>"),
                               pretty, normalized, threshold, worst);

  prefail = (flags & 0x0001);
  online = (flags & 0x0002);

  if (prefail)
    {
      /* Translators: Please keep "(Pre-Fail)" in English
       */
      prefail_str = _("Failure is a sign the disk will fail within 24 hours <span size=\"small\">(Pre-Fail)</span>");
    }
  else
    {
      /* Translators: Please keep "(Old-Age)" in English
       */
      prefail_str = _("Failure is a sign the disk exceeded its intended design life period <span size=\"small\">(Old-Age)</span>");
    }

  if (online)
    {
      /* Translators: Please keep "(Online)" in English
       */
      online_str = _("Updated every time data is collected <span size=\"small\">(Online)</span>");
    }
  else
    {
      /* Translators: Please keep "(Not Online)" in English
       */
      online_str = _("Updated only during off-line activities <span size=\"small\">(Not Online)</span>");
    }

  type_str = g_strdup_printf ("%s\n%s",
                              prefail_str,
                              online_str);

 out:
  gtk_label_set_markup (GTK_LABEL (data->attr_value_label), value_str);
  gtk_label_set_markup (GTK_LABEL (data->attr_type_label), type_str);
  gtk_label_set_markup (GTK_LABEL (data->attr_long_description_label), long_description_str);

  g_free (long_description_str);
  g_free (value_str);
  g_free (type_str);
  g_free (pretty);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
calculate_self_test (UDisksDriveAta *ata,
                     gboolean       *out_selftest_running)
{
  const gchar *s;
  gchar *ret;
  gboolean selftest_running = FALSE;

  s = udisks_drive_ata_get_smart_selftest_status (ata);
  if (g_strcmp0 (s, "success") == 0)
    {
      ret = g_strdup (C_("smart-self-test-result", "Last self-test completed successfully"));
    }
  else if (g_strcmp0 (s, "aborted") == 0)
    {
      ret = g_strdup (C_("smart-self-test-result", "Last self-test was aborted"));
    }
  else if (g_strcmp0 (s, "interrupted") == 0)
    {
      ret = g_strdup (C_("smart-self-test-result", "Last self-test was interrupted"));
    }
  else if (g_strcmp0 (s, "fatal") == 0)
    {
      ret = g_strdup (C_("smart-self-test-result", "Last self-test did not complete"));
    }
  else if (g_strcmp0 (s, "error_unknown") == 0)
    {
      ret = g_strdup (C_("smart-self-test-result", "Last self-test failed"));
    }
  else if (g_strcmp0 (s, "error_electrical") == 0)
    {
      /* Translators: shown when the last self-test failed and the problem is with the electrical subsystem */
      ret = g_strdup (C_("smart-self-test-result", "Last self-test failed (electrical)"));
    }
  else if (g_strcmp0 (s, "error_servo") == 0)
    {
      /* Translators: shown when the last self-test failed and the problem is with the servo subsystem - see http://en.wikipedia.org/wiki/Servomechanism */
      ret = g_strdup (C_("smart-self-test-result", "Last self-test failed (servo)"));
    }
  else if (g_strcmp0 (s, "error_read") == 0)
    {
      /* Translators: shown when the last self-test failed and the problem is with the reading subsystem - */
      ret = g_strdup (C_("smart-self-test-result", "Last self-test failed (read)"));
    }
  else if (g_strcmp0 (s, "error_handling") == 0)
    {
      /* Translators: shown when the last self-test failed and the disk is suspected of having handling damage (e.g. physical damage to the hard disk) */
      ret = g_strdup (C_("smart-self-test-result", "Last self-test failed (handling)"));
    }
  else if (g_strcmp0 (s, "inprogress") == 0)
    {
      /* Translators: shown when a self-test is in progress. The first %d is the percentage of the test remaining. */
      ret = g_strdup_printf (C_("smart-self-test-result", "Self-test in progress — %d%% remaining"),
                             udisks_drive_ata_get_smart_selftest_percent_remaining (ata));
      selftest_running = TRUE;
    }
  else
    {
      /* Translators: Shown when a self-test is not unknown. The %s is the result-code from the API code. */
      ret = g_strdup_printf (C_("smart-self-test-result", "Unknown (%s)"), s);
    }

  if (out_selftest_running)
    *out_selftest_running = selftest_running;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_updated_label (DialogData *data)
{
  time_t now;
  time_t updated;
  gchar *s;
  gchar *s2;

  now = time (NULL);
  updated = udisks_drive_ata_get_smart_updated (data->ata);
  s = gdu_utils_format_duration_usec ((now - updated) * G_USEC_PER_SEC,
                                      GDU_FORMAT_DURATION_FLAGS_NONE);
  s2 = g_strdup_printf (_("%s ago"), s);
  gtk_label_set_text (GTK_LABEL (data->updated_label), s2);
  g_free (s2);
  g_free (s);
}

static gchar *
format_temp (UDisksDriveAta *ata)
{
  gdouble temp;
  gchar *ret = NULL;

  temp = udisks_drive_ata_get_smart_temperature (ata);
  if (temp > 1.0)
    {
      gdouble celcius;
      gdouble fahrenheit;

      celcius = temp - 273.15;
      fahrenheit = 9.0 * celcius / 5.0 + 32.0;
      /* Translators: Used to format a temperature.
       * The first %f is the temperature in degrees Celcius and
       * the second %f is the temperature in degrees Fahrenheit.
       */
      ret = g_strdup_printf (_("%.0f° C / %.0f° F"), celcius, fahrenheit);
    }
  return ret;
}

static gchar *
format_powered_on (UDisksDriveAta *ata)
{
  guint64 secs;
  gchar *ret = NULL;

  secs = udisks_drive_ata_get_smart_power_on_seconds (ata);
  if (secs > 0)
    ret = gdu_utils_format_duration_usec (secs * G_USEC_PER_SEC,
                                          GDU_FORMAT_DURATION_FLAGS_NONE);
  return ret;
}

static gchar *
gdu_ata_smart_get_overall_assessment (UDisksDriveAta *ata,
                                      gboolean        one_liner,
                                      gboolean       *out_smart_is_supported,
                                      gboolean       *out_warn)
{
  gchar *ret;
  gint num_failing;
  gint num_failed_in_the_past;
  gint num_bad_sectors;
  gboolean smart_is_supported = FALSE;
  gboolean warn = FALSE;
  gchar *selftest = NULL;

  if (!udisks_drive_ata_get_smart_supported (ata))
    {
      ret = g_strdup (_("SMART is not supported"));
      goto out_no_smart;
    }

  if (!udisks_drive_ata_get_smart_enabled (ata))
    {
      ret = g_strdup (_("SMART is not enabled"));
      goto out_no_smart;
    }

  smart_is_supported = TRUE;

  num_failing = udisks_drive_ata_get_smart_num_attributes_failing (ata);
  num_failed_in_the_past = udisks_drive_ata_get_smart_num_attributes_failed_in_the_past (ata);
  num_bad_sectors = udisks_drive_ata_get_smart_num_bad_sectors (ata);

  if (g_strcmp0 (udisks_drive_ata_get_smart_selftest_status (ata), "inprogress") == 0)
    {
      selftest = g_strdup (_("Self-test in progress"));
    }

  /* If self-assessment indicates failure, always return that */
  if (udisks_drive_ata_get_smart_failing (ata))
    {
      /* if doing a one-liner also include if a self-test is running */
      if (one_liner && selftest != NULL)
        {
          /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
           * of hard-coding the color
           */
          ret = g_strdup_printf ("<span foreground=\"#ff0000\"><b>%s</b></span> — %s",
                                 _("DISK IS LIKELY TO FAIL SOON"),
                                 selftest);
        }
      else
        {
          /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
           * of hard-coding the color
           */
          ret = g_strdup_printf ("<span foreground=\"#ff0000\"><b>%s</b></span>",
                                 _("DISK IS LIKELY TO FAIL SOON"));
        }
      warn = TRUE;
      goto out;
    }

  /* Ok, self-assessment is good.. so if doing a self-test, prefer that to attrs / bad sectors
   * on the one-liner
   */
  if (one_liner && selftest != NULL)
    {
      ret = selftest;
      selftest = NULL;
      goto out;
    }

  /* Otherwise, if last self-test failed, return that for the one-liner */
  if (g_str_has_prefix (udisks_drive_ata_get_smart_selftest_status (ata), "error"))
    {
      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      ret = g_strdup_printf ("<span foreground=\"#ff0000\"><b>%s</b></span>",
                             _("SELF-TEST FAILED"));
      warn = TRUE;
      goto out;
    }

  /* Otherwise, if an attribute is failing, return that */
  if (num_failing > 0)
    {
      ret = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                        "Disk is OK, one failing attribute is failing",
                                        "Disk is OK, %d attributes are failing",
                                        num_failing),
                             num_failing);
      goto out;
    }

  /* Otherwise, if bad sectors have been detected, return that */
  if (num_bad_sectors > 0)
    {
      ret = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                        "Disk is OK, one bad sector",
                                        "Disk is OK, %d bad sectors",
                                        num_bad_sectors),
                             num_bad_sectors);
      goto out;
    }

  /* Otherwise, if an attribute has failed in the past return that */
  if (num_failed_in_the_past > 0)
    {
      ret = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                        "Disk is OK, one attribute failed in the past",
                                        "Disk is OK, %d attributes failed in the past",
                                        num_failed_in_the_past),
                             num_failed_in_the_past);
      goto out;
    }

  /* Otherwise, it's all honky dory */

  ret = g_strdup (_("Disk is OK"));

 out:

  if (one_liner)
    {
      gchar *s, *s1;
      s = format_temp (ata);
      if (s != NULL)
        {
          /* Translators: Used to convey the status and temperature in one line.
           * The first %s is the status of the drive.
           * The second %s is the temperature of the drive.
           */
          s1 = g_strdup_printf (_("%s (%s)"), ret, s);
          g_free (ret);
          ret = s1;
        }
    }

 out_no_smart:
  g_free (selftest);
  if (out_smart_is_supported != NULL)
    *out_smart_is_supported = smart_is_supported;
  if (out_warn != NULL)
    *out_warn = warn;
  return ret;
}

gchar *
gdu_ata_smart_get_one_liner_assessment (UDisksDriveAta *ata,
                                        gboolean       *out_smart_is_supported,
                                        gboolean       *out_warn)
{
  return gdu_ata_smart_get_overall_assessment (ata, TRUE, out_smart_is_supported, out_warn);
}

static void
update_dialog (DialogData *data)
{
  gchar *s;
  GVariant *attributes = NULL;
  GError *error;
  GtkTreeIter tree_iter;
  GtkTreeIter *tree_iter_to_select;
  gint selected_id;
  gboolean selftest_running;

  /* TODO: do it async and show spinner while call is pending */
  error = NULL;
  if (!udisks_drive_ata_call_smart_get_attributes_sync (udisks_object_peek_drive_ata (data->object),
                                                        g_variant_new ("a{sv}", NULL), /* options */
                                                        &attributes,
                                                        NULL, /* GCancellable */
                                                        &error))
    {
      g_warning ("Error getting ATA SMART information: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }

  update_updated_label (data);

  s = calculate_self_test (data->ata, &selftest_running);
  gtk_label_set_text (GTK_LABEL (data->self_test_label), s);
  g_free (s);

  if (selftest_running)
    {
      gtk_widget_set_visible (data->start_selftest_button, FALSE);
      gtk_widget_set_visible (data->stop_selftest_button, TRUE);
    }
  else
    {
      gtk_widget_set_visible (data->start_selftest_button, TRUE);
      gtk_widget_set_visible (data->stop_selftest_button, FALSE);
    }

  /* record currently selected row so we can reselect it */
  selected_id = -1;
  tree_iter_to_select = NULL;
  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->attributes_treeview)),
                                       NULL,
                                       &tree_iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (data->attributes_list),
                          &tree_iter,
                          ID_COLUMN, &selected_id,
                          -1);
    }

  gtk_list_store_clear (data->attributes_list);
  if (attributes != NULL)
    {
      GVariantIter iter;
      guchar id;
      const gchar *name;
      guint16 flags;
      gint current, worst, threshold;
      guint64 pretty; gint pretty_unit;
      GVariant *expansion;

      g_variant_iter_init (&iter, attributes);
      while (g_variant_iter_next (&iter,
                                  "(y&sqiiixi@a{sv})",
                                  &id,
                                  &name,
                                  &flags,
                                  &current, &worst, &threshold,
                                  &pretty, &pretty_unit,
                                  &expansion))
        {
          GtkTreeIter titer;
          gchar *long_desc_str;
          gchar *desc_str;
          gchar *assessment_str;
          gchar *pretty_str;

          desc_str = attr_format_desc (id, name);
          long_desc_str = attr_format_long_desc (id, name);
          assessment_str = attr_format_assessment (current, worst, threshold, flags);
          pretty_str = pretty_to_string (pretty, pretty_unit);

          gtk_list_store_append (data->attributes_list, &titer);
          gtk_list_store_set (data->attributes_list, &titer,
                              ID_COLUMN, (gint) id,
                              DESC_COLUMN, desc_str,
                              LONG_DESC_COLUMN, long_desc_str,
                              ASSESSMENT_COLUMN, assessment_str,
                              PRETTY_COLUMN, pretty_str,
                              NORMALIZED_COLUMN, current,
                              THRESHOLD_COLUMN, threshold,
                              WORST_COLUMN, worst,
                              FLAGS_COLUMN, flags,
                              -1);

          if (id == selected_id)
            tree_iter_to_select = gtk_tree_iter_copy (&titer);

          g_free (long_desc_str);
          g_free (desc_str);
          g_free (assessment_str);
          g_free (pretty_str);

          g_variant_unref (expansion);
        }
    }

  /* reselect the previously selected row */
  if (tree_iter_to_select != NULL)
    {
      gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->attributes_treeview)),
                                      tree_iter_to_select);
      gtk_tree_iter_free (tree_iter_to_select);
    }
  else
    {
      GtkTreeIter titer;
      /* or the first row, if the previously selected one does not exist anymore */
      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (data->attributes_list), &titer))
        {
          gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->attributes_treeview)),
                                          &titer);
        }
    }

  if (udisks_drive_ata_get_smart_failing (data->ata))
    {
      /* Translators: XXX */
      s = g_strdup (_("Threshold exceeded"));
    }
  else
    {
      /* Translators: XXX */
      s = g_strdup (_("Threshold not exceeded"));
    }
  gtk_label_set_markup (GTK_LABEL (data->self_assessment_label), s);
  g_free (s);

  s = format_powered_on (data->ata);
  if (s == NULL)
    s = g_strdup ("—");
  gtk_label_set_markup (GTK_LABEL (data->powered_on_label), s);
  g_free (s);

  s = format_temp (data->ata);
  if (s == NULL)
    s = g_strdup ("—");
  gtk_label_set_markup (GTK_LABEL (data->temperature_label), s);
  g_free (s);

  s = gdu_ata_smart_get_overall_assessment (data->ata, FALSE, NULL, NULL);
  gtk_label_set_markup (GTK_LABEL (data->overall_assessment_label), s);
  g_free (s);

  if (attributes != NULL)
    g_variant_unref (attributes);
}

/* called when properties on the Drive.Ata object changes */
static void
on_ata_notify (GObject     *object,
               GParamSpec  *pspec,
               gpointer     user_data)
{
  DialogData *data = user_data;
  update_dialog (data);
}

/* called every second */
static gboolean
on_timeout (gpointer user_data)
{
  DialogData *data = user_data;
  update_updated_label (data);
  return TRUE; /* keep timeout around */
}


/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_cb (UDisksDriveAta  *ata,
            GAsyncResult    *res,
            gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_drive_ata_call_smart_update_finish (ata, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error refreshing SMART data"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}


static void
refresh_do (DialogData  *data)
{
  udisks_drive_ata_call_smart_update (data->ata,
                                      g_variant_new ("a{sv}", NULL), /* options */
                                      NULL, /* GCancellable */
                                      (GAsyncReadyCallback) refresh_cb,
                                      g_object_ref (data->window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
smart_cancel_cb (UDisksDriveAta  *ata,
                 GAsyncResult    *res,
                 gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_drive_ata_call_smart_selftest_abort_finish (ata, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error aborting SMART self-test"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
smart_start_cb (UDisksDriveAta  *ata,
                GAsyncResult    *res,
                gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_drive_ata_call_smart_selftest_start_finish (ata, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error starting SMART self-test"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}


static void
selftest_do (DialogData  *data,
             const gchar *type)
{
  if (g_strcmp0 (type, "abort") == 0)
    {
      udisks_drive_ata_call_smart_selftest_abort (data->ata,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* GCancellable */
                                                  (GAsyncReadyCallback) smart_cancel_cb,
                                                  g_object_ref (data->window));
    }
  else
    {
      udisks_drive_ata_call_smart_selftest_start (data->ata,
                                                  type,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* GCancellable */
                                                  (GAsyncReadyCallback) smart_start_cb,
                                                  g_object_ref (data->window));
    }
}

static void
on_selftest_short (GtkMenuItem *menu_item,
                   gpointer     user_data)
{
  DialogData *data = user_data;
  selftest_do (data, "short");
}

static void
on_selftest_extended (GtkMenuItem *menu_item,
                      gpointer     user_data)
{
  DialogData *data = user_data;
  selftest_do (data, "extended");
}

static void
on_selftest_conveyance (GtkMenuItem *menu_item,
                        gpointer     user_data)
{
  DialogData *data = user_data;
  selftest_do (data, "conveyance");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_tree_selection_changed (GtkTreeSelection *tree_selection,
                           gpointer          user_data)
{
  DialogData *data = user_data;
  update_attr (data);
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_ata_smart_dialog_show (GduWindow    *window,
                           UDisksObject *object)
{
  DialogData *data;
  guint n;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  gulong notify_id;
  guint timeout_id;

  data = g_new0 (DialogData, 1);
  data->object = g_object_ref (object);
  data->ata = udisks_object_peek_drive_ata (data->object);
  data->window = g_object_ref (window);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "smart-dialog.ui",
                                                         "dialog1",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = GTK_WIDGET (gtk_builder_get_object (data->builder, widget_mapping[n].name));
    }

  data->attributes_list = gtk_list_store_new (N_COLUMNS,
                                              G_TYPE_INT,         /* id */
                                              G_TYPE_STRING,      /* desc */
                                              G_TYPE_STRING,      /* long_desc */
                                              G_TYPE_STRING,      /* assessment */
                                              G_TYPE_STRING,      /* pretty */
                                              G_TYPE_INT,         /* normalized */
                                              G_TYPE_INT,         /* threshold */
                                              G_TYPE_INT,         /* worst */
                                              G_TYPE_INT);        /* flags */
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->attributes_list),
                                        ID_COLUMN,
                                        GTK_SORT_ASCENDING);
  gtk_tree_view_set_model (GTK_TREE_VIEW (data->attributes_treeview),
                           GTK_TREE_MODEL (data->attributes_list));

  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (data->attributes_treeview), TRUE);
  gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (data->attributes_treeview), LONG_DESC_COLUMN);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the Attribute ID (0-255) */
  gtk_tree_view_column_set_title (column, _("ID"));
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", ID_COLUMN, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the attribute name and description */
  gtk_tree_view_column_set_title (column, _("Attribute"));
  gtk_tree_view_column_set_expand (column, TRUE);
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", DESC_COLUMN, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the value */
  gtk_tree_view_column_set_title (column, _("Value"));
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", PRETTY_COLUMN, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the assessment of the attribute */
  gtk_tree_view_column_set_title (column, _("Assessment"));
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", ASSESSMENT_COLUMN, NULL);

  column = gtk_tree_view_column_new ();


  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->attributes_treeview)),
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));

  notify_id = g_signal_connect (data->ata, "notify", G_CALLBACK (on_ata_notify), data);
  timeout_id = g_timeout_add_seconds (1, on_timeout, data);

  g_signal_connect (data->selftest_short_menuitem, "activate", G_CALLBACK (on_selftest_short), data);
  g_signal_connect (data->selftest_extended_menuitem, "activate", G_CALLBACK (on_selftest_extended), data);
  g_signal_connect (data->selftest_conveyance_menuitem, "activate", G_CALLBACK (on_selftest_conveyance), data);

  data->start_selftest_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (data->dialog), 0);
  data->stop_selftest_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (data->dialog), 1);

  update_dialog (data);

  while (TRUE)
    {
      gint response;
      response = gtk_dialog_run (GTK_DIALOG (data->dialog));
      /* Keep in sync with .ui file */
      switch (response)
        {
        case 0:
          gtk_menu_popup (GTK_MENU (data->selftest_menu), NULL, NULL, NULL, NULL, 1, 0);
          break;
        case 1:
          selftest_do (data, "abort");
          break;
        case 2:
          refresh_do (data);
          break;
        }

      if (response < 0)
        break;
    }

  g_source_remove (timeout_id);
  g_signal_handler_disconnect (data->ata, notify_id);

  dialog_data_free (data);
}
