/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ata-smart-dialog.c
 *
 * Copyright (C) 2009 David Zeuthen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <atasmart.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "gdu-time-label.h"
#include "gdu-ata-smart-dialog.h"
#include "gdu-spinner.h"
#include "gdu-pool-tree-model.h"

/* ---------------------------------------------------------------------------------------------------- */

struct GduAtaSmartDialogPrivate
{
        GduDrive *drive;
        GduDevice *device;

        guint64 last_updated;
        gulong device_changed_signal_handler_id;
        gulong device_job_changed_signal_handler_id;

        GduPoolTreeModel *pool_tree_model;
        GtkWidget *drive_combo_box;

        GtkWidget *updated_label;
        GtkWidget *updating_spinner;
        GtkWidget *updating_label;
        GtkWidget *update_link_label;

        GtkWidget *self_test_result_label;
        GtkWidget *self_test_progress_bar;
        GtkWidget *self_test_run_link_label;
        GtkWidget *self_test_cancel_link_label;

        GtkWidget *model_label;
        GtkWidget *firmware_label;
        GtkWidget *serial_label;
        GtkWidget *power_on_hours_label;
        GtkWidget *temperature_label;
        GtkWidget *sectors_label;
        GtkWidget *self_assessment_label;
        GtkWidget *overall_assessment_image;
        GtkWidget *overall_assessment_label;
        GtkWidget *no_warn_check_button;

        GtkWidget *tree_view;
        GtkListStore *attr_list_store;

        gboolean is_updating;

        gboolean has_been_constructed;
};

enum
{
        PROP_0,
        PROP_DRIVE,
};

/* ---------------------------------------------------------------------------------------------------- */

#define _GDU_TYPE_SK_ATTR (_gdu_type_sk_attr_get_type ())

static SkSmartAttributeParsedData *
_gdu_sk_attr_copy (const SkSmartAttributeParsedData *instance)
{
        SkSmartAttributeParsedData *ret;

        ret = g_new0 (SkSmartAttributeParsedData, 1);
        memcpy (ret, instance, sizeof (SkSmartAttributeParsedData));
        ret->name = g_strdup (instance->name);

        return ret;
}

static void
_gdu_sk_attr_free (SkSmartAttributeParsedData *instance)
{
        g_free ((gchar *) instance->name);
        g_free (instance);
}

static GType
_gdu_type_sk_attr_get_type (void)
{
        static volatile gsize type_volatile = 0;

        if (g_once_init_enter (&type_volatile)) {
                GType type = g_boxed_type_register_static (
                                                           g_intern_static_string ("_GduSkAttr"),
                                                           (GBoxedCopyFunc) _gdu_sk_attr_copy,
                                                           (GBoxedFreeFunc) _gdu_sk_attr_free);
                g_once_init_leave (&type_volatile, type);
        }

        return type_volatile;
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
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Read Error Rate"),
                N_("Frequency of errors while reading raw data from the disk. "
                   "A non-zero value indicates a problem with "
                   "either the disk surface or read/write heads")
        },
        {
                "throughput-performance",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Throughput Performance"),
                N_("Average efficiency of the disk")
        },
        {
                "spin-up-time",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Spinup Time"),
                N_("Time needed to spin up the disk")
        },
        {
                "start-stop-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Start/Stop Count"),
                N_("Number of spindle start/stop cycles")
        },
        {
                "reallocated-sector-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Reallocated Sector Count"),
                N_("Count of remapped sectors. "
                   "When the hard drive finds a read/write/verification error, it mark the sector "
                   "as \"reallocated\" and transfers data to a special reserved area (spare area)")
        },
        {
                "read-channel-margin",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Read Channel Margin"),
                N_("Margin of a channel while reading data.")
        },
        {
                "seek-error-rate",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Seek Error Rate"),
                N_("Frequency of errors while positioning")
        },
        {
                "seek-time-performance",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Seek Timer Performance"),
                N_("Average efficiency of operatings while positioning")
        },
        {
                "power-on-hours",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Power-On Hours"),
                N_("Number of hours elapsed in the power-on state")
        },
        {
                "spin-retry-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Spinup Retry Count"),
                N_("Number of retry attempts to spin up")
        },
        {
                "calibration-retry-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Calibration Retry Count"),
                N_("Number of attempts to calibrate the device")
        },
        {
                "power-cycle-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Power Cycle Count"),
                N_("Number of power-on events")
        },
        {
                "read-soft-error-rate",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Soft read error rate"),
                N_("Frequency of 'program' errors while reading from the disk")
        },
        {
                "reported-uncorrect",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Reported Uncorrectable Errors"),
                N_("Number of errors that could not be recovered using hardware ECC")
        },
        {
                "high-fly-writes",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("High Fly Writes"),
                N_("Number of times a recording head is flying outside its normal operating range")
        },
        {
                "airflow-temperature-celsius",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Airflow Temperature"),
                N_("Airflow temperature of the drive")
        },
        {
                "g-sense-error-rate",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("G-sense Error Rate"),
                N_("Frequency of mistakes as a result of impact loads")
        },
        {
                "power-off-retract-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Power-off Retract Count"),
                N_("Number of power-off or emergency retract cycles")
        },
        {
                "load-cycle-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Load/Unload Cycle Count"),
                N_("Number of cycles into landing zone position")
        },
        {
                "temperature-celsius-2",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Temperature"),
                N_("Current internal temperature of the drive")
        },
        {
                "hardware-ecc-recovered",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Hardware ECC Recovered"),
                N_("Number of ECC on-the-fly errors")
        },
        {
                "reallocated-event-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
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
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
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
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Uncorrectable Sector Count"),
                N_("The total number of uncorrectable errors when reading/writing a sector. "
                   "A rise in the value of this attribute indicates defects of the "
                   "disk surface and/or problems in the mechanical subsystem")
        },
        {
                "udma-crc-error-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("UDMA CRC Error Rate"),
                N_("Number of CRC errors during UDMA mode")
        },
        {
                "multi-zone-error-rate",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Write Error Rate"),
                N_("Number of errors while writing to disk (or) multi-zone error rate (or) flying-height")
        },
        {
                "soft-read-error-rate",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Soft Read Error Rate"),
                N_("Number of off-track errors")
        },
        {
                "ta-increase-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Data Address Mark Errors"),
                N_("Number of Data Address Mark (DAM) errors (or) vendor-specific")
        },
        {
                "run-out-cancel",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Run Out Cancel"),
                N_("Number of ECC errors")
        },
        {
                "shock-count-write-open",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Soft ECC correction"),
                N_("Number of errors corrected by software ECC")
        },
        {
                "shock-rate-write-open",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Thermal Asperity Rate"),
                N_("Number of Thermal Asperity Rate errors")
        },
        {
                "flying-height",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Flying Height"),
                N_("Height of heads above the disk surface")
        },
        {
                "spin-high-current",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Spin High Current"),
                N_("Amount of high current used to spin up the drive")
        },
        {
                "spin-buzz",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Spin Buzz"),
                N_("Number of buzz routines to spin up the drive")
        },
        {
                "offline-seek-performance",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Offline Seek Performance"),
                N_("Drive's seek performance during offline operations")
        },
        {
                "disk-shift",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Disk Shift"),
                N_("Shift of disk is possible as a result of strong shock loading in the store, "
                   "as a result of falling (or) temperature")
        },
        {
                "g-sense-error-rate-2",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("G-sense Error Rate"),
                N_("Number of errors as a result of impact loads as detected by a shock sensor")
        },
        {
                "loaded-hours",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Loaded Hours"),
                N_("Number of hours in general operational state")
        },
        {
                "load-retry-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Load/Unload Retry Count"),
                N_("Loading on drive caused by numerous recurrences of operations, like reading, "
                   "recording, positioning of heads, etc")
        },
        {
                "load-friction",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Load Friction"),
                N_("Load on drive cause by friction in mechanical parts of the store")
        },
        {
                "load-cycle-count-2",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Load/Unload Cycle Count"),
                N_("Total number of load cycles")
        },
        {
                "load-in-time",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Load-in Time"),
                N_("General time for loading in a drive")
        },
        {
                "torq-amp-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Torque Amplification Count"),
                N_("Quantity efforts of the rotating moment of a drive")
        },
        {
                "power-off-retract-count-2",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Power-off Retract Count"),
                N_("Number of power-off retract events")
        },
        {
                "head-amplitude",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("GMR Head Amplitude"),
                N_("Amplitude of heads trembling (GMR-head) in running mode")
        },
        {
                "temperature-celsius",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Temperature"),
                N_("Temperature of the drive")
        },
        {
                "endurance-remaining",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Endurance Remaining"),
                N_("Number of physical erase cycles completed on the drive as "
                   "a percentage of the maximum physical erase cycles the drive supports")
        },
        {
                "power-on-seconds-2",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Power-On Hours"),
                N_("Number of hours elapsed in the power-on state")
        },
        {
                "uncorrectable-ecc-count",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Uncorrectable ECC Count"),
                N_("Number of uncorrectable ECC errors")
        },
        {
                "good-block-rate",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Good Block Rate"),
                N_("Number of available reserved blocks as a percentage "
                   "of the total number of reserved blocks"),
        },
        {
                "head-flying-hours",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
                 * or the next string for a longer explanation.
                 */
                N_("Head Flying Hours"),
                N_("Time while head is positioning")
        },
        {
                "read-error-retry-rate",
                /* Translators: S.M.A.R.T attribute, see http://smartmontools.sourceforge.net/doc.html
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


static void
attribute_get_details (SkSmartAttributeParsedData  *a,
                       gchar                      **out_name,
                       gchar                      **out_description)
{
        const gchar *n;
        const gchar *d;
        SmartDetails *details;
        static volatile gsize have_hash = 0;
        static GHashTable *smart_details_map = NULL;

        if (g_once_init_enter (&have_hash)) {
                guint n;

                smart_details_map = g_hash_table_new (g_str_hash, g_str_equal);
                for (n = 0; smart_details[n].name != NULL; n++) {
                        g_hash_table_insert (smart_details_map,
                                             (gpointer) smart_details[n].name,
                                             (gpointer) &(smart_details[n]));
                }
                g_once_init_leave (&have_hash, 1);
        }

        n = NULL;
        d = NULL;
        details = g_hash_table_lookup (smart_details_map, a->name);
        if (details != NULL) {
                n = _(details->pretty_name);
                d = _(details->desc);
        }

        if (out_name != NULL)
                *out_name = g_strdup (n);
        if (out_description != NULL)
                *out_description = g_strdup (d);
}

/* ---------------------------------------------------------------------------------------------------- */

enum {
        ID_COLUMN,
        NAME_COLUMN,
        VALUE_COLUMN,
        TOOLTIP_COLUMN,
        SK_ATTR_COLUMN,
        N_COLUMNS,
};

G_DEFINE_TYPE (GduAtaSmartDialog, gdu_ata_smart_dialog, GTK_TYPE_DIALOG)

static void update_dialog (GduAtaSmartDialog *dialog);
static void device_changed (GduDevice *device, gpointer user_data);
static void device_job_changed (GduDevice *device, gpointer user_data);

static gchar *pretty_to_string (guint64                  pretty_value,
                                SkSmartAttributeUnit     pretty_unit);

static gboolean get_ata_smart_no_warn (GduDevice *device);
static gboolean set_ata_smart_no_warn (GduDevice *device,
                                       gboolean   no_warn);

static void
gdu_ata_smart_dialog_finalize (GObject *object)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);

        if (dialog->priv->drive != NULL) {
                g_object_unref (dialog->priv->drive);
        }
        if (dialog->priv->device != NULL) {
                g_signal_handler_disconnect (dialog->priv->device, dialog->priv->device_changed_signal_handler_id);
                g_signal_handler_disconnect (dialog->priv->device, dialog->priv->device_job_changed_signal_handler_id);
                g_object_unref (dialog->priv->device);
        }
        g_object_unref (dialog->priv->attr_list_store);

        if (G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->finalize (object);
}

