#pragma once

#include <adwaita.h>

extern void
gdu_rs_restore_disk_image_dialog_show(GtkWindow *parent_window,
                                      const gchar *object_path,
                                      const gchar *disk_image_filename);

extern gboolean
gdu_rs_has_local_jobs ();

extern void
gdu_rs_local_jobs_clear ();
