/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#define _GNU_SOURCE

#include "gdu-create-disk-image-dialog.h"

#include <fcntl.h>

#include <gio/gfiledescriptorbased.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

#include "gdu-application.h"
#include "gdu-job-manager.h"
#include "gdudvdsupport.h"
#include "gduestimator.h"
#include "gdulocaljob.h"

/* TODOs / ideas for Disk Image creation
 *
 * - Be tolerant of I/O errors like dd_rescue(1), see http://www.gnu.org/s/ddrescue/ddrescue.html
 * - Create images useful for Virtualization, e.g. vdi, vmdk, qcow2. Maybe use libguestfs for
 *   this. See http://libguestfs.org/
 * - Support a Apple DMG-ish format
 * - Sliding buffer size
 * - Update time remaining / speed exactly every 1/10th second instead of when we've read a full buffer
 *
 */

/* ---------------------------------------------------------------------------------------------------- */

struct _GduCreateDiskImageDialog {
    AdwDialog parent_instance;

    GtkWidget *name_entry;
    GtkWidget *location_entry;
    GtkWidget *source_label;

    /* UI state and user selections. Copy/job-owned state lives in CreateDiskImageJobData. */
    UDisksObject *object;
    UDisksBlock *block;
    UDisksDrive *drive;
    UDisksClient *client;
    GFile *directory;
};

typedef struct {
    GtkWindow *window;
    UDisksBlock *block;
    UDisksDrive *drive;

    GFile *output_file;
    GFileOutputStream *output_file_stream;
    gchar *source_description;

    /* must hold copy_lock when reading/writing these */
    GMutex copy_lock;
    GduEstimator *estimator;

    gboolean allocating_file;
    gboolean retrieving_dvd_keys;
    guint64 num_error_bytes;
    gboolean played_read_error_sound;

    guint inhibit_cookie;
} CreateDiskImageJobData;

static void create_disk_image_job_data_free (gpointer user_data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CreateDiskImageJobData, create_disk_image_job_data_free)

G_DEFINE_FINAL_TYPE (GduCreateDiskImageDialog, gdu_create_disk_image_dialog, ADW_TYPE_DIALOG)

static GtkWindow *
gdu_create_disk_image_dialog_get_window (GduCreateDiskImageDialog *self)
{
    GtkWidget *window;

    window = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
    return window != NULL ? GTK_WINDOW (window) : NULL;
}

static void
create_disk_image_job_data_uninhibit (CreateDiskImageJobData *data)
{
    if (data->inhibit_cookie > 0) {
        gtk_application_uninhibit (GTK_APPLICATION ((gpointer) g_application_get_default ()), data->inhibit_cookie);
        data->inhibit_cookie = 0;
    }
}