static void
gdu_ata_smart_dialog_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);

        switch (property_id) {
        case PROP_DRIVE:
                g_value_set_object (value, dialog->priv->drive);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_ata_smart_dialog_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);

        switch (property_id) {
        case PROP_DRIVE:
                if (dialog->priv->drive != NULL) {
                        g_object_unref (dialog->priv->drive);
                }
                if (dialog->priv->device != NULL) {
                        g_signal_handler_disconnect (dialog->priv->device,
                                                     dialog->priv->device_changed_signal_handler_id);
                        g_signal_handler_disconnect (dialog->priv->device,
                                                     dialog->priv->device_job_changed_signal_handler_id);
                        g_object_unref (dialog->priv->device);
                }
                if (g_value_get_object (value) != NULL) {
                        dialog->priv->drive = g_value_dup_object (value);
                        dialog->priv->device = gdu_presentable_get_device (GDU_PRESENTABLE (dialog->priv->drive));
                        if (dialog->priv->device != NULL) {
                                dialog->priv->device_changed_signal_handler_id = g_signal_connect (dialog->priv->device,
                                                                                                   "changed",
                                                                                                   G_CALLBACK (device_changed),
                                                                                                   dialog);
                                dialog->priv->device_job_changed_signal_handler_id = g_signal_connect (dialog->priv->device,
                                                                                                       "job-changed",
                                                                                                       G_CALLBACK (device_job_changed),
                                                                                                       dialog);
                        }
                } else {
                        dialog->priv->drive = NULL;
                        dialog->priv->device = NULL;
                }
                update_dialog (dialog);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
selection_changed (GtkTreeSelection *tree_selection,
                   gpointer          user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);
        GtkTreeIter iter;
        gchar *attr_name;

        attr_name = NULL;

        if (!gtk_tree_selection_get_selected (tree_selection,
                                              NULL,
                                              &iter))
                goto out;

        gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->attr_list_store),
                            &iter,
                            NAME_COLUMN,
                            &attr_name,
                            -1);

        //g_debug ("selected %s", attr_name);

 out:
        g_free (attr_name);
}

static gchar *
get_grey_color (GtkTreeView *tree_view,
                GtkTreeIter *iter)
{
        GtkTreeSelection *tree_selection;
        GtkStyle *style;
        GdkColor desc_gdk_color = {0};
        GtkStateType state;
        gchar *desc_color;

        /* This color business shouldn't be this hard... */
        tree_selection = gtk_tree_view_get_selection (tree_view);
        style = gtk_widget_get_style (GTK_WIDGET (tree_view));
        if (gtk_tree_selection_iter_is_selected (tree_selection, iter)) {
                if (GTK_WIDGET_HAS_FOCUS (GTK_WIDGET (tree_view)))
                        state = GTK_STATE_SELECTED;
                else
                        state = GTK_STATE_ACTIVE;
        } else {
                state = GTK_STATE_NORMAL;
        }
#define BLEND_FACTOR 0.7
        desc_gdk_color.red   = style->text[state].red   * BLEND_FACTOR +
                               style->base[state].red   * (1.0 - BLEND_FACTOR);
        desc_gdk_color.green = style->text[state].green * BLEND_FACTOR +
                               style->base[state].green * (1.0 - BLEND_FACTOR);
        desc_gdk_color.blue  = style->text[state].blue  * BLEND_FACTOR +
                               style->base[state].blue  * (1.0 - BLEND_FACTOR);
#undef BLEND_FACTOR
        desc_color = g_strdup_printf ("#%02x%02x%02x",
                                      (desc_gdk_color.red >> 8),
                                      (desc_gdk_color.green >> 8),
                                      (desc_gdk_color.blue >> 8));

        return desc_color;
}

static void
format_markup_name (GtkCellLayout   *cell_layout,
                    GtkCellRenderer *renderer,
                    GtkTreeModel    *tree_model,
                    GtkTreeIter     *iter,
                    gpointer         user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);
        SkSmartAttributeParsedData *a;
        gint id;
        gchar *name;
        gchar *desc;
        gchar *markup;
        gchar *desc_color;

        gtk_tree_model_get (tree_model,
                            iter,
                            ID_COLUMN, &id,
                            SK_ATTR_COLUMN, &a,
                            -1);

        attribute_get_details (a,
                               &name,
                               &desc);

        if (name == NULL)
                name = g_strdup (a->name);

        if (desc == NULL) {
                /* Translators: This is shown in the attribute treeview when no description is found.
                 * %d is the attribute number.
                 */
                desc = g_strdup_printf (_("No description for attribute %d"), id);
        }

        desc_color = get_grey_color (GTK_TREE_VIEW (dialog->priv->tree_view), iter);
        if (a->warn) {
                markup = g_strdup_printf ("<b><span fgcolor='red'>%s</span></b>\n"
                                          "<span fgcolor='darkred'><small>%s</small></span>",
                                          name,
                                          desc);
        } else {
                markup = g_strdup_printf ("<b>%s</b>\n"
                                          "<span fgcolor=\"%s\"><small>%s</small></span>",
                                          name,
                                          desc_color,
                                          desc);
        }

        g_object_set (renderer,
                      "markup", markup,
                      NULL);

        _gdu_sk_attr_free (a);
        g_free (name);
        g_free (desc);
        g_free (markup);
        g_free (desc_color);
}

static void
format_markup_id (GtkCellLayout   *cell_layout,
                  GtkCellRenderer *renderer,
                  GtkTreeModel    *tree_model,
                  GtkTreeIter     *iter,
                  gpointer         user_data)
{
        SkSmartAttributeParsedData *a;
        gchar *markup;
        gchar *id;

        gtk_tree_model_get (tree_model,
                            iter,
                            SK_ATTR_COLUMN, &a,
                            -1);

        id = g_strdup_printf ("%d", a->id);

        if (a->warn)
                markup = g_strdup_printf ("<span foreground='red'>%s</span>", id);
        else
                markup = g_strdup_printf ("%s", id);

        g_object_set (renderer,
                      "markup", markup,
                      NULL);

        _gdu_sk_attr_free (a);
        g_free (id);
        g_free (markup);
}

