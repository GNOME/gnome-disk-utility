/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ata-smart-attribute.c
 *
 * Copyright (C) 2007 David Zeuthen
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
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <time.h>

#include "gdu-private.h"
#include "gdu-ata-smart-attribute.h"

struct _GduAtaSmartAttributePrivate {
        guint id;
        gchar *name;
        guint flags;
        gboolean online, prefailure;
        guchar current;
        gboolean current_valid;
        guchar worst;
        gboolean worst_valid;
        guchar threshold;
        gboolean threshold_valid;
        gboolean good, good_valid;
        guint pretty_unit;
        guint64 pretty_value;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduAtaSmartAttribute, gdu_ata_smart_attribute, G_TYPE_OBJECT);

static void
gdu_ata_smart_attribute_finalize (GduAtaSmartAttribute *attribute)
{
        g_free (attribute->priv->name);
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (attribute));
}

static void
gdu_ata_smart_attribute_class_init (GduAtaSmartAttributeClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_ata_smart_attribute_finalize;

        g_type_class_add_private (klass, sizeof (GduAtaSmartAttributePrivate));
}

static void
gdu_ata_smart_attribute_init (GduAtaSmartAttribute *attribute)
{
        attribute->priv = G_TYPE_INSTANCE_GET_PRIVATE (attribute, GDU_TYPE_ATA_SMART_ATTRIBUTE, GduAtaSmartAttributePrivate);
}

guint
gdu_ata_smart_attribute_get_id (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->id;
}

guint
gdu_ata_smart_attribute_get_flags (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->flags;
}

gboolean
gdu_ata_smart_attribute_get_online (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->online;
}

gboolean
gdu_ata_smart_attribute_get_prefailure (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->prefailure;
}

guint
gdu_ata_smart_attribute_get_current (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->current;
}

gboolean
gdu_ata_smart_attribute_get_current_valid (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->current_valid;
}

guint
gdu_ata_smart_attribute_get_worst (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->worst;
}

gboolean
gdu_ata_smart_attribute_get_worst_valid (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->worst_valid;
}

guint
gdu_ata_smart_attribute_get_threshold (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->threshold;
}

gboolean
gdu_ata_smart_attribute_get_threshold_valid (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->threshold_valid;
}

gboolean
gdu_ata_smart_attribute_get_good (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->good;
}

gboolean
gdu_ata_smart_attribute_get_good_valid (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->good_valid;
}

guint64
gdu_ata_smart_attribute_get_pretty_value (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->pretty_value;
}

GduAtaSmartAttributeUnit
gdu_ata_smart_attribute_get_pretty_unit (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->pretty_unit;
}