static void
create_disk_image_job_data_free (gpointer user_data)
{
    CreateDiskImageJobData *data = user_data;
    gboolean delete_output_file;

    if (data == NULL)
        return;

    create_disk_image_job_data_uninhibit (data);

    delete_output_file = data->output_file_stream != NULL;

    if (data->output_file_stream != NULL) {
        g_autoptr(GError) error = NULL;

        if (!g_output_stream_close (G_OUTPUT_STREAM (data->output_file_stream), NULL, &error)) {
            g_warning ("Error closing file output stream: %s (%s, %d)", error->message,
                       g_quark_to_string (error->domain), error->code);
        }
    }

    if (delete_output_file && data->output_file != NULL) {
        g_autoptr(GError) error = NULL;

        if (!g_file_delete (data->output_file, NULL, &error)) {
            g_warning ("Error deleting file: %s (%s, %d)", error->message, g_quark_to_string (error->domain),
                       error->code);
        }
    }

    g_clear_object (&data->window);
    g_clear_object (&data->block);
    g_clear_object (&data->drive);
    g_clear_object (&data->output_file);
    g_clear_object (&data->output_file_stream);
    g_clear_object (&data->estimator);
    g_clear_pointer (&data->source_description, g_free);
    g_mutex_clear (&data->copy_lock);
    g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
play_read_error_sound (CreateDiskImageJobData *data)
{
    const gchar *sound_message;

    /* Translators: A descriptive string for the sound played when
     * there's a read error that's being ignored, see
     * CA_PROP_EVENT_DESCRIPTION
     */
    sound_message = _("Disk image read error");
    /* gtk4 todo : Find a replacement for this
    ca_gtk_play_for_widget (GTK_WIDGET (data->window), 0,
                            CA_PROP_EVENT_ID, "dialog-warning",
                            CA_PROP_EVENT_DESCRIPTION, sound_message,
                            NULL);
    */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
create_disk_image_job_update (GduLocalJob *job)
{
    CreateDiskImageJobData *data = gdu_local_job_get_user_data (job);
    g_autofree gchar *extra_markup = NULL;
    guint64 bytes_completed = 0;
    guint64 bytes_target = 0;
    guint64 bytes_per_sec = 0;
    guint64 usec_remaining = 0;
    guint64 num_error_bytes = 0;
    gboolean allocating_file = FALSE;
    gboolean retrieving_dvd_keys = FALSE;
    gboolean played_read_error_sound = FALSE;
    gdouble progress = 0.0;
    gchar *s2, *s3;

    g_mutex_lock (&data->copy_lock);
    if (data->estimator != NULL) {
        bytes_per_sec = gdu_estimator_get_bytes_per_sec (data->estimator);
        usec_remaining = gdu_estimator_get_usec_remaining (data->estimator);
        bytes_completed = gdu_estimator_get_completed_bytes (data->estimator);
        bytes_target = gdu_estimator_get_target_bytes (data->estimator);
        num_error_bytes = data->num_error_bytes;
    }
    allocating_file = data->allocating_file;
    retrieving_dvd_keys = data->retrieving_dvd_keys;
    played_read_error_sound = data->played_read_error_sound;
    g_mutex_unlock (&data->copy_lock);

    if (allocating_file) {
        extra_markup = g_strdup (_("Allocating Disk Image"));
    } else if (retrieving_dvd_keys) {
        extra_markup = g_strdup (_("Retrieving DVD keys"));
    }

    if (num_error_bytes > 0) {
        s2 = g_format_size (num_error_bytes);
        /* Translators: Shown when there are read errors and we skip some data.
         *              The first %s is the amount of unreadable data (ex. "512 kB").
         */
        s3 = g_strdup_printf (_("%s unreadable (replaced with zeroes)"), s2);
        /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
         * of hard-coding the color
         */
        g_free (extra_markup);
        extra_markup = g_strdup_printf ("<span foreground=\"#ff0000\">%s</span>", s3);
        g_free (s3);
        g_free (s2);
    }

    gdu_local_job_set_bytes (job, bytes_target);
    gdu_local_job_set_rate (job, bytes_per_sec);

    if (bytes_target != 0)
        progress = ((gdouble) bytes_completed) / ((gdouble) bytes_target);
    else
        progress = 0.0;
    gdu_local_job_set_progress (job, progress);

    if (usec_remaining == 0)
        gdu_local_job_set_expected_end_time (job, 0);
    else
        gdu_local_job_set_expected_end_time (job, usec_remaining + g_get_real_time ());

    gdu_local_job_set_extra_markup (job, extra_markup);

    /* Play a sound the first time we encounter a read error */
    if (num_error_bytes > 0 && !played_read_error_sound) {
        play_read_error_sound (data);

        g_mutex_lock (&data->copy_lock);
        data->played_read_error_sound = TRUE;
        g_mutex_unlock (&data->copy_lock);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
play_complete_sound (CreateDiskImageJobData *data)
{
    const gchar *sound_message;

    /* Translators: A descriptive string for the 'complete' sound, see CA_PROP_EVENT_DESCRIPTION */
    sound_message = _("Disk image copying complete");
    /* gtk4 todo : Find a replacement for this
    ca_gtk_play_for_widget (GTK_WIDGET (data->window), 0,
                            CA_PROP_EVENT_ID, "complete",
                            CA_PROP_EVENT_DESCRIPTION, sound_message,
                            NULL);
    */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_delete_response (GObject *object, GAsyncResult *response, gpointer user_data)
{
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (object);
    GFile *output_file = G_FILE (user_data);
    g_autoptr(GError) error = NULL;

    if (g_strcmp0 (adw_alert_dialog_choose_finish (dialog, response), "cancel") != 0) {
        if (!g_file_delete (output_file, NULL, &error)) {
            g_warning ("Error deleting file: %s (%s, %d)", error->message, g_quark_to_string (error->domain),
                       error->code);
        }
    }

    g_object_unref (output_file);
}

static void
on_create_disk_image_job_completed (GduLocalJob *job, GduLocalJobResult result, GError *error)
{
    CreateDiskImageJobData *data = gdu_local_job_get_user_data (job);

    if (data == NULL)
        return;

    if (result == GDU_LOCAL_JOB_RESULT_ERROR) {
        if (error != NULL)
            gdu_utils_show_error (data->window, _("Error creating disk image"), error);
        return;
    }

    if (result != GDU_LOCAL_JOB_RESULT_SUCCESS)
        return;

    play_complete_sound (data);

    /* OK, we're done but we had to replace unreadable data with
     * zeroes. Bring up a modal dialog to inform the user of this and
     * allow him to delete the file, if so desired.
     */
    {
        guint64 num_error_bytes = 0;
        guint64 bytes_target = 0;

        g_mutex_lock (&data->copy_lock);
        num_error_bytes = data->num_error_bytes;
        if (data->estimator != NULL)
            bytes_target = gdu_estimator_get_target_bytes (data->estimator);
        g_mutex_unlock (&data->copy_lock);

        if (num_error_bytes > 0) {
            AdwDialog *dialog;
            g_autoptr(GFile) output_file = NULL;
            g_autofree gchar *s = NULL;
            gdouble percentage = 0.0;

            if (bytes_target > 0)
                percentage = 100.0 * ((gdouble) num_error_bytes) / ((gdouble) bytes_target);

            dialog = adw_alert_dialog_new (/* Translators: Heading in dialog shown if some data was unreadable while
                                              creating a disk image */
                                           _("Unrecoverable Read Errors"), NULL);

            s = g_format_size (num_error_bytes);

            adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                          /* Translators: Body in dialog shown if some data was unreadable while
                                           * creating a disk image. The %f is the percentage of unreadable data
                                           * (ex. 13.0). The first %s is the amount of unreadable data (ex. "4.2 MB").
                                           * The second %s is the name of the device (ex "/dev/").
                                           */
                                          _("%2.1f%% (%s) of the data on the device “%s” was unreadable and replaced with zeroes in the created disk image file. This typically happens if the medium is scratched or if there is physical damage to the drive."),
                                            percentage, s, data->source_description);

            adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel",
                                            _("_Cancel"), "confirm", _("_Delete Disk Image File"), NULL);

            adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
            adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "confirm", ADW_RESPONSE_DESTRUCTIVE);

            output_file = g_object_ref (data->output_file);
            adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), data->window != NULL ? GTK_WIDGET (data->window) : NULL,
                                     NULL, on_delete_response, g_steal_pointer (&output_file));
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/* Note that error on reading is *not* considered an error - instead 0
 * is returned.
 *
 * Error conditions include failure to seek or write to output.
 *
 * Returns: Number of bytes actually read (e.g. not include padding) -1 if @error is set.
 */
static gssize
copy_span (gint fd, GOutputStream *output_stream, guint64 offset, guint64 size, guchar *buffer,
           gboolean pad_with_zeroes, GduDVDSupport *dvd_support, GCancellable *cancellable, GError **error)
{
    gint64 ret = -1;
    gssize num_bytes_read;
    gsize num_bytes_to_write;

    g_return_val_if_fail (-1, buffer != NULL);
    g_return_val_if_fail (-1, G_IS_OUTPUT_STREAM (output_stream));
    g_return_val_if_fail (-1, buffer != NULL);
    g_return_val_if_fail (-1, cancellable == NULL || G_IS_CANCELLABLE (cancellable));
    g_return_val_if_fail (-1, error == NULL || *error == NULL);

    if (dvd_support != NULL) {
        num_bytes_read = gdu_dvd_support_read (dvd_support, fd, buffer, offset, size);
    } else {
        if (lseek (fd, offset, SEEK_SET) == (off_t) -1) {
            g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                         "Error seeking to offset %" G_GUINT64_FORMAT ": %s", offset, strerror (errno));
            goto out;
        }
    read_again:
        num_bytes_read = read (fd, buffer, size);
        if (num_bytes_read < 0) {
            if (errno == EAGAIN || errno == EINTR)
                goto read_again;
        } else {
            /* EOF */
            if (num_bytes_read == 0) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Reading from offset %" G_GUINT64_FORMAT " returned zero bytes", offset);
                goto out;
            }
        }
    }

    if (num_bytes_read < 0) {
        /* do not consider this an error - treat as zero bytes read */
        num_bytes_read = 0;
    }

    num_bytes_to_write = num_bytes_read;
    if (pad_with_zeroes && (guint64) num_bytes_read < size) {
        memset (buffer + num_bytes_read, 0, size - num_bytes_read);
        num_bytes_to_write = size;
    }

    if (!g_seekable_seek (G_SEEKABLE (output_stream), offset, G_SEEK_SET, cancellable, error)) {
        g_prefix_error (error, "Error seeking to offset %" G_GUINT64_FORMAT ": ", offset);
        goto out;
    }

    if (!g_output_stream_write_all (G_OUTPUT_STREAM (output_stream), buffer, num_bytes_to_write, G_PRIORITY_DEFAULT,
                                    cancellable, error)) {
        g_prefix_error (error, "Error writing %" G_GSIZE_FORMAT " bytes to offset %" G_GUINT64_FORMAT ": ",
                        num_bytes_to_write, offset);
        goto out;
    }

    ret = num_bytes_read;

out:

    return ret;
}

static GduLocalJobResult
create_disk_image_job_run (GduLocalJob *job, GCancellable *cancellable, GError **out_error)
{
    CreateDiskImageJobData *data = gdu_local_job_get_user_data (job);
    g_autoptr(GduDVDSupport) dvd_support = NULL;
    g_autofree guchar *buffer_unaligned = NULL;
    guchar *buffer;
    guint64 block_device_size = 0;
    glong page_size;
    GError *error = NULL;
    GError *error2 = NULL;
    gint64 last_update_usec = -1;
    gint fd = -1;
    gint buffer_size;
    guint64 num_bytes_completed = 0;

    /* default to 1 MiB blocks */
    buffer_size = (1 * 1024 * 1024);

    /* Most OSes put ACLs for logged-in users on /dev/sr* nodes (this is
     * so CD burning tools etc. work) so see if we can open the device
     * file ourselves. If so, great, since this avoids a polkit dialog.
     *
     * As opposed to udisks' OpenForBackup() we also avoid O_EXCL since
     * the disc is read-only by its very nature. As a side-effect this
     * allows creating a disk image of a mounted disc.
     */
    if (g_str_has_prefix (udisks_block_get_device (data->block), "/dev/sr")) {
        const gchar *device_file = udisks_block_get_device (data->block);
        fd = open (device_file, O_RDONLY);

        /* Use libdvdcss (if available on the system) on DVDs with UDF
         * filesystems - otherwise the backup process may fail because
         * of unreadable/scrambled sectors
         */
        if (g_strcmp0 (udisks_block_get_id_usage (data->block), "filesystem") == 0
            && g_strcmp0 (udisks_block_get_id_type (data->block), "udf") == 0 && data->drive != NULL
            && g_str_has_prefix (udisks_drive_get_media (data->drive), "optical_dvd")) {
            g_mutex_lock (&data->copy_lock);
            data->retrieving_dvd_keys = TRUE;
            g_mutex_unlock (&data->copy_lock);
            gdu_local_job_queue_update (job);

            dvd_support = gdu_dvd_support_new (device_file, udisks_block_get_size (data->block));

            g_mutex_lock (&data->copy_lock);
            data->retrieving_dvd_keys = FALSE;
            g_mutex_unlock (&data->copy_lock);
            gdu_local_job_queue_update (job);
        }
    }

    /* Otherwise, request the fd from udisks */
    if (fd == -1) {
        GUnixFDList *fd_list = NULL;
        GVariant *fd_index = NULL;
        if (!udisks_block_call_open_for_backup_sync (data->block, g_variant_new ("a{sv}", NULL), /* options */
                                                     NULL,                                       /* fd_list */
                                                     &fd_index, &fd_list, cancellable,           /* cancellable */
                                                     &error))
            goto out;

        fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), &error);
        if (error != NULL) {
            g_prefix_error (&error,
                            "Error extracing fd with handle %d from D-Bus message: ", g_variant_get_handle (fd_index));
            goto out;
        }
        if (fd_index != NULL)
            g_variant_unref (fd_index);
        g_clear_object (&fd_list);
    }

    g_assert (fd != -1);

    /* We can't use udisks_block_get_size() because the media may have
     * changed and udisks may not have noticed. TODO: maybe have a
     * Block.GetSize() method instead...
     */
    if (ioctl (fd, BLKGETSIZE64, &block_device_size) != 0) {
        error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno), "%s", strerror (errno));
        g_prefix_error (&error, _("Error determining size of device: "));
        goto out;
    }

    if (block_device_size == 0) {
        error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, _("Device is size 0"));
        goto out;
    }

    /* If supported, allocate space at once to ensure blocks are laid
     * out contigously, see http://lwn.net/Articles/226710/
     */
    if (G_IS_FILE_DESCRIPTOR_BASED (data->output_file_stream)) {
        gint output_fd = g_file_descriptor_based_get_fd (G_FILE_DESCRIPTOR_BASED (data->output_file_stream));
        gint rc;

        g_mutex_lock (&data->copy_lock);
        data->allocating_file = TRUE;
        g_mutex_unlock (&data->copy_lock);
        gdu_local_job_queue_update (job);

        rc = fallocate (output_fd, 0, /* mode */
                        (off_t) 0, (off_t) block_device_size);

        if (rc != 0) {
            if (errno == ENOSYS || errno == EOPNOTSUPP) {
                /* If the kernel or filesystem does not support it, too
                 * bad. Just continue.
                 */
            } else {
                error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno), "%s", strerror (errno));
                g_prefix_error (&error, _("Error allocating space for disk image file: "));
                goto out;
            }
        }

        g_mutex_lock (&data->copy_lock);
        data->allocating_file = FALSE;
        g_mutex_unlock (&data->copy_lock);
        gdu_local_job_queue_update (job);
    }

    page_size = sysconf (_SC_PAGESIZE);
    buffer_unaligned = g_new0 (guchar, buffer_size + page_size);
    buffer = (guchar *) (((gintptr) (buffer_unaligned + page_size)) & (~(page_size - 1)));

    g_mutex_lock (&data->copy_lock);
    data->estimator = gdu_estimator_new (block_device_size);
    data->num_error_bytes = 0;
    g_mutex_unlock (&data->copy_lock);

    /* Read huge (e.g. 1 MiB) blocks and write it to the output
     * file even if it was only partially read.
     */
    num_bytes_completed = 0;
    while (num_bytes_completed < block_device_size) {
        gssize num_bytes_to_read;
        gssize num_bytes_read;
        gint64 now_usec;

        num_bytes_to_read = buffer_size;
        if (num_bytes_to_read + num_bytes_completed > block_device_size)
            num_bytes_to_read = block_device_size - num_bytes_completed;

        /* Update GUI - but only every 200 ms and only if last update isn't pending */
        g_mutex_lock (&data->copy_lock);
        now_usec = g_get_monotonic_time ();
        if (now_usec - last_update_usec > 200 * G_USEC_PER_SEC / 1000 || last_update_usec < 0) {
            if (num_bytes_completed > 0)
                gdu_estimator_add_sample (data->estimator, num_bytes_completed);
            last_update_usec = now_usec;
            g_mutex_unlock (&data->copy_lock);
            gdu_local_job_queue_update (job);
        } else {
            g_mutex_unlock (&data->copy_lock);
        }

        num_bytes_read = copy_span (fd, G_OUTPUT_STREAM (data->output_file_stream), num_bytes_completed,
                                    num_bytes_to_read, buffer, TRUE, /* pad_with_zeroes */
                                    dvd_support, cancellable, &error);
        if (num_bytes_read < 0)
            goto out;

        /*g_print ("read %" G_GUINT64_FORMAT " bytes (requested %" G_GUINT64_FORMAT ") from offset %" G_GUINT64_FORMAT
           "\n", num_bytes_read, num_bytes_to_read, num_bytes_completed);*/

        if (num_bytes_read < num_bytes_to_read) {
            guint64 num_bytes_skipped = num_bytes_to_read - num_bytes_read;
            g_mutex_lock (&data->copy_lock);
            data->num_error_bytes += num_bytes_skipped;
            g_mutex_unlock (&data->copy_lock);
        }
        num_bytes_completed += num_bytes_to_read;
    }