static void
format_markup_value_headings (GtkCellLayout   *cell_layout,
                              GtkCellRenderer *renderer,
                              GtkTreeModel    *tree_model,
                              GtkTreeIter     *iter,
                              gpointer         user_data)
{
        SkSmartAttributeParsedData *a;
        gchar *markup;
        const gchar *s1, *s2, *s3, *s4;

        gtk_tree_model_get (tree_model,
                            iter,
                            SK_ATTR_COLUMN, &a,
                            -1);

        /* Translators: This is shown in the tree view for the normalized value of an attribute (0-254) */
        s1 = _("Normalized:");
        /* Translators: This is shown in the tree view for the worst value of an attribute (0-254) */
        s2 = _("Worst:");
        /* Translators: This is shown in the tree view for the threshold of an attribute (0-254) */
        s3 = _("Threshold:");
        /* Translators: This is shown in the tree view for the interpreted/pretty value of an attribute */
        s4 = _("Value:");

        if (a->warn)
                markup = g_strdup_printf ("<span foreground='red'>"
                                          "<small>"
                                          "%s\n"
                                          "%s\n"
                                          "%s\n"
                                          "%s"
                                          "</small>"
                                          "</span>",
                                          s1, s2, s3, s4);
        else
                markup = g_strdup_printf ("<small>"
                                          "%s\n"
                                          "%s\n"
                                          "%s\n"
                                          "%s"
                                          "</small>",
                                          s1, s2, s3, s4);

        g_object_set (renderer,
                      "markup", markup,
                      NULL);

        _gdu_sk_attr_free (a);
        g_free (markup);
}

static void
format_markup_value (GtkCellLayout   *cell_layout,
                     GtkCellRenderer *renderer,
                     GtkTreeModel    *tree_model,
                     GtkTreeIter     *iter,
                     gpointer         user_data)
{
        SkSmartAttributeParsedData *a;
        gchar *markup;
        gchar *s1, *s2, *s3, *s4;

        gtk_tree_model_get (tree_model,
                            iter,
                            SK_ATTR_COLUMN, &a,
                            -1);

        if (a->worst_value_valid) {
                s1 = g_strdup_printf ("%d", a->current_value);
        } else {
                /* Translators: This is used in the attribute treview when normalized/worst/treshold
                 * value isn't available */
                s1 = g_strdup (_("N/A"));
        }

        if (a->worst_value_valid) {
                s2 = g_strdup_printf ("%d", a->worst_value);
        } else {
                /* Translators: This is used in the attribute treview when normalized/worst/treshold
                 * value isn't available */
                s2 = g_strdup (_("N/A"));
        }

        if (a->threshold_valid) {
                s3 = g_strdup_printf ("%d", a->threshold);
        } else {
                /* Translators: This is used in the attribute treview when normalized/worst/treshold
                 * value isn't available */
                s3 = g_strdup (_("N/A"));
        }

        s4 = pretty_to_string (a->pretty_value, a->pretty_unit);

        if (a->warn)
                markup = g_strdup_printf ("<span foreground='red'>"
                                          "<small>"
                                          "%s\n"
                                          "%s\n"
                                          "%s\n"
                                          "%s"
                                          "</small>"
                                          "</span>",
                                          s1, s2, s3, s4);
        else
                markup = g_strdup_printf ("<small>"
                                          "%s\n"
                                          "%s\n"
                                          "%s\n"
                                          "%s"
                                          "</small>",
                                          s1, s2, s3, s4);

        g_object_set (renderer,
                      "markup", markup,
                      NULL);

        _gdu_sk_attr_free (a);
        g_free (s1);
        g_free (s2);
        g_free (s3);
        g_free (s4);
        g_free (markup);
}

static void
format_markup_assessment (GtkCellLayout   *cell_layout,
                          GtkCellRenderer *renderer,
                          GtkTreeModel    *tree_model,
                          GtkTreeIter     *iter,
                          gpointer         user_data)
{
        SkSmartAttributeParsedData *a;
        gchar *markup;
        const gchar *assessment;
        gboolean failed;
        gboolean failed_in_the_past;

        gtk_tree_model_get (tree_model,
                            iter,
                            SK_ATTR_COLUMN, &a,
                            -1);

        failed = FALSE;
        failed_in_the_past = FALSE;
        if (a->prefailure) {
                if (!a->good_now && a->good_now_valid) {
                        failed = TRUE;
                }

                if (!a->good_in_the_past && a->good_in_the_past_valid) {
                        failed_in_the_past = TRUE;
                }
        } else {
                if (a->current_value_valid && a->threshold_valid &&
                    a->current_value <= a->threshold) {
                        failed = TRUE;
                }

                if (a->worst_value_valid && a->threshold_valid &&
                    a->worst_value <= a->threshold) {
                        failed_in_the_past = TRUE;
                }
        }

        if (failed) {
                /* Translators: Shown in the treeview for a failing attribute */
                assessment = _("Failing");
        } else if (failed_in_the_past) {
                /* Translators: Shown in the treeview for an attribute that failed in the past */
                assessment = _("Failed in the past");
        } else if (a->good_now_valid) {
                if (a->warn) {
                        /* Translators: Shown in the treeview for an attribute that we want to warn about */
                        assessment = _("Warning");
                } else {
                        /* Translators: Shown in the treeview for an attribute that is good */
                        assessment = _("Good");
                }
        } else {
                if (a->warn) {
                        /* Translators: Shown in the treeview for an attribute that we want to warn about */
                        assessment = _("Warning");
                } else {
                        /* Translators: Shown in the treeview for an attribute we don't know the status about */
                        assessment = _("N/A");
                }
        }

        if (a->warn)
                markup = g_strdup_printf ("<span foreground='red'>%s</span>", assessment);
        else
                markup = g_strdup_printf ("%s", assessment);

        g_object_set (renderer,
                      "markup", markup,
                      NULL);

        _gdu_sk_attr_free (a);
        g_free (markup);
}

static void
pixbuf_assessment (GtkCellLayout   *cell_layout,
                   GtkCellRenderer *renderer,
                   GtkTreeModel    *tree_model,
                   GtkTreeIter     *iter,
                   gpointer         user_data)
{
        SkSmartAttributeParsedData *a;
        const gchar *icon_name;

        gtk_tree_model_get (tree_model,
                            iter,
                            SK_ATTR_COLUMN, &a,
                            -1);

        if (a->warn) {
                icon_name = "gdu-smart-failing";
        } else {
                if (a->good_now_valid)
                        icon_name = "gdu-smart-healthy";
                else
                        icon_name = "gdu-smart-unknown";
        }

        g_object_set (renderer,
                      "icon-name", icon_name,
                      "stock-size", GTK_ICON_SIZE_MENU,
                      NULL);

        _gdu_sk_attr_free (a);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_cb (GduDevice  *device,
            GError     *error,
            gpointer    user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);

        /* TODO: maybe show error dialog */
        if (error != NULL)
                g_error_free (error);

        dialog->priv->is_updating = FALSE;
        update_dialog (dialog);
}


