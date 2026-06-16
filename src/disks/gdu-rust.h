#pragma once

#include <adwaita.h>

/* gobject-linter-ignore-next-line: missing_implementation */
extern void gdu_rs_restore_disk_image_dialog_show (GtkWindow *parent_window, const gchar *object_path,
                                                   const gchar *disk_image_filename);

/* gobject-linter-ignore-next-line: missing_implementation */
extern gboolean gdu_rs_has_local_jobs (void);

/* gobject-linter-ignore-next-line: missing_implementation */
extern void gdu_rs_local_jobs_clear (void);