out:
    /* in either case, close the stream */
    if (!g_output_stream_close (G_OUTPUT_STREAM (data->output_file_stream), NULL, /* cancellable */
                                &error2)) {
        g_warning ("Error closing file output stream: %s (%s, %d)", error2->message, g_quark_to_string (error2->domain),
                   error2->code);
        g_clear_error (&error2);
    }
    g_clear_object (&data->output_file_stream);

    if (error != NULL) {
        /* Cleanup */
        if (!g_file_delete (data->output_file, NULL, &error2)) {
            g_warning ("Error deleting file: %s (%s, %d)", error2->message, g_quark_to_string (error2->domain),
                       error2->code);
            g_clear_error (&error2);
        }
    }
    if (fd != -1) {
        if (close (fd) != 0)
            g_warning ("Error closing fd: %m");
    }

    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error (&error);
        return GDU_LOCAL_JOB_RESULT_CANCELLED;
    }

    if (error != NULL) {
        g_propagate_error (out_error, g_steal_pointer (&error));
        return GDU_LOCAL_JOB_RESULT_ERROR;
    }

    if (g_cancellable_is_cancelled (cancellable))
        return GDU_LOCAL_JOB_RESULT_CANCELLED;

    return GDU_LOCAL_JOB_RESULT_SUCCESS;
}