static void
on_activate_link_update_smart_data (GtkLabel    *label,
                                    const gchar *uri,
                                    gpointer     user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);

        gdu_device_drive_ata_smart_refresh_data (dialog->priv->device,
                                                 refresh_cb,
                                                 dialog);

        g_signal_stop_emission_by_name (label, "activate-link");

        dialog->priv->is_updating = TRUE;
        update_dialog (dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
cancel_self_test_cb (GduDevice  *device,
                     GError     *error,
                     gpointer    user_data)
{
        /* TODO: maybe show error dialog */
        if (error != NULL)
                g_error_free (error);
}


static void
on_activate_link_cancel_self_test (GtkLabel    *link_label,
                                   const gchar *uri,
                                   gpointer     user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);

        g_signal_stop_emission_by_name (link_label, "activate-link");

        gdu_device_op_cancel_job (dialog->priv->device,
                                  cancel_self_test_cb,
                                  dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
run_self_test_cb (GduDevice  *device,
                  GError     *error,
                  gpointer    user_data)
{
        /* TODO: maybe show error dialog */
        if (error != NULL)
                g_error_free (error);
}


static void
on_activate_link_run_self_test (GtkLabel    *link_label,
                                const gchar *uri,
                                gpointer     user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);
        GtkWidget *test_dialog;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *main_vbox;
        GtkWidget *label;
        GtkWidget *radio0;
        GtkWidget *radio1;
        GtkWidget *radio2;
        gchar *s;
        gint response;
        const gchar *test;

        g_signal_stop_emission_by_name (link_label, "activate-link");

        test_dialog = gtk_dialog_new_with_buttons (NULL,
                                                   GTK_WINDOW (dialog),
                                                   GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
                                                   NULL);
        gtk_window_set_title (GTK_WINDOW (test_dialog), "");

	gtk_container_set_border_width (GTK_CONTAINER (test_dialog), 6);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (test_dialog)->vbox), 2);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (test_dialog)->action_area), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (test_dialog)->action_area), 6);
	gtk_window_set_resizable (GTK_WINDOW (test_dialog), FALSE);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (test_dialog)->vbox), hbox, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	main_vbox = gtk_vbox_new (FALSE, 10);
	gtk_box_pack_start (GTK_BOX (hbox), main_vbox, TRUE, TRUE, 0);

	label = gtk_label_new (NULL);
        s = g_strconcat ("<big><b>",
                         /* Translators: Shown in the "Run self-test" dialog */
                         _("Select what SMART self test to run"),
                         "</b></big>",
                         NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

	label = gtk_label_new (NULL);
        /* Translators: Shown in the "Run self-test" dialog */
        gtk_label_set_markup (GTK_LABEL (label), _("The tests may take a very long time to complete depending "
                                                   "on the speed and size of the disk. You can continue using "
                                                   "your system while the test is running."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

        radio0 = gtk_radio_button_new_with_mnemonic_from_widget (NULL,
                                                                 /* Translators: Radio button for short test */
                                                                 _("_Short (usually less than ten minutes)"));
        radio1 = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (radio0),
                                                                 /* Translators: Radio button for extended test */
                                                                 _("_Extended (usually tens of minutes)"));
        radio2 = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (radio0),
                                                                 /* Translators: Radio button for conveyance test */
                                                                 _("C_onveyance (usually less than ten minutes)"));

	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (radio0), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (radio1), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (radio2), FALSE, FALSE, 0);

        gtk_dialog_add_button (GTK_DIALOG (test_dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        /* Translators: Button in "Run self-test dialog" */
        gtk_dialog_add_button (GTK_DIALOG (test_dialog), _("_Initiate Self Test"), 0);
        gtk_dialog_set_default_response (GTK_DIALOG (test_dialog), 0);

        gtk_widget_show_all (test_dialog);
        response = gtk_dialog_run (GTK_DIALOG (test_dialog));

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio0))) {
                test = "short";
        } else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio1))) {
                test = "extended";
        } else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio2))) {
                test = "conveyance";
        }

        gtk_widget_destroy (test_dialog);
        if (response != 0)
                goto out;

        gdu_device_op_drive_ata_smart_initiate_selftest (dialog->priv->device,
                                                         test,
                                                         run_self_test_cb,
                                                         dialog);

 out:
        ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
disk_name_data_func (GtkCellLayout   *cell_layout,
                     GtkCellRenderer *renderer,
                     GtkTreeModel    *tree_model,
                     GtkTreeIter     *iter,
                     gpointer         user_data)
{
        gchar *name;
        gchar *vpd_name;
        gchar *markup;
        gchar *desc;
        GduPresentable *p;
        GduDevice *d;
        gboolean sensitive;
        gchar *s;

        gtk_tree_model_get (tree_model,
                            iter,
                            GDU_POOL_TREE_MODEL_COLUMN_NAME, &name,
                            GDU_POOL_TREE_MODEL_COLUMN_VPD_NAME, &vpd_name,
                            GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &p,
                            -1);

        d = gdu_presentable_get_device (p);

        desc = NULL;
        sensitive = FALSE;
        if (d != NULL) {
                if (gdu_device_drive_ata_smart_get_is_available (d) &&
                    gdu_device_drive_ata_smart_get_time_collected (d) > 0) {
                        const gchar *status;
                        gboolean highlight;

                        sensitive = TRUE;

                        status = gdu_device_drive_ata_smart_get_status (d);
                        if (status != NULL && strlen (status) > 0) {
                                desc = gdu_util_ata_smart_status_to_desc (status, &highlight, NULL, NULL);
                                if (highlight) {
                                        s = g_strdup_printf ("<span fgcolor=\"red\"><b>%s</b></span>", desc);
                                        g_free (desc);
                                        desc = s;
                                }
                        } else if (gdu_device_drive_ata_smart_get_is_available (d) &&
                                   gdu_device_drive_ata_smart_get_time_collected (d) > 0) {
                                /* Translators: Used in the drive combo-box to indicate the health status is unknown */
                                desc = g_strdup (_("Health status is unknown"));
                        }
                } else {
                        if (gdu_device_drive_ata_smart_get_is_available (d)) {
                                /* Translators: Used in the drive combo-box to indicate SMART is not enabled */
                                desc = g_strdup (_("SMART is not enabled"));
                        } else {
                                /* Translators: Used in the drive combo-box to indicate SMART is not available */
                                desc = g_strdup (_("SMART is not available"));
                        }
                }
        }

        if (desc != NULL) {
                markup = g_strdup_printf ("<b>%s</b> – %s\n"
                                          "<small>%s</small>",
                                          name,
                                          vpd_name,
                                          desc);
        } else {
                markup = g_strdup_printf ("<b>%s</b> – %s\n"
                                          "<small> </small>",
                                          name,
                                          vpd_name);
        }

        g_object_set (renderer,
                      "markup", markup,
                      "sensitive", sensitive,
                      NULL);

        g_free (name);
        g_free (vpd_name);
        g_free (markup);
        g_free (desc);
        g_object_unref (p);
        if (d != NULL)
                g_object_unref (d);
}