static void
attribute_get_details (GduAtaSmartAttribute  *attr,
                       gchar                    **out_name,
                       gchar                    **out_description,
                       gboolean                  *out_warn)
{
        const char *n;
        const char *d;
        gboolean warn;

        /* See http://ata_smartmontools.sourceforge.net/doc.html
         *     http://en.wikipedia.org/wiki/S.M.A.R.T
         *     http://www.t13.org/Documents/UploadedDocuments/docs2005/e05148r0-ACS-ATA_SMARTAttributesAnnex.pdf
         */

        n = NULL;
        d = NULL;
        warn = FALSE;
        switch (attr->priv->id) {
        case 1:
                n = _("Read Error Rate");
                d = _("Frequency of errors while reading raw data from the disk. "
                      "A non-zero value indicates a problem with "
                      "either the disk surface or read/write heads.");
                break;
        case 2:
                n = _("Throughput Performance");
                d = _("Average effeciency of the disk.");
                break;
        case 3:
                n = _("Spinup Time");
                d = _("Time needed to spin up the disk.");
                break;
        case 4:
                n = _("Start/Stop Count");
                d = _("Number of spindle start/stop cycles.");
                break;
        case 5:
                n = _("Reallocated Sector Count");
                d = _("Count of remapped sectors. "
                      "When the hard drive finds a read/write/verification error, it mark the sector "
                      "as \"reallocated\" and transfers data to a special reserved area (spare area).");
                break;
        case 7:
                n = _("Seek Error Rate");
                d = _("Frequency of errors while positioning.");
                break;
        case 8:
                n = _("Seek Timer Performance");
                d = _("Average efficiency of operatings while positioning");
                break;
        case 9:
                n = _("Power-On Hours");
                d = _("Number of hours elapsed in the power-on state.");
                break;
        case 10:
                n = _("Spinup Retry Count");
                d = _("Number of retry attempts to spin up.");
                break;
        case 11:
                n = _("Calibration Retry Count");
                d = _("Number of attempts to calibrate the device.");
                break;
        case 12:
                n = _("Power Cycle Count");
                d = _("Number of power-on events.");
                break;
        case 13:
                n = _("Soft read error rate");
                d = _("Frequency of 'program' errors while reading from the disk.");
                break;

        case 191:
                n = _("G-sense Error Rate");
                d = _("Frequency of mistakes as a result of impact loads.");
                break;
        case 192:
                n = _("Power-off Retract Count");
                d = _("Number of power-off or emergency retract cycles.");
                break;
        case 193:
                n = _("Load/Unload Cycle Count");
                d = _("Number of cycles into landing zone position.");
                break;
        case 194:
                n = _("Temperature");
                d = _("Current internal temperature in degrees Celcius.");
                break;
        case 195:
                n = _("Hardware ECC Recovered");
                d = _("Number of ECC on-the-fly errors.");
                break;
        case 196:
                n = _("Reallocation Count");
                d = _("Number of remapping operations. "
                      "The raw value of this attribute shows the total number of (successful "
                      "and unsucessful) attempts to transfer data from reallocated sectors "
                      "to a spare area.");
                break;
        case 197:
                n = _("Current Pending Sector Count");
                d = _("Number of sectors waiting to be remapped. "
                      "If the sector waiting to be remapped is subsequently written or read "
                      "successfully, this value is decreased and the sector is not remapped. Read "
                      "errors on the sector will not remap the sector, it will only be remapped on "
                      "a failed write attempt.");
                if (attr->priv->pretty_value > 0)
                        warn = TRUE;
                break;
        case 198:
                n = _("Uncorrectable Sector Count");
                d = _("The total number of uncorrectable errors when reading/writing a sector. "
                      "A rise in the value of this attribute indicates defects of the "
                      "disk surface and/or problems in the mechanical subsystem.");
                break;
        case 199:
                n = _("UDMA CRC Error Rate");
                d = _("Number of CRC errors during UDMA mode.");
                break;
        case 200:
                n = _("Write Error Rate");
                d = _("Number of errors while writing to disk (or) multi-zone error rate (or) flying-height.");
                break;
        case 201:
                n = _("Soft Read Error Rate");
                d = _("Number of off-track errors.");
                break;
        case 202:
                n = _("Data Address Mark Errors");
                d = _("Number of Data Address Mark (DAM) errors (or) vendor-specific.");
                break;
        case 203:
                n = _("Run Out Cancel");
                d = _("Number of ECC errors.");
                break;
        case 204:
                n = _("Soft ECC correction");
                d = _("Number of errors corrected by software ECC.");
                break;
        case 205:
                n = _("Thermal Asperity Rate");
                d = _("Number of Thermal Asperity Rate errors.");
                break;
        case 206:
                n = _("Flying Height");
                d = _("Height of heads above the disk surface.");
                break;
        case 207:
                n = _("Spin High Current");
                d = _("Amount of high current used to spin up the drive.");
                break;
        case 208:
                n = _("Spin Buzz");
                d = _("Number of buzz routines to spin up the drive.");
                break;
        case 209:
                n = _("Offline Seek Performance");
                d = _("Drive's seek performance during offline operations.");
                break;

        case 220:
                n = _("Disk Shift");
                d = _("Shift of disk os possible as a result of strong shock loading in the store, "
                      "as a result of falling (or) temperature.");
                break;
        case 221:
                n = _("G-sense Error Rate");
                d = _("Number of errors as a result of impact loads as detected by a shock sensor.");
                break;
        case 222:
                n = _("Loaded Hours");
                d = _("Number of hours in general operational state.");
                break;
        case 223:
                n = _("Load/Unload Retry Count");
                d = _("Loading on drive caused by numerous recurrences of operations, like reading, "
                      "recording, positioning of heads, etc.");
                break;
        case 224:
                n = _("Load Friction");
                d = _("Load on drive cause by friction in mechanical parts of the store.");
                break;
        case 225:
                n = _("Load/Unload Cycle Count");
                d = _("Total number of load cycles.");
                break;
        case 226:
                n = _("Load-in Time");
                d = _("General time for loading in a drive.");
                break;
        case 227:
                n = _("Torque Amplification Count");
                d = _("Quantity efforts of the rotating moment of a drive.");
                break;
        case 228:
                n = _("Power-off Retract Count");
                d = _("Number of power-off retract events.");
                break;

        case 230:
                n = _("GMR Head Amplitude");
                d = _("Amplitude of heads trembling (GMR-head) in running mode.");
                break;
        case 231:
                n = _("Temperature");
                d = _("Temperature of the drive.");
                break;

        case 240:
                n = _("Head Flying Hours");
                d = _("Time while head is positioning.");
                break;
        case 250:
                n = _("Read Error Retry Rate");
                d = _("Number of errors while reading from a disk.");
                break;
        default:
                break;
        }

        if (out_name != NULL)
                *out_name = g_strdup (n);
        if (out_description != NULL)
                *out_description = g_strdup (d);
        if (out_warn != NULL)
                *out_warn = warn;
}


const gchar *
gdu_ata_smart_attribute_get_name (GduAtaSmartAttribute *attribute)
{
        return attribute->priv->name;
}

gchar *
gdu_ata_smart_attribute_get_localized_name (GduAtaSmartAttribute *attribute)
{
        char *s;
        attribute_get_details (attribute, &s, NULL, NULL);
        if (s == NULL)
                s = g_strdup (attribute->priv->name);
        return s;
}

gchar *
gdu_ata_smart_attribute_get_localized_description (GduAtaSmartAttribute *attribute)
{
        char *s;
        attribute_get_details (attribute, NULL, &s, NULL);
        return s;
}

GduAtaSmartAttribute *
_gdu_ata_smart_attribute_new (gpointer data)
{
        GValue elem = {0};
        GduAtaSmartAttribute *attribute;

        attribute = GDU_ATA_SMART_ATTRIBUTE (g_object_new (GDU_TYPE_ATA_SMART_ATTRIBUTE, NULL));

        g_value_init (&elem, ATA_SMART_ATTRIBUTE_STRUCT_TYPE);
        g_value_set_static_boxed (&elem, data);

        dbus_g_type_struct_get (&elem,
                                0, &attribute->priv->id,
                                1, &attribute->priv->name,
                                2, &attribute->priv->flags,
                                3, &attribute->priv->online,
                                4, &attribute->priv->prefailure,
                                5, &attribute->priv->current,
                                6, &attribute->priv->current_valid,
                                7, &attribute->priv->worst,
                                8, &attribute->priv->worst_valid,
                                9, &attribute->priv->threshold,
                                10, &attribute->priv->threshold_valid,
                                11, &attribute->priv->good,
                                12, &attribute->priv->good_valid,
                                13, &attribute->priv->pretty_unit,
                                14, &attribute->priv->pretty_value,
                                //15, &raw_data,
                                G_MAXUINT);

        return attribute;
}