static CreateDiskImageJobData *
create_disk_image_job_data_new (GduCreateDiskImageDialog *self, GFile *output_file,
                                GFileOutputStream *output_file_stream)
{
    CreateDiskImageJobData *data;
    GtkWindow *window;
    const gchar *source_description;

    data = g_new0 (CreateDiskImageJobData, 1);
    g_mutex_init (&data->copy_lock);

    window = gdu_create_disk_image_dialog_get_window (self);
    if (window != NULL)
        data->window = g_object_ref (window);

    data->block = g_object_ref (self->block);
    if (self->drive != NULL)
        data->drive = g_object_ref (self->drive);
    data->output_file = g_object_ref (output_file);
    data->output_file_stream = g_object_ref (output_file_stream);

    source_description = adw_action_row_get_subtitle (ADW_ACTION_ROW (self->source_label));
    data->source_description = g_strdup (source_description != NULL ? source_description : "");

    data->inhibit_cookie = gtk_application_inhibit ((gpointer) g_application_get_default (), data->window,
                                                    GTK_APPLICATION_INHIBIT_SUSPEND | GTK_APPLICATION_INHIBIT_LOGOUT,
                                                    /* Translators: Reason why suspend/logout is being inhibited */
                                                    _("Copying device to disk image"));

    return data;
}

