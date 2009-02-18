/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-smart-data-attribute.c
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
#include "gdu-smart-data-attribute.h"

struct _GduSmartDataAttributePrivate {
        /* TODO: use guint8 */
        int id;
        int value;
        int worst;
        int threshold;
        int flags;
        char *raw;
        char *name;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSmartDataAttribute, gdu_smart_data_attribute, G_TYPE_OBJECT);

static void
gdu_smart_data_attribute_finalize (GduSmartDataAttribute *smart_data_attribute)
{
        g_free (smart_data_attribute->priv->raw);
        g_free (smart_data_attribute->priv->name);
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (smart_data_attribute));
}

static void
gdu_smart_data_attribute_class_init (GduSmartDataAttributeClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_smart_data_attribute_finalize;

        g_type_class_add_private (klass, sizeof (GduSmartDataAttributePrivate));
}

static void
gdu_smart_data_attribute_init (GduSmartDataAttribute *smart_data_attribute)
{
        smart_data_attribute->priv = G_TYPE_INSTANCE_GET_PRIVATE (smart_data_attribute, GDU_TYPE_SMART_DATA_ATTRIBUTE, GduSmartDataAttributePrivate);
}

int
gdu_smart_data_attribute_get_id (GduSmartDataAttribute *smart_data_attribute)
{
        return smart_data_attribute->priv->id;
}

int
gdu_smart_data_attribute_get_flags (GduSmartDataAttribute *smart_data_attribute)
{
        return smart_data_attribute->priv->flags;
}

int
gdu_smart_data_attribute_get_value (GduSmartDataAttribute *smart_data_attribute)
{
        return smart_data_attribute->priv->value;
}

int
gdu_smart_data_attribute_get_worst (GduSmartDataAttribute *smart_data_attribute)
{
        return smart_data_attribute->priv->worst;
}

int
gdu_smart_data_attribute_get_threshold (GduSmartDataAttribute *smart_data_attribute)
{
        return smart_data_attribute->priv->threshold;
}

char *
gdu_smart_data_attribute_get_raw (GduSmartDataAttribute *smart_data_attribute)
{
        return g_strdup (smart_data_attribute->priv->raw);
}

static void
attribute_get_details (GduSmartDataAttribute  *attr,
                       char                  **out_name,
                       char                  **out_description,
                       gboolean               *out_should_warn)
{
        const char *n;
        const char *d;
        gboolean warn;
        int raw_int;

        raw_int = atoi (attr->priv->raw);

        /* See http://smartmontools.sourceforge.net/doc.html
         *     http://en.wikipedia.org/wiki/S.M.A.R.T
         *     http://www.t13.org/Documents/UploadedDocuments/docs2005/e05148r0-ACS-SMARTAttributesAnnex.pdf
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
                if (raw_int > 0)
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
        if (out_should_warn != NULL)
                *out_should_warn = warn;
}


char *
gdu_smart_data_attribute_get_name (GduSmartDataAttribute *smart_data_attribute)
{
        char *s;
        attribute_get_details (smart_data_attribute, &s, NULL, NULL);
        if (s == NULL)
                s = g_strdup (smart_data_attribute->priv->name);
        return s;
}

char *
gdu_smart_data_attribute_get_description (GduSmartDataAttribute *smart_data_attribute)
{
        char *s;
        attribute_get_details (smart_data_attribute, NULL, &s, NULL);
        return s;
}

gboolean
gdu_smart_data_attribute_is_warning (GduSmartDataAttribute *smart_data_attribute)
{
        gboolean should_warn;
        attribute_get_details (smart_data_attribute, NULL, NULL, &should_warn);
        return should_warn;
}

gboolean
gdu_smart_data_attribute_is_failing (GduSmartDataAttribute *smart_data_attribute)
{
        return smart_data_attribute->priv->value < smart_data_attribute->priv->threshold;
}

GduSmartDataAttribute *
_gdu_smart_data_attribute_new (gpointer data)
{
        GValue elem = {0};
        GduSmartDataAttribute *smart_data_attribute;

        smart_data_attribute = GDU_SMART_DATA_ATTRIBUTE (g_object_new (GDU_TYPE_SMART_DATA_ATTRIBUTE, NULL));

        g_value_init (&elem, SMART_DATA_STRUCT_TYPE);
        g_value_set_static_boxed (&elem, data);
        dbus_g_type_struct_get (&elem,
                                0, &(smart_data_attribute->priv->id),
                                1, &(smart_data_attribute->priv->name),
                                2, &(smart_data_attribute->priv->flags),
                                3, &(smart_data_attribute->priv->value),
                                4, &(smart_data_attribute->priv->worst),
                                5, &(smart_data_attribute->priv->threshold),
                                6, &(smart_data_attribute->priv->raw),
                                G_MAXUINT);

        return smart_data_attribute;
}