static void
disk_name_gicon_func (GtkCellLayout   *cell_layout,
                      GtkCellRenderer *renderer,
                      GtkTreeModel    *tree_model,
                      GtkTreeIter     *iter,
                      gpointer         user_data)
{
        GIcon *icon;
        GduPresentable *p;
        GduDevice *d;
        gboolean sensitive;

        gtk_tree_model_get (tree_model,
                            iter,
                            GDU_POOL_TREE_MODEL_COLUMN_ICON, &icon,
                            GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &p,
                            -1);

        d = gdu_presentable_get_device (p);
        sensitive = FALSE;
        if (d != NULL) {
                if (gdu_device_drive_ata_smart_get_is_available (d) &&
                    gdu_device_drive_ata_smart_get_time_collected (d) > 0)
                        sensitive = TRUE;
        }

        g_object_set (renderer,
                      "gicon", icon,
                      "sensitive", sensitive,
                      NULL);

        g_object_unref (icon);
        g_object_unref (p);
        if (d != NULL)
                g_object_unref (d);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_drive_combo_box_changed (GtkComboBox *combo_box,
                            gpointer     user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);
        GtkTreeIter iter = {0};

        if (gtk_combo_box_get_active_iter (combo_box, &iter)) {
                GduPresentable *p;

                gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->pool_tree_model),
                                    &iter,
                                    GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE, &p,
                                    -1);

                g_object_set (dialog,
                              "drive", GDU_DRIVE (p),
                              NULL);

                g_object_unref (p);
        } else {
                g_object_set (dialog,
                              "drive", NULL,
                              NULL);
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_no_warn_check_button_toggled (GtkToggleButton *toggle_button,
                                 gpointer         user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);
        gboolean is_active;

        is_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->priv->no_warn_check_button));

        if (dialog->priv->device == NULL)
                goto out;

        if (get_ata_smart_no_warn (dialog->priv->device) != is_active) {
                set_ata_smart_no_warn (dialog->priv->device, is_active);
                update_dialog (dialog);
        }

 out:
        ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_ata_smart_dialog_constructed (GObject *object)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *align;
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *table;
        GtkWidget *label;
        GtkWidget *tree_view;
        GtkWidget *scrolled_window;
        GtkWidget *spinner;
        GtkWidget *progress_bar;
        GtkWidget *check_button;
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;
        gint row;
        GtkTreeSelection *selection;
        gchar *s;
        GtkWidget *combo_box;
        GduPool *pool;
        GtkTreeIter iter = {0};
        const gchar *tooltip_markup;
        gboolean rtl;

        rtl = (gtk_widget_get_direction (GTK_WIDGET (dialog)) == GTK_TEXT_DIR_RTL);

        /* Translators: Title of the SMART dialog */
        gtk_window_set_title (GTK_WINDOW (dialog), _("SMART Data"));
        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 12, 12, 12, 12);
        gtk_box_pack_start (GTK_BOX (content_area), align, TRUE, TRUE, 0);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        /* ---------------------------------------------------------------------------------------------------- */

        pool = gdu_device_get_pool (dialog->priv->device);
        dialog->priv->pool_tree_model = gdu_pool_tree_model_new (pool,
                                                                 GDU_POOL_TREE_MODEL_FLAGS_NO_VOLUMES);
        g_object_unref (pool);

        table = gtk_table_new (4, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_table_set_row_spacings (GTK_TABLE (table), 0);
        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        row = 0;

        /* ------------------------------ */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Label used before the drive combo box */
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Drive:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        combo_box = gtk_combo_box_new_with_model (GTK_TREE_MODEL (dialog->priv->pool_tree_model));
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
        dialog->priv->drive_combo_box = combo_box;
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        g_signal_connect (combo_box,
                          "changed",
                          G_CALLBACK (on_drive_combo_box_changed),
                          dialog);

        renderer = gtk_cell_renderer_pixbuf_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, FALSE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo_box),
                                            renderer,
                                            disk_name_gicon_func,
                                            dialog,
                                            NULL);
        g_object_set (renderer,
                      "stock-size", GTK_ICON_SIZE_SMALL_TOOLBAR,
                      NULL);

        renderer = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo_box),
                                            renderer,
                                            disk_name_data_func,
                                            dialog,
                                            NULL);

        row++;

        if (dialog->priv->drive != NULL) {
                if (gdu_pool_tree_model_get_iter_for_presentable (dialog->priv->pool_tree_model,
                                                                  GDU_PRESENTABLE (dialog->priv->drive),
                                                                  &iter)) {
                        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (dialog->priv->drive_combo_box), &iter);
                }
        }

        /* ---------------------------------------------------------------------------------------------------- */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Heading used in the main dialog for the SMART status */
        s = g_strconcat ("<b>", _("Status"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (align), vbox2);

        table = gtk_table_new (4, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_table_set_row_spacings (GTK_TABLE (table), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* ------------------------------ */
        /* updated */

        /* Translators: Tooltip for the Updated item in the status table */
        tooltip_markup = _("Time since SMART data was last read – SMART data is updated every 30 minutes unless "
                           "the disk is sleeping");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Updated:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        hbox = gtk_hbox_new (FALSE, 0);

        /* Translators: Used in the status table when data is currently being updated */
        label = gtk_label_new (_("Updating..."));
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
        dialog->priv->updating_label = label;

        spinner = gdu_spinner_new ();
        gtk_box_pack_start (GTK_BOX (hbox), spinner, FALSE, FALSE, 6);
        dialog->priv->updating_spinner = spinner;

        label = gdu_time_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->updated_label = label;
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);


        label = gtk_label_new (NULL);
        s = g_strdup_printf (rtl ? "<a href=\"update-now\" title=\"%s\">%s</a> – " :
                                   " – <a href=\"update-now\" title=\"%s\">%s</a>",
                             /* Translators: Tooltip for the "Update Now" hyperlink */
                             _("Reads SMART data from the disk, waking it up if necessary"),
                            /* Translators: Text used in the hyperlink in the status table to update the SMART status */
                             _("Update now"));
        gtk_label_set_track_visited_links (GTK_LABEL (label), FALSE);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->update_link_label = label;
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
        g_signal_connect (label,
                          "activate-link",
                          G_CALLBACK (on_activate_link_update_smart_data),
                          dialog);

        gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);
        row++;

        /* control visibility (see update_dialog()) */
        gtk_widget_set_no_show_all (dialog->priv->updated_label, TRUE);
        gtk_widget_set_no_show_all (dialog->priv->updating_label, TRUE);
        gtk_widget_set_no_show_all (dialog->priv->updating_spinner, TRUE);
        gtk_widget_set_no_show_all (dialog->priv->update_link_label, TRUE);

        /* ------------------------------ */
        /* self-tests */

        /* Translators: Tooltip for the Self-tests item in the status table */
        tooltip_markup = _("The result of the last self-test that ran on the disk");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Self-tests:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        hbox = gtk_hbox_new (FALSE, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
        dialog->priv->self_test_result_label = label;


        progress_bar = gtk_progress_bar_new ();
        gtk_box_pack_start (GTK_BOX (hbox), progress_bar, FALSE, FALSE, 0);
        dialog->priv->self_test_progress_bar = progress_bar;

        label = gtk_label_new (NULL);
        s = g_strdup_printf (rtl ? "<a href=\"run-self-test\" title=\"%s\">%s</a> – " :
                                   " – <a href=\"run-self-test\" title=\"%s\">%s</a>",
                             /* Translators: Tooltip for the "Run self-test" hyperlink */
                             _("Initiates a self-test on the drive"),
                             /* Translators: Text used in the hyperlink in the status table to run a self-test */
                             _("Run self-test"));
        gtk_label_set_track_visited_links (GTK_LABEL (label), FALSE);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
        dialog->priv->self_test_run_link_label = label;
        g_signal_connect (label,
                          "activate-link",
                          G_CALLBACK (on_activate_link_run_self_test),
                          dialog);

        label = gtk_label_new (NULL);
        gtk_label_set_track_visited_links (GTK_LABEL (label), FALSE);
        s = g_strdup_printf (rtl ? "<a href=\"cancel-self-test\" title=\"%s\">%s</a> – " :
                                   " – <a href=\"cancel-self-test\" title=\"%s\">%s</a>",
                             /* Translators: Tooptip for the "Cancel" hyperlink */
                             _("Cancels the currently running test"),
                             /* Translators: Text used in the hyperlink in the status table to cancel a self-test */
                             _("Cancel"));
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
        dialog->priv->self_test_cancel_link_label = label;
        g_signal_connect (label,
                          "activate-link",
                          G_CALLBACK (on_activate_link_cancel_self_test),
                          dialog);

        gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* control visibility (see update_dialog()) */
        gtk_widget_set_no_show_all (dialog->priv->self_test_result_label, TRUE);
        gtk_widget_set_no_show_all (dialog->priv->self_test_progress_bar, TRUE);
        gtk_widget_set_no_show_all (dialog->priv->self_test_run_link_label, TRUE);
        gtk_widget_set_no_show_all (dialog->priv->self_test_cancel_link_label, TRUE);

        /* ------------------------------ */
        /* model */

        /* Translators: Tooltip for the "Model Name:" item in the status table */
        tooltip_markup = _("The name of the model of the disk");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Model Name:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->model_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* ------------------------------ */
        /* firmware */

        /* Translators: Tooltip for the "Firmware Version:" item in the status table */
        tooltip_markup = _("The firmware version of the disk");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Firmware Version:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->firmware_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* ------------------------------ */
        /* serial */

        /* Translators: Tooltip for the "Serial:" item in the status table */
        tooltip_markup = _("The serial number of the disk");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Serial Number:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->serial_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* ------------------------------ */
        /* power on hours */

        /* Translators: Tooltip for the "Powered On:" item in the status table */
        tooltip_markup = _("The amount of elapsed time the disk has been in a powered-up state");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Powered On:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->power_on_hours_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* ------------------------------ */
        /* temperature */

        /* Translators: Tooltip for the "Temperature:" item in the status table */
        tooltip_markup = _("The temperature of the disk");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Temperature:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->temperature_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* ------------------------------ */
        /* bad sectors */

        /* Translators: Tooltip for the "Bad Sectors" item in the status table */
        tooltip_markup = _("The sum of pending and reallocated bad sectors");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Bad Sectors:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->sectors_label = label;
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);
        row++;

        /* ------------------------------ */
        /* self assessment */

        /* Translators: Tooltip for the "Self Assessment" item in the status table */
        tooltip_markup = _("The assessment from the disk itself whether it is about to fail");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Self Assessment:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->self_assessment_label = label;
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);
        row++;

        /* ------------------------------ */
        /* overall assessment */

        /* Translators: Tooltip for the "Overall Assessment" in the status table */
        tooltip_markup = _("An overall assessment of the health of the disk");

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
        /* Translators: Item name in the status table */
        gtk_label_set_markup (GTK_LABEL (label), _("Overall Assessment:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        hbox = gtk_hbox_new (FALSE, 2);
        image = gtk_image_new_from_icon_name ("gdu-smart-unknown",
                                              GTK_ICON_SIZE_MENU);
        gtk_widget_set_tooltip_markup (image, tooltip_markup);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
        dialog->priv->overall_assessment_image = image;

        label = gtk_label_new (NULL);
        gtk_widget_set_tooltip_markup (label, tooltip_markup);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
        dialog->priv->overall_assessment_label = label;
        gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

        gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);
        row++;

        /* ------------------------------ */

        /* Translators: Tooltip for the "Do not warn if disk is failing" check button */
        tooltip_markup = _("Leave unchecked to get notified if the disk starts failing");

        /* Translators: Check button in the status table */
        check_button = gtk_check_button_new_with_mnemonic (_("Don't _warn me if the disk is failing"));
        gtk_widget_set_tooltip_markup (check_button, tooltip_markup);
        gtk_box_pack_start (GTK_BOX (vbox2), check_button, FALSE, FALSE, 0);
        dialog->priv->no_warn_check_button = check_button;
        g_signal_connect (check_button,
                          "toggled",
                          G_CALLBACK (on_no_warn_check_button_toggled),
                          dialog);

        /* ---------------------------------------------------------------------------------------------------- */
        /* attributes in a tree view */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        /* Translators: Heading used in the main dialog for SMART attributes*/
        s = g_strconcat ("<b>", _("_Attributes"), "</b>", NULL);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (vbox), align, TRUE, TRUE, 0);

        dialog->priv->attr_list_store = gtk_list_store_new (N_COLUMNS,
                                                            G_TYPE_INT,         /* id */
                                                            G_TYPE_STRING,      /* name */
                                                            G_TYPE_STRING,      /* value */
                                                            G_TYPE_STRING,      /* tooltip */
                                                            _GDU_TYPE_SK_ATTR); /* SkSmartAttributeParsedData pointer */

        tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (dialog->priv->attr_list_store));
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), tree_view);
        gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (tree_view), TRUE);
        gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (tree_view), TOOLTIP_COLUMN);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (dialog->priv->attr_list_store),
                                              ID_COLUMN,
                                              GTK_SORT_ASCENDING);
        dialog->priv->tree_view = tree_view;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
        g_signal_connect (selection,
                          "changed",
                          G_CALLBACK (selection_changed),
                          dialog);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);
        /* Translators: This string is used as the column title in the treeview for the Attribute ID (0-255) */
        gtk_tree_view_column_set_title (column, _("ID"));
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            format_markup_id,
                                            dialog,
                                            NULL);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);
        /* Translators: This string is used as the column title in the treeview for the attribute name and description */
        gtk_tree_view_column_set_title (column, _("Attribute"));
        gtk_tree_view_column_set_expand (column, TRUE);
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer,
                      "wrap-width", 300,
                      "wrap-mode", PANGO_WRAP_WORD_CHAR,
                      NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            format_markup_name,
                                            dialog,
                                            NULL);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);
        /* Translators: This string is used as the column title in the treeview for the assessment of the attribute */
        gtk_tree_view_column_set_title (column, _("Assessment"));
        renderer = gtk_cell_renderer_pixbuf_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            pixbuf_assessment,
                                            dialog,
                                            NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            format_markup_assessment,
                                            dialog,
                                            NULL);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);
        /* Translators: This string is used as the column title in the treeview for the value of the attribute */
        gtk_tree_view_column_set_title (column, _("Value"));
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            format_markup_value_headings,
                                            dialog,
                                            NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column),
                                            renderer,
                                            format_markup_value,
                                            dialog,
                                            NULL);

        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                             GTK_SHADOW_IN);
        gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

        gtk_container_add (GTK_CONTAINER (align), scrolled_window);

        /* ---------------------------------------------------------------------------------------------------- */

        gtk_window_set_default_size (GTK_WINDOW (dialog), 700, 600);

        dialog->priv->has_been_constructed = TRUE;
        update_dialog (dialog);

        /* Focus the first attribute view */
        gtk_widget_grab_focus (dialog->priv->tree_view);

        if (G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->constructed (object);
}