static void
start_copying (GduCreateDiskImageDialog *self)
{
    const gchar *name;
    g_autoptr(CreateDiskImageJobData) data = NULL;
    GduJobManager *job_manager;
    g_autoptr(GduLocalJob) job = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) output_file = NULL;
    g_autoptr(GFileOutputStream) output_file_stream = NULL;

    name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));

    output_file = g_file_get_child (self->directory, name);
    output_file_stream = g_file_replace (output_file, NULL, /* etag */
                                         FALSE,             /* make_backup */
                                         G_FILE_CREATE_NONE, NULL, &error);
    if (output_file_stream == NULL) {
        gdu_utils_show_error (gdu_create_disk_image_dialog_get_window (self), _("Error opening file for writing"),
                                                                                error);
        return;
    }

    /* gtk4 todo */
    /* now that we know the user picked a folder, update file chooser settings */
    // gdu_utils_file_chooser_for_disk_images_set_default_folder (folder);

    data = create_disk_image_job_data_new (self, output_file, output_file_stream);
    job = gdu_local_job_new (self->object, "x-gdu-create-disk-image",
                             _("Creating Disk Image"), create_disk_image_job_run, create_disk_image_job_update,
                               on_create_disk_image_job_completed, g_steal_pointer (&data),
                               create_disk_image_job_data_free);
    if (job == NULL)
        return;

    gdu_local_job_set_progress_valid (job, TRUE);
    gdu_local_job_set_cancelable (job, TRUE);

    job_manager = gdu_application_get_job_manager ();
    if (!gdu_job_manager_enqueue (job_manager, g_steal_pointer (&job)))
        g_warning ("Failed to enqueue create disk image job");
}

