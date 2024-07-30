/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gdu-application.h"
#include "gdu-window.h"
#include "gdu-ata-smart-dialog.h"

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
  TYPE_COLUMN,
  UPDATES_COLUMN,
  FLAGS_COLUMN,
  N_COLUMNS,
};

typedef struct
{
  volatile guint ref_count;

  UDisksClient *client;
  UDisksObject *object;
  UDisksDriveAta *ata;

  GtkWindow *window;
  GtkBuilder *builder;

  GtkListStore *attributes_list;

  GtkWidget *enabled_switch;
  GtkWidget *status_grid;
  GtkWidget *attributes_label;
  GtkWidget *attributes_vbox;

  GtkWidget *dialog;
  GtkWidget *updated_label;
  GtkWidget *temperature_label;
  GtkWidget *powered_on_label;
  GtkWidget *self_test_label;
  GtkWidget *self_assessment_label;
  GtkWidget *overall_assessment_label;

  GtkWidget *attributes_treeview;

  GtkWidget *start_selftest_button;
  GtkWidget *stop_selftest_button;
  GtkWidget *refresh_button;

  guint timeout_id;
  gulong notify_id;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, enabled_switch), "enabled-switch"},

  {G_STRUCT_OFFSET (DialogData, status_grid), "status-grid"},
  {G_STRUCT_OFFSET (DialogData, attributes_label), "attributes-label"},
  {G_STRUCT_OFFSET (DialogData, attributes_vbox), "attributes-vbox"},

  {G_STRUCT_OFFSET (DialogData, updated_label), "updated-label"},
  {G_STRUCT_OFFSET (DialogData, temperature_label), "temperature-label"},
  {G_STRUCT_OFFSET (DialogData, powered_on_label), "powered-on-label"},
  {G_STRUCT_OFFSET (DialogData, self_test_label), "self-test-label"},
  {G_STRUCT_OFFSET (DialogData, self_assessment_label), "self-assessment-label"},
  {G_STRUCT_OFFSET (DialogData, overall_assessment_label), "overall-assessment-label"},
  {G_STRUCT_OFFSET (DialogData, attributes_treeview), "attributes-treeview"},

  {G_STRUCT_OFFSET (DialogData, start_selftest_button), "start-selftest-button"},
  {G_STRUCT_OFFSET (DialogData, stop_selftest_button), "stop-selftest-button"},
  {G_STRUCT_OFFSET (DialogData, refresh_button), "refresh-button"},
  {0, NULL}
};