static void
gdu_ata_smart_dialog_class_init (GduAtaSmartDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduAtaSmartDialogPrivate));

        object_class->get_property = gdu_ata_smart_dialog_get_property;
        object_class->set_property = gdu_ata_smart_dialog_set_property;
        object_class->constructed  = gdu_ata_smart_dialog_constructed;
        object_class->finalize     = gdu_ata_smart_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_DRIVE,
                                         g_param_spec_object ("drive",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_DRIVE,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT));
}

static void
gdu_ata_smart_dialog_init (GduAtaSmartDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog, GDU_TYPE_ATA_SMART_DIALOG, GduAtaSmartDialogPrivate);
}

GtkWidget *
gdu_ata_smart_dialog_new (GtkWindow *parent,
                          GduDrive  *drive)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_ATA_SMART_DIALOG,
                                         "transient-for", parent,
                                         "drive", drive,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
pretty_to_string (uint64_t              pretty_value,
                  SkSmartAttributeUnit  pretty_unit)
{
        gchar *ret;
        gdouble celcius;
        gdouble fahrenheit;

        switch (pretty_unit) {

        case SK_SMART_ATTRIBUTE_UNIT_MSECONDS:
                if (pretty_value > 1000 * 60 * 60 * 24 * 365.25) {
                        /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                         * for a time-based unit that exceed one year */
                        ret = g_strdup_printf (_("%.1f years"), pretty_value / 1000.0 / 60.0 / 60.0 / 24.0 / 365.25);
                } else if (pretty_value > 1000 * 60 * 60 * 24) {
                        /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                         * for a time-based unit that exceed one day */
                        ret = g_strdup_printf (_("%.1f days"), pretty_value / 1000.0 / 60.0 / 60.0 / 24.0);
                } else if (pretty_value > 1000 * 60 * 60) {
                        /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                         * for a time-based unit that exceed one hour */
                        ret = g_strdup_printf (_("%.1f hours"), pretty_value / 1000.0 / 60.0 / 60.0);
                } else if (pretty_value > 1000 * 60) {
                        /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                         * for a time-based unit that exceed one minute */
                        ret = g_strdup_printf (_("%.1f minutes"), pretty_value / 1000.0 / 60.0);
                } else if (pretty_value > 1000) {
                        /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                         * for a time-based unit that exceed one second */
                        ret = g_strdup_printf (_("%.1f seconds"), pretty_value / 1000.0);
                } else {
                        /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                         * for a time-based unit that is counted in milliseconds */
                        ret = g_strdup_printf (_("%" G_GUINT64_FORMAT " msec"), pretty_value);
                }
                break;

        case SK_SMART_ATTRIBUTE_UNIT_SECTORS:
                /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                 * for a sector-based unit */
                ret = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                                  "%d sector",
                                                  "%d sectors",
                                                  (gint) pretty_value),
                                       (gint) pretty_value);
                break;

        case SK_SMART_ATTRIBUTE_UNIT_MKELVIN:
                celcius = pretty_value / 1000.0 - 273.15;
                fahrenheit = 9.0 * celcius / 5.0 + 32.0;
                /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                 * for a temperature-based unit - first %f is the temperature in degrees Celcius, second %f
                 * is the temperature in degrees Fahrenheit */
                ret = g_strdup_printf (_("%.0f° C / %.0f° F"), celcius, fahrenheit);
                break;

        case SK_SMART_ATTRIBUTE_UNIT_NONE:
                ret = g_strdup_printf ("%" G_GUINT64_FORMAT, pretty_value);
                break;

        default:
        case SK_SMART_ATTRIBUTE_UNIT_UNKNOWN:
                /* Translators: Used in the treeview for the pretty/interpreted value of an attribute
                 * where the value cannot be interpreted */
                ret = g_strdup (_("N/A"));
                break;
        }

        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
find_row_with_name (GtkListStore *list_store,
                    const gchar *name,
                    GtkTreeIter *out_iter)
{
        GtkTreeIter iter;
        gboolean ret;

        ret = FALSE;

        if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store), &iter)) {
                do {
                        gchar *row_name;

                        gtk_tree_model_get (GTK_TREE_MODEL (list_store),
                                            &iter,
                                            NAME_COLUMN, &row_name,
                                            -1);
                        if (g_strcmp0 (name, row_name) == 0) {
                                g_free (row_name);
                                ret = TRUE;
                                if (out_iter != NULL)
                                        *out_iter = iter;
                                goto out;
                        }
                        g_free (row_name);
                } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store), &iter));
        }

 out:
        return ret;
}

static void
attr_foreach_add_cb (SkDisk                           *d,
                     const SkSmartAttributeParsedData *a,
                     void                             *user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);
        GtkTreeIter iter;
        gchar *pretty_str;
        gchar *tooltip_str;
        const gchar *tip_type_str;
        const gchar *tip_updates_str;

        if (a->prefailure) {
                /* Translators: Used in the tooltip for a row in the attribute treeview - please keep
                 * "(Pre-Fail)" in English */
                tip_type_str = _("Failure is a sign of imminent disk failure (Pre-Fail)");
        } else {
                /* Translators: Used in the tooltip for a row in the attribute treeview - please keep
                 * "(Old-Age)" in English */
                tip_type_str = _("Failure is a sign of old age (Old-Age)");
        }

        if (a->online) {
                /* Translators: Used in the tooltip for a row in the attribute treeview - please keep
                 * "(Online)" in English */
                tip_updates_str = _("Every time data is collected (Online)");
        } else {
                /* Translators: Used in the tooltip for a row in the attribute treeview - please keep
                 * "(Not Online)" in English */
                tip_updates_str = _("Only during off-line activities (Not Online)");
        }

        /* Translators: Used in the tooltip for a row in the attribute treeview.
         * First %s is the type of the attribute (Pre-Fail or Old-Age).
         * Second %s is the update type (Online or Not Online).
         * The six %x is the raw data of the attribute.
         */
        tooltip_str = g_strdup_printf (_("Type: %s\n"
                                         "Updates: %s\n"
                                         "Raw: 0x%02x%02x%02x%02x%02x%02x"),
                                       tip_type_str,
                                       tip_updates_str,
                                       a->raw[0], a->raw[1], a->raw[2], a->raw[3], a->raw[4], a->raw[5]);

        pretty_str = pretty_to_string (a->pretty_value, a->pretty_unit);

        gtk_list_store_append (dialog->priv->attr_list_store, &iter);
        gtk_list_store_set (dialog->priv->attr_list_store, &iter,
                            ID_COLUMN, a->id,
                            NAME_COLUMN, a->name,
                            VALUE_COLUMN, pretty_str,
                            TOOLTIP_COLUMN, tooltip_str,
                            SK_ATTR_COLUMN, a,
                            -1);
        g_free (pretty_str);
        g_free (tooltip_str);
}