static void
ensure_unused_cb (GtkWindow *window, GAsyncResult *res, gpointer user_data)
{
    GduCreateDiskImageDialog *self = GDU_CREATE_DISK_IMAGE_DIALOG (user_data);

    if (gdu_utils_ensure_unused_finish (self->client, res, NULL))
        start_copying (self);

    g_object_unref (self);
}

static void
create_disk_image (GduCreateDiskImageDialog *self)
{
    /* If it's a optical drive, we don't need to try and
     * manually unmount etc.  everything as we're attempting to
     * open it O_RDONLY anyway - see create_disk_image_job_run() for
     * details.
     */
    if (g_str_has_prefix (udisks_block_get_device (self->block), "/dev/sr")) {
        start_copying (self);
        return;
    }

    /* ensure the device is unused (e.g. unmounted) before copying data from it... */
    gdu_utils_ensure_unused (self->client, gdu_create_disk_image_dialog_get_window (self), self->object,
                             (GAsyncReadyCallback) ensure_unused_cb, NULL, /* GCancellable */
                             g_object_ref (self));
}

static void
overwrite_response_cb (GObject *object, GAsyncResult *response, gpointer user_data)
{
    GduCreateDiskImageDialog *self = GDU_CREATE_DISK_IMAGE_DIALOG (user_data);
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (object);

    if (g_strcmp0 (adw_alert_dialog_choose_finish (dialog, response), "cancel") == 0)
        return;

    create_disk_image (self);

    adw_dialog_close (ADW_DIALOG (self));
}