static DialogData *
dialog_data_ref (DialogData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
dialog_data_unref (DialogData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      if (data->dialog != NULL)
        {
          adw_dialog_close (ADW_DIALOG (data->dialog));
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
     "as “reallocated” and transfers data to a special reserved area (spare area)")
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
  N_("Drive’s seek performance during offline operations")
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
  "total-lbas-written",
  /* Translators: SMART attribute, see https://en.wikipedia.org/wiki/S.M.A.R.T.#Known_ATA_S.M.A.R.T._attributes
   * or the next string for a longer explanation.
   */
  N_("Total LBAs Written"),
  N_("The amount of data written during the lifetime of the disk")
},
{
  "total-lbas-read",
  /* Translators: SMART attribute, see https://en.wikipedia.org/wiki/S.M.A.R.T.#Known_ATA_S.M.A.R.T._attributes
   * or the next string for a longer explanation.
   */
  N_("Total LBAs Read"),
  N_("The amount of data read during the lifetime of the disk")
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
                       const gchar **out_desc)
{
  SmartDetails *details;
  static gsize have_hash = 0;
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
      if (out_desc != NULL)
        *out_desc = gettext (details->desc);
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
  gchar *ret;

  if (!attribute_get_details (name, &localized_name, NULL))
    {
      localized_name = name;
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
      ret = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                        /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                                         * for a sector-based unit */
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
  gchar *s = NULL;
  if (udisks_drive_ata_get_smart_enabled (data->ata))
    {
      time_t now;
      time_t updated;
      gchar *s2;

      now = time (NULL);
      updated = udisks_drive_ata_get_smart_updated (data->ata);
      s2 = gdu_utils_format_duration_usec ((now - updated) * G_USEC_PER_SEC,
                                           GDU_FORMAT_DURATION_FLAGS_NO_SECONDS);
      s = g_strdup_printf (_("%s ago"), s2);
      g_free (s2);
    }
  else
    {
      s = g_strdup ("—");
    }
  gtk_label_set_text (GTK_LABEL (data->updated_label), s);
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

  smart_is_supported = TRUE;

  if (!udisks_drive_ata_get_smart_enabled (ata))
    {
      ret = g_strdup (_("SMART is not enabled"));
      goto out_no_smart;
    }

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
          g_free (s);
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

/* ---------------------------------------------------------------------------------------------------- */

static void
update_attributes_list (DialogData *data,
                        GVariant   *attributes)
{
  GtkTreeIter tree_iter;
  GtkTreeIter *tree_iter_to_select;
  gint selected_id;

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
          gchar *current_str;
          gchar *threshold_str;
          gchar *worst_str;
          const gchar *type_str;
          const gchar *updates_str;
          const gchar *na_str;

          desc_str = attr_format_desc (id, name);
          long_desc_str = attr_format_long_desc (id, name);
          assessment_str = attr_format_assessment (current, worst, threshold, flags);
          pretty_str = pretty_to_string (pretty, pretty_unit);

          if (flags & 0x0001)
            type_str = _("Pre-Fail");
          else
            type_str = _("Old-Age");

          if (flags & 0x0002)
            updates_str = _("Online");
          else
            updates_str = _("Offline");

          /* Translators: Shown for normalized values (current, worst, threshold) if the value is
           * not applicable, e.g. meaningless. See http://en.wikipedia.org/wiki/N/A
           */
          na_str = _("N/A");
          current_str   = (current == -1   ? g_strdup (na_str) : g_strdup_printf ("%d", current));
          threshold_str = (threshold == -1 ? g_strdup (na_str) : g_strdup_printf ("%d", threshold));
          worst_str     = (worst == -1     ? g_strdup (na_str) : g_strdup_printf ("%d", worst));

          gtk_list_store_append (data->attributes_list, &titer);
          gtk_list_store_set (data->attributes_list, &titer,
                              ID_COLUMN, (gint) id,
                              DESC_COLUMN, desc_str,
                              LONG_DESC_COLUMN, long_desc_str,
                              ASSESSMENT_COLUMN, assessment_str,
                              PRETTY_COLUMN, pretty_str,
                              NORMALIZED_COLUMN, current_str,
                              THRESHOLD_COLUMN, threshold_str,
                              WORST_COLUMN, worst_str,
                              TYPE_COLUMN, type_str,
                              UPDATES_COLUMN, updates_str,
                              FLAGS_COLUMN, flags,
                              -1);

          if (id == selected_id)
            tree_iter_to_select = gtk_tree_iter_copy (&titer);

          g_free (desc_str);
          g_free (long_desc_str);
          g_free (assessment_str);
          g_free (pretty_str);
          g_free (current_str);
          g_free (threshold_str);
          g_free (worst_str);

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
}

static void
update_dialog (DialogData *data)
{
  gchar *s;
  gboolean enabled = FALSE;

  enabled = udisks_drive_ata_get_smart_enabled (data->ata);
  gtk_switch_set_active (GTK_SWITCH (data->enabled_switch), enabled);

  if (enabled)
    {
      GVariant *attributes = NULL;
      GError *error = NULL;
      gboolean selftest_running = FALSE;

      /* TODO: do it async and show spinner while call is pending */
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
      gtk_widget_set_visible (data->refresh_button, TRUE);

      update_attributes_list (data, attributes);
      update_updated_label (data);

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
  else
    {
      gtk_widget_set_visible (data->start_selftest_button, FALSE);
      gtk_widget_set_visible (data->stop_selftest_button, FALSE);
      gtk_widget_set_visible (data->refresh_button, FALSE);
      update_attributes_list (data, NULL);
      update_updated_label (data);
      gtk_label_set_markup (GTK_LABEL (data->self_test_label), "—");
      gtk_label_set_markup (GTK_LABEL (data->temperature_label), "—");
      gtk_label_set_markup (GTK_LABEL (data->powered_on_label), "—");
      gtk_label_set_markup (GTK_LABEL (data->self_assessment_label), "—");
      gtk_label_set_markup (GTK_LABEL (data->overall_assessment_label), "—");
    }
  gtk_widget_set_sensitive (data->status_grid, enabled);
  gtk_widget_set_sensitive (data->attributes_label, enabled);
  gtk_widget_set_sensitive (data->attributes_vbox, enabled);
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
on_selftest_short (GAction     *action,
                   GVariant    *parameter,
                   gpointer     user_data)
{
  DialogData *data = user_data;
  selftest_do (data, "short");
}

static void
on_selftest_extended (GAction     *action,
                      GVariant    *parameter,
                      gpointer     user_data)
{
  DialogData *data = user_data;
  selftest_do (data, "extended");
}

static void
on_selftest_conveyance (GAction     *action,
                      GVariant    *parameter,
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
  /* DialogData *data = user_data;*/
}

/* ---------------------------------------------------------------------------------------------------- */

static void
smart_set_enabled_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  DialogData *data = user_data;
  GError *error = NULL;

  if (!udisks_drive_ata_call_smart_set_enabled_finish (UDISKS_DRIVE_ATA (source_object),
                                                       res,
                                                       &error))
    {
      gdu_utils_show_error (data->window,
                            _("An error occurred when trying to toggle whether SMART is enabled"),
                            error);
      g_clear_error (&error);
    }
  udisks_client_settle (data->client);
  update_dialog (data);
  dialog_data_unref (data);
}

static void
on_enabled_switch_notify_active (GObject    *object,
                                 GParamSpec *pspec,
                                 gpointer    user_data)
{
  DialogData *data = user_data;
  gboolean enabled;

  enabled = gtk_switch_get_active (GTK_SWITCH (data->enabled_switch));
  if (!!enabled != !!udisks_drive_ata_get_smart_enabled (data->ata))
    {
      udisks_drive_ata_call_smart_set_enabled (data->ata,
                                               enabled,
                                               g_variant_new ("a{sv}", NULL), /* options */
                                               NULL, /* GCancellable */
                                               (GAsyncReadyCallback) smart_set_enabled_cb,
                                               dialog_data_ref (data));
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_refresh_clicked (GtkButton *button,
                    gpointer   user_data)
{
  DialogData *data = user_data;
  refresh_do (data);
}

static void
on_stop_clicked (GtkButton *button,
                 gpointer   user_data)
{
  DialogData *data = user_data;
  selftest_do (data, "abort");
}

static void
on_dialog_closed (AdwDialog *dialog,
                  gpointer   user_data)
{
  DialogData *data = user_data;

  adw_dialog_close (dialog);

  if (data->timeout_id) {
    g_source_remove (data->timeout_id);
    data->timeout_id = 0;
  }
  if (data->notify_id) {
    g_signal_handler_disconnect (data->ata, data->notify_id);
    data->notify_id = 0;
  }
  dialog_data_unref (data);
}

void
gdu_ata_smart_dialog_show (GtkWindow    *parent_window,
                           UDisksObject *object,
                           UDisksClient *client)
{
  DialogData *data;
  guint n;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GSimpleActionGroup *group;
  GSimpleAction *action;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->ata = udisks_object_peek_drive_ata (data->object);
  data->window = g_object_ref (parent_window);
  data->client = client;

  data->dialog = GTK_WIDGET (gdu_application_new_widget ((gpointer)g_application_get_default (),
                                                         "gdu-ata-smart-dialog.ui",
                                                         "smart-dialog",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = GTK_WIDGET (gtk_builder_get_object (data->builder, widget_mapping[n].name));
      g_warn_if_fail (*p != NULL);
    }

  data->attributes_list = gtk_list_store_new (N_COLUMNS,
                                              G_TYPE_INT,         /* id */
                                              G_TYPE_STRING,      /* desc */
                                              G_TYPE_STRING,      /* long_desc */
                                              G_TYPE_STRING,      /* assessment */
                                              G_TYPE_STRING,      /* pretty */
                                              G_TYPE_STRING,      /* normalized */
                                              G_TYPE_STRING,      /* threshold */
                                              G_TYPE_STRING,      /* worst */
                                              G_TYPE_STRING,      /* type */
                                              G_TYPE_STRING,      /* updates */
                                              G_TYPE_INT);        /* flags */
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->attributes_list),
                                        ID_COLUMN,
                                        GTK_SORT_ASCENDING);
  gtk_tree_view_set_model (GTK_TREE_VIEW (data->attributes_treeview),
                           GTK_TREE_MODEL (data->attributes_list));

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
  /* Translators: This string is used as the column title in the treeview for the normalized value */
  gtk_tree_view_column_set_title (column, _("Normalized"));
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", NORMALIZED_COLUMN, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the threshold value */
  gtk_tree_view_column_set_title (column, _("Threshold"));
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", THRESHOLD_COLUMN, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the worst value */
  gtk_tree_view_column_set_title (column, _("Worst"));
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", WORST_COLUMN, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the type */
  gtk_tree_view_column_set_title (column, _("Type"));
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", TYPE_COLUMN, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->attributes_treeview), column);
  /* Translators: This string is used as the column title in the treeview for the update type (Online / Offline) */
  gtk_tree_view_column_set_title (column, _("Updates"));
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_attributes (column, renderer,
                                       "markup", UPDATES_COLUMN, NULL);

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


  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->attributes_treeview)),
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    data);

  data->notify_id = g_signal_connect (data->ata, "notify", G_CALLBACK (on_ata_notify), data);
  data->timeout_id = g_timeout_add_seconds (1, on_timeout, data);

  group = g_simple_action_group_new ();

  action = g_simple_action_new ("short", NULL);
  g_signal_connect (action, "activate", G_CALLBACK (on_selftest_short), data);
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));

  action = g_simple_action_new ("extended", NULL);
  g_signal_connect (action, "activate", G_CALLBACK (on_selftest_extended), data);
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));

  action = g_simple_action_new ("conveyance", NULL);
  g_signal_connect (action, "activate", G_CALLBACK (on_selftest_conveyance), data);
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));

  gtk_widget_insert_action_group (GTK_WIDGET (data->dialog), "test", G_ACTION_GROUP (group));

  update_dialog (data);
  gtk_widget_grab_focus (data->attributes_treeview);

  g_signal_connect (data->enabled_switch, "notify::active", G_CALLBACK (on_enabled_switch_notify_active), data);

  g_signal_connect (data->stop_selftest_button, "clicked", G_CALLBACK (on_stop_clicked), data);
  g_signal_connect (data->refresh_button, "clicked", G_CALLBACK (on_refresh_clicked), data);
  g_signal_connect (data->dialog, "closed", G_CALLBACK (on_dialog_closed), data);

  adw_dialog_present (ADW_DIALOG (data->dialog), GTK_WIDGET (parent_window));
}