static gboolean
is_self_test_running (GduDevice *device,
                      SkSmartSelfTest *out_test_type)
{
        gboolean ret;

        ret = FALSE;

        if (!gdu_device_job_in_progress (device))
                goto out;

        if (g_strcmp0 (gdu_device_job_get_id (device), "DriveAtaSmartSelftestShort") == 0) {
                ret = TRUE;
                if (out_test_type != NULL)
                        *out_test_type = SK_SMART_SELF_TEST_SHORT;
        } else if (g_strcmp0 (gdu_device_job_get_id (device), "DriveAtaSmartSelftestExtended") == 0) {
                ret = TRUE;
                if (out_test_type != NULL)
                        *out_test_type = SK_SMART_SELF_TEST_EXTENDED;
        } else if (g_strcmp0 (gdu_device_job_get_id (device), "DriveAtaSmartSelftestConveyance") == 0) {
                ret = TRUE;
                if (out_test_type != NULL)
                        *out_test_type = SK_SMART_SELF_TEST_CONVEYANCE;
        }

 out:
        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* TODO: in the future we probably want to get/set this via the daemon - e.g. so the preference also
 * takes effect system-wide */

/* TODO: keep in sync with src/notification/notification-main.c */
static gboolean
get_ata_smart_no_warn (GduDevice *device)
{
        gboolean ret;
        gchar *path;
        gchar *disk_id;
        struct stat stat_buf;

        ret = FALSE;

        disk_id = g_strdup_printf ("%s-%s-%s-%s",
                                   gdu_device_drive_get_vendor (device),
                                   gdu_device_drive_get_model (device),
                                   gdu_device_drive_get_revision (device),
                                   gdu_device_drive_get_serial (device));

        path = g_build_filename (g_get_user_config_dir (),
                                 "gnome-disk-utility",
                                 "ata-smart-ignore",
                                 disk_id,
                                 NULL);

        if (g_stat (path, &stat_buf) == 0) {
                ret = TRUE;
        }

        g_free (path);
        g_free (disk_id);

        return ret;
}

static gboolean
set_ata_smart_no_warn (GduDevice *device,
                       gboolean   no_warn)
{
        gboolean ret;
        gchar *path;
        gchar *dir_path;
        gchar *disk_id;
        gint fd;

        disk_id = NULL;
        dir_path = NULL;
        path = NULL;
        ret = FALSE;

        disk_id = g_strdup_printf ("%s-%s-%s-%s",
                                   gdu_device_drive_get_vendor (device),
                                   gdu_device_drive_get_model (device),
                                   gdu_device_drive_get_revision (device),
                                   gdu_device_drive_get_serial (device));

        dir_path = g_build_filename (g_get_user_config_dir (),
                                     "gnome-disk-utility",
                                     "ata-smart-ignore",
                                     NULL);

        path = g_build_filename (dir_path,
                                 disk_id,
                                 NULL);

        if (g_mkdir_with_parents (dir_path, 0755) != 0) {
                g_warning ("Error creating directory `%s': %s",
                           dir_path,
                           g_strerror (errno));
                goto out;
        }

        if (no_warn) {
                fd = g_creat (path, 0644);
                if (fd == -1) {
                        g_warning ("Error creating file `%s': %s",
                                   path,
                                   g_strerror (errno));
                }
                close (fd);
        } else {
                if (g_unlink (path) != 0) {
                        g_warning ("Error unlinking `%s': %s",
                                   path,
                                   g_strerror (errno));
                        goto out;
                }
        }

        ret = TRUE;

 out:
        g_free (path);
        g_free (dir_path);
        g_free (disk_id);

        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_dialog (GduAtaSmartDialog *dialog)
{
        gchar *self_assessment_text;
        gchar *overall_assessment_text;
        gchar *bad_sectors_text;
        gchar *powered_on_text;
        gchar *model_text;
        gchar *firmware_text;
        gchar *serial_text;
        gchar *temperature_text;
        gchar *selftest_text;
        gchar *action_text;
        gboolean highlight;
        GTimeVal updated;
        gconstpointer blob;
        gsize blob_size;
        GIcon *status_icon;
        gchar *s;
        SkBool self_assessment_good;
        uint64_t num_bad_sectors;
        uint64_t power_on_msec;
        uint64_t temperature_mkelvin;
        SkSmartSelfTest test_type;
        SkDisk *sk_disk;
        const SkSmartParsedData *parsed_data;
        const SkIdentifyParsedData *parsed_identify_data;
        gboolean no_warn;

        self_assessment_text = NULL;
        overall_assessment_text = NULL;
        bad_sectors_text = NULL;
        model_text = NULL;
        firmware_text = NULL;
        serial_text = NULL;
        powered_on_text = NULL;
        temperature_text = NULL;
        selftest_text = NULL;
        action_text = NULL;
        updated.tv_sec = 0;
        dialog->priv->last_updated = 0;
        status_icon = NULL;
        sk_disk = NULL;

        /* avoid updating anything if the widgets hasn't been constsructed */
        if (!dialog->priv->has_been_constructed)
                goto out;

        if (dialog->priv->device == NULL) {
                /* Translators: Shown in the "Overall Assessment" item in the status table
                 * when no drive is currently selected */
                overall_assessment_text = g_strdup (_("No drive selected"));
                goto has_data;
        }

        if (!gdu_device_drive_ata_smart_get_is_available (dialog->priv->device)) {
                /* Translators: Shown in the "Overall Assessment" item in the status table
                 * when SMART is not available */
                overall_assessment_text = g_strdup (_("SMART not supported"));
                goto has_data;
        }

        blob = gdu_device_drive_ata_smart_get_blob (dialog->priv->device, &blob_size);
        if (blob == NULL) {
                /* Translators: Shown in the "Overall Assessment" item in the status table
                 * when SMART is supported but data was never collected */
                overall_assessment_text = g_strdup (_("SMART data never collected"));
                goto has_data;
        }

        if (sk_disk_open (NULL, &sk_disk) != 0) {
                /* Translators: Shown in the "Overall Assessment" item in the status table
                 * when the SMART data is malformed */
                overall_assessment_text = g_strdup (_("SMART data is malformed"));
                goto has_data;
        }
        if (sk_disk_set_blob (sk_disk, blob, blob_size) != 0) {
                /* Translators: Shown in the "Overall Assessment" item in the status table
                 * when the SMART data is malformed */
                overall_assessment_text = g_strdup (_("SMART data is malformed"));
                goto has_data;
        }

        dialog->priv->last_updated = updated.tv_sec = gdu_device_drive_ata_smart_get_time_collected (dialog->priv->device);
        updated.tv_usec = 0;

        s = gdu_util_ata_smart_status_to_desc (gdu_device_drive_ata_smart_get_status (dialog->priv->device),
                                               &highlight,
                                               &action_text,
                                               &status_icon);
        if (highlight) {
                gchar *s2;
                s2 = g_strdup_printf ("<span fgcolor=\"red\"><b>%s</b></span>", s);
                g_free (s);
                s = s2;
        }
        if (action_text != NULL) {
                overall_assessment_text = g_strdup_printf ("%s\n"
                                                           "<small>%s</small>",
                                                           s,
                                                           action_text);
                g_free (action_text);
        } else {
                overall_assessment_text = s;
                s = NULL;
        }

        if (sk_disk_smart_status (sk_disk, &self_assessment_good) != 0) {
                /* Translators: Shown in the "Self-assessment" item in the status table
                 * when the self-assessment of the drive is unknown */
                self_assessment_text = g_strdup (_("Unknown"));
        } else {
                if (self_assessment_good) {
                        /* Translators: Shown in the "Self-assessment" item in the status table
                         * when the self-assessment of the drive is PASSED */
                        self_assessment_text = g_strdup (_("Passed"));
                } else {
                        self_assessment_text = g_strdup_printf ("<span foreground='red'><b>%s</b></span>",
                                                                /* Translators: Shown in the "Self-assessment" item in
                                                                 * the status table when the self-assessment of the
                                                                 * drive is FAILING */
                                                                _("FAILING"));
                }
        }

        if (sk_disk_smart_get_bad (sk_disk, &num_bad_sectors) != 0) {
                /* Translators: Shown in the "Bad Sectors" item in the status table
                 * when we don't know if the disk has bad sectors */
                bad_sectors_text = g_strdup (_("Unknown"));
        } else {
                if (num_bad_sectors == 0) {
                        /* Translators: Shown in the "Bad Sectors" item in the status table
                         * when we the disk has no bad sectors */
                        bad_sectors_text = g_strdup (_("None"));
                } else {
                        /* Translators: Shown in the "Bad Sectors" item in the status table
                         * when we the disk has one or more bad sectors */
                        bad_sectors_text = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                                                       "%d bad sector",
                                                                       "%d bad sectors",
                                                                       (gint) num_bad_sectors),
                                                            (gint) num_bad_sectors);
                }
        }

        if (sk_disk_smart_get_power_on (sk_disk, &power_on_msec) != 0) {
                /* Translators: Shown in the "Powered On" item in the status table when we don't know
                 * the amount of time the disk has been powered on */
                powered_on_text = g_strdup (_("Unknown"));
        } else {
                powered_on_text = pretty_to_string (power_on_msec, SK_SMART_ATTRIBUTE_UNIT_MSECONDS);
        }

        if (sk_disk_smart_get_temperature (sk_disk, &temperature_mkelvin) != 0) {
                /* Translators: Shown in the "Temperature" item in the status table when we don't know
                 * the temperature of the disk
                 */
                temperature_text = g_strdup (_("Unknown"));
        } else {
                temperature_text = pretty_to_string (temperature_mkelvin, SK_SMART_ATTRIBUTE_UNIT_MKELVIN);
        }

        if (sk_disk_identify_parse (sk_disk, &parsed_identify_data) == 0) {
                model_text = g_strdup (parsed_identify_data->model);
                firmware_text = g_strdup (parsed_identify_data->firmware);
                serial_text = g_strdup (parsed_identify_data->serial);
        }

        if (sk_disk_smart_parse (sk_disk, &parsed_data) == 0) {
                const gchar *self_text;
                gboolean highlight;

                highlight = FALSE;
                switch (parsed_data->self_test_execution_status) {
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_SUCCESS_OR_NEVER:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test completed OK");
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_ABORTED:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test was cancelled");
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_INTERRUPTED:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test was cancelled (with hard or soft reset)");
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_FATAL:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test not completed (a fatal error might have occured)");
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_ELECTRICAL:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test FAILED (Electrical)");
                        highlight = TRUE;
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_SERVO:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test FAILED (Servo)");
                        highlight = TRUE;
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_READ:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test FAILED (Read)");
                        highlight = TRUE;
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_HANDLING:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Last self-test FAILED (Suspected of having handled damage)");
                        highlight = TRUE;
                        break;
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_INPROGRESS:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Self-test is in progress");
                        highlight = TRUE;
                        break;

                default:
                case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_UNKNOWN:
                        /* Translators: Shown in the "Self-tests" item in the status table */
                        self_text = _("Unknown");
                        break;
                }

                if (highlight)
                        selftest_text = g_strdup_printf ("<span foreground='red'><b>%s</b></span>", self_text);
                else
                        selftest_text = g_strdup (self_text);
        }

 has_data:

        if (status_icon == NULL)
                status_icon = g_themed_icon_new ("gdu-smart-unknown");
        gtk_image_set_from_gicon (GTK_IMAGE (dialog->priv->overall_assessment_image),
                                  status_icon,
                                  GTK_ICON_SIZE_MENU);

        gtk_label_set_markup (GTK_LABEL (dialog->priv->self_assessment_label), self_assessment_text != NULL ? self_assessment_text : "-");
        gtk_label_set_markup (GTK_LABEL (dialog->priv->overall_assessment_label), overall_assessment_text != NULL ? overall_assessment_text : "-");
        gtk_label_set_markup (GTK_LABEL (dialog->priv->sectors_label), bad_sectors_text != NULL ? bad_sectors_text : "-");
        gtk_label_set_markup (GTK_LABEL (dialog->priv->power_on_hours_label), powered_on_text != NULL ? powered_on_text : "-");
        gtk_label_set_markup (GTK_LABEL (dialog->priv->temperature_label), temperature_text != NULL ? temperature_text : "-");
        gtk_label_set_markup (GTK_LABEL (dialog->priv->model_label), model_text != NULL ? model_text : "-");
        gtk_label_set_markup (GTK_LABEL (dialog->priv->firmware_label), firmware_text != NULL ? firmware_text : "-");
        gtk_label_set_markup (GTK_LABEL (dialog->priv->serial_label), serial_text != NULL ? serial_text : "-");
        if (updated.tv_sec == 0) {
                gdu_time_label_set_time (GDU_TIME_LABEL (dialog->priv->updated_label), NULL);
                gtk_label_set_markup (GTK_LABEL (dialog->priv->updated_label), "-");
        } else {
                gdu_time_label_set_time (GDU_TIME_LABEL (dialog->priv->updated_label), &updated);
        }
        gtk_label_set_markup (GTK_LABEL (dialog->priv->self_test_result_label), selftest_text != NULL ? selftest_text : "-");

        if (sk_disk == NULL) {
                gtk_list_store_clear (dialog->priv->attr_list_store);
        } else {
                GtkTreeIter iter;
                gchar *name_selected;

                /* keep selected row if it exists after the refresh (disk may have changed) */
                name_selected = NULL;
                if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->tree_view)),
                                                     NULL,
                                                     &iter)) {
                        gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->attr_list_store),
                                            &iter,
                                            NAME_COLUMN, &name_selected,
                                            -1);
                }

                gtk_list_store_clear (dialog->priv->attr_list_store);
                sk_disk_smart_parse_attributes (sk_disk,
                                                attr_foreach_add_cb,
                                                dialog);

                if (name_selected != NULL) {
                        if (!find_row_with_name (dialog->priv->attr_list_store, name_selected, &iter))
                                gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dialog->priv->attr_list_store), &iter);
                        g_free (name_selected);
                } else {
                        gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dialog->priv->attr_list_store), &iter);
                }
                gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->priv->tree_view)),
                                                &iter);
        }

        /* update "no warning" check button */
        no_warn = FALSE;
        if (dialog->priv->device != NULL)
                no_warn = get_ata_smart_no_warn (dialog->priv->device);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->no_warn_check_button), no_warn);

        /* control visility of update widgets */
        if (dialog->priv->is_updating) {
                gtk_widget_hide (dialog->priv->updated_label);
                gtk_widget_hide (dialog->priv->update_link_label);
                gtk_widget_show (dialog->priv->updating_spinner);
                gtk_widget_show (dialog->priv->updating_label);
                gdu_spinner_start (GDU_SPINNER (dialog->priv->updating_spinner));
        } else {
                gtk_widget_hide (dialog->priv->updating_spinner);
                gdu_spinner_stop (GDU_SPINNER (dialog->priv->updating_spinner));
                gtk_widget_hide (dialog->priv->updating_label);
                gtk_widget_show (dialog->priv->updated_label);
                if (dialog->priv->device == NULL) {
                        gtk_widget_hide (dialog->priv->update_link_label);
                } else {
                        gtk_widget_show (dialog->priv->update_link_label);
                }
        }

        /* control visibility of self-test widgets */
        if (dialog->priv->device != NULL && is_self_test_running (dialog->priv->device, &test_type)) {
                gdouble fraction;
                const gchar *test_type_str;

                fraction = gdu_device_job_get_percentage (dialog->priv->device) / 100.0;
                if (fraction < 0.0)
                        fraction = 0.0;
                if (fraction > 1.0)
                        fraction = 1.0;

                gtk_widget_show (dialog->priv->self_test_result_label);
                gtk_widget_hide (dialog->priv->self_test_run_link_label);
                gtk_widget_show (dialog->priv->self_test_progress_bar);
                if (gdu_device_job_is_cancellable (dialog->priv->device))
                        gtk_widget_show (dialog->priv->self_test_cancel_link_label);
                else
                        gtk_widget_hide (dialog->priv->self_test_cancel_link_label);
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (dialog->priv->self_test_progress_bar),
                                               fraction);

                switch (test_type) {
                case SK_SMART_SELF_TEST_SHORT:
                        /* Translators: Shown in the "Self-tests" item in the status table when a test is underway */
                        test_type_str = _("Short self-test in progress: ");
                        break;
                case SK_SMART_SELF_TEST_EXTENDED:
                        /* Translators: Shown in the "Self-tests" item in the status table when a test is underway */
                        test_type_str = _("Extended self-test in progress: ");
                        break;
                case SK_SMART_SELF_TEST_CONVEYANCE:
                        /* Translators: Shown in the "Self-tests" item in the status table when a test is underway */
                        test_type_str = _("Conveyance self-test in progress: ");
                        break;
                default:
                        g_assert_not_reached ();
                        break;
                }
                gtk_label_set_markup (GTK_LABEL (dialog->priv->self_test_result_label),
                                      test_type_str);
        } else {
                gtk_widget_hide (dialog->priv->self_test_progress_bar);
                gtk_widget_hide (dialog->priv->self_test_cancel_link_label);
                gtk_widget_show (dialog->priv->self_test_result_label);
                if (dialog->priv->device == NULL) {
                        gtk_widget_hide (dialog->priv->self_test_run_link_label);
                } else {
                        gtk_widget_show (dialog->priv->self_test_run_link_label);
                }
        }

        if (sk_disk != NULL)
                sk_disk_free (sk_disk);

 out:
        g_free (overall_assessment_text);
        g_free (self_assessment_text);
        g_free (powered_on_text);
        g_free (model_text);
        g_free (firmware_text);
        g_free (serial_text);
        g_free (temperature_text);
        g_free (selftest_text);
        if (status_icon != NULL)
                g_object_unref (status_icon);
}

static void
device_changed (GduDevice *device,
                gpointer   user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);

        if (gdu_device_drive_ata_smart_get_time_collected (dialog->priv->device) != dialog->priv->last_updated) {
                update_dialog (dialog);
        }

}

static void
device_job_changed (GduDevice *device,
                    gpointer   user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);

        update_dialog (dialog);
}