static void
on_create_image_button_clicked_cb (GduCreateDiskImageDialog *self, GtkButton *button)
{
    const gchar *name;
    g_autoptr(GFile) file = NULL;
    ConfirmationDialogData *data;
    GtkWindow *window;

    name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
    file = g_file_get_child (self->directory, name);

    if (!g_file_query_exists (file, NULL)) {
        create_disk_image (self);
        adw_dialog_close (ADW_DIALOG (self));
        return;
    }

    data = g_new0 (ConfirmationDialogData, 1);
    data->message = _("Replace File?");
    data->description = g_strdup_printf (_("A file named “%s” already exists in %s"), name,
                                           gdu_utils_unfuse_path (g_file_get_path (self->directory)));
    data->response_verb = _("Replace");
    data->response_appearance = ADW_RESPONSE_DESTRUCTIVE;
    data->callback = overwrite_response_cb;
    data->user_data = self;

    window = gdu_create_disk_image_dialog_get_window (self);
    gdu_utils_show_confirmation (GTK_WIDGET (window), data, NULL);
}

static void
gdu_create_disk_image_dialog_update_directory (GduCreateDiskImageDialog *self)
{
    g_autofree char *path = NULL;

    path = gdu_utils_unfuse_path (g_file_get_path (self->directory));

    adw_action_row_set_subtitle (ADW_ACTION_ROW (self->location_entry), path);
}

static void
file_dialog_open_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
    GduCreateDiskImageDialog *self = GDU_CREATE_DISK_IMAGE_DIALOG (user_data);
    g_autofree char *path = NULL;
    GFile *directory = NULL;
    GtkFileDialog *file_dialog = GTK_FILE_DIALOG (object);

    directory = gtk_file_dialog_select_folder_finish (file_dialog, res, NULL);
    if (directory) {
        g_clear_object (&self->directory);
        self->directory = directory;
        gdu_create_disk_image_dialog_update_directory (self);
    }
}

static void
on_choose_folder_button_clicked_cb (GduCreateDiskImageDialog *self)
{
    GtkFileDialog *file_dialog;
    GtkWindow *toplevel;

    toplevel = gdu_create_disk_image_dialog_get_window (self);
    if (toplevel == NULL) {
        g_info ("Could not get native window for dialog");
    }

    file_dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (file_dialog, _("Choose a location to save the disk image."));

    gtk_file_dialog_select_folder (file_dialog, toplevel, NULL, file_dialog_open_cb, self);
}

static void
gdu_create_disk_image_dialog_set_default_name (GduCreateDiskImageDialog *self)
{
    g_autoptr(GTimeZone) tz = NULL;
    g_autoptr(GDateTime) now = NULL;
    g_autofree char *now_string = NULL;
    g_autofree char *proposed_filename = NULL;
    GString *device_name;
    const gchar *fstype;
    const gchar *fslabel;

    tz = g_time_zone_new_local ();
    now = g_date_time_new_now (tz);
    now_string = g_date_time_format (now, "%Y-%m-%d %H%M");

    device_name = g_string_new (udisks_block_dup_preferred_device (self->block));
    g_string_replace (device_name, "/dev/", "", 1);
    g_string_replace (device_name, "/", "_", 0);

    /* If it's an ISO/UDF filesystem, suggest a filename ending in .iso */
    fstype = udisks_block_get_id_type (self->block);
    fslabel = udisks_block_get_id_label (self->block);
    if ((g_strcmp0 (fstype, "iso9660") == 0 || g_strcmp0 (fstype, "udf") == 0) && fslabel != NULL
        && strlen (fslabel) > 0) {
        proposed_filename = g_strdup_printf ("%s.iso", fslabel);
    } else {
        /* Translators: The suggested name for the disk image to create.
         *              The first %s is a name for the disk (e.g. 'sdb').
         *              The second %s is today's date and time, e.g. "March 2, 1976 6:25AM".
         */
        proposed_filename =
            g_strdup_printf (_("Disk Image of %s (%s).img"), g_string_free_and_steal (device_name), now_string);
    }

    gtk_editable_set_text (GTK_EDITABLE (self->name_entry), proposed_filename);
}

static void
gdu_create_disk_image_set_source_label (GduCreateDiskImageDialog *self)
{
    g_autoptr(UDisksObjectInfo) info = NULL;

    info = udisks_client_get_object_info (self->client, self->object);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (self->source_label), udisks_object_info_get_one_liner (info));
}

static void
gdu_create_disk_image_dialog_finalize (GObject *object)
{
    GduCreateDiskImageDialog *self = GDU_CREATE_DISK_IMAGE_DIALOG (object);

    g_clear_object (&self->object);
    g_clear_object (&self->block);
    g_clear_object (&self->drive);
    g_clear_object (&self->directory);

    G_OBJECT_CLASS (gdu_create_disk_image_dialog_parent_class)->finalize (object);
}

void
gdu_create_disk_image_dialog_class_init (GduCreateDiskImageDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = gdu_create_disk_image_dialog_finalize;

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/DiskUtility/ui/"
                                                               "gdu-create-disk-image-dialog.ui");

    gtk_widget_class_bind_template_child (widget_class, GduCreateDiskImageDialog, name_entry);
    gtk_widget_class_bind_template_child (widget_class, GduCreateDiskImageDialog, location_entry);
    gtk_widget_class_bind_template_child (widget_class, GduCreateDiskImageDialog, source_label);

    gtk_widget_class_bind_template_callback (widget_class, on_choose_folder_button_clicked_cb);
    gtk_widget_class_bind_template_callback (widget_class, on_create_image_button_clicked_cb);
}

void
gdu_create_disk_image_dialog_init (GduCreateDiskImageDialog *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    self->directory = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
}

void
gdu_create_disk_image_dialog_show (GtkWindow *parent_window, UDisksObject *object, UDisksClient *client)
{
    GduCreateDiskImageDialog *self;

    self = g_object_new (GDU_TYPE_CREATE_DISK_IMAGE_DIALOG, NULL);

    self->client = client;
    self->object = g_object_ref (object);
    self->block = udisks_object_get_block (object);
    g_assert (self->block != NULL);
    self->drive = udisks_client_get_drive_for_block (client, self->block);

    gdu_create_disk_image_set_source_label (self);
    gdu_create_disk_image_dialog_set_default_name (self);
    gdu_create_disk_image_dialog_update_directory (self);

    // gtk4 todo
    // gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (self->folder_fcbutton),
    //                                                   FALSE,   /* set file types */
    //                                                   FALSE);  /* allow_compressed */

    adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
