/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_UTILS_H__
#define __GDU_UTILS_H__

#include "libgdutypes.h"

G_BEGIN_DECLS

gboolean gdu_utils_has_configuration (UDisksBlock  *block,
                                      const gchar  *type,
                                      gboolean     *out_has_passphrase);

gboolean gdu_utils_has_userspace_mount_option (UDisksBlock *block,
                                               const gchar *option);

void gdu_utils_configure_file_chooser_for_disk_images (GtkFileChooser *file_chooser,
                                                       gboolean        set_file_types,
                                                       gboolean        allow_compressed);

void gdu_utils_file_chooser_for_disk_images_set_default_folder (GFile *folder);

GtkWidget *gdu_utils_create_info_bar (GtkMessageType  message_type,
                                      const gchar    *markup,
                                      GtkWidget     **out_label);

gchar *gdu_utils_unfuse_path (const gchar *path);

void gdu_options_update_check_option (GtkWidget       *options_entry,
                                      const gchar     *option,
                                      GtkWidget       *widget,
                                      GtkWidget       *check_button,
                                      gboolean         negate,
                                      gboolean         add_to_front);

void gdu_options_update_entry_option (GtkWidget       *options_entry,
                                      const gchar     *option,
                                      GtkWidget       *widget,
                                      GtkWidget       *entry);

const gchar *gdu_utils_get_seat (void);

gchar *gdu_utils_format_duration_usec (guint64                usec,
                                       GduFormatDurationFlags flags);

gboolean gdu_utils_is_flash                  (UDisksDrive *drive);

guint gdu_utils_count_primary_dos_partitions (UDisksClient         *client,
                                              UDisksPartitionTable *table);

gboolean gdu_utils_have_dos_extended         (UDisksClient         *client,
                                              UDisksPartitionTable *table);

gboolean gdu_utils_is_inside_dos_extended    (UDisksClient         *client,
                                              UDisksPartitionTable *table,
                                              guint64               offset);

void            gdu_utils_show_error      (GtkWindow      *parent_window,
                                           const gchar    *message,
                                           GError         *error);

gboolean        gdu_utils_show_confirmation (GtkWindow    *parent_window,
                                             const gchar  *message,
                                             const gchar  *secondary_message,
                                             const gchar  *affirmative_verb,
                                             const gchar  *checkbox_mnemonic,
                                             gboolean     *inout_checkbox_value,
                                             UDisksClient *client,
                                             GList        *objects,
                                             gboolean     destructive_action);

gboolean gdu_utils_is_ntfs_available (UDisksClient *client);


/* Defined by libblockdev/UDisks */
typedef enum {
  OFFLINE_SHRINK = 1 << 1,
  OFFLINE_GROW = 1 << 2,
  ONLINE_SHRINK = 1 << 3,
  ONLINE_GROW = 1 << 4
} ResizeFlags;

gboolean gdu_utils_can_resize (UDisksClient *client,
                               const gchar  *fstype,
                               gboolean      flush,
                               ResizeFlags  *mode_out,
                               gchar       **missing_util_out);

gboolean gdu_utils_can_repair (UDisksClient *client,
                               const gchar  *fstype,
                               gboolean      flush,
                               gchar       **missing_util_out);

gboolean gdu_utils_can_take_ownership (const gchar  *fstype);

gboolean gdu_utils_can_check  (UDisksClient *client,
                               const gchar  *fstype,
                               gboolean      flush,
                               gchar       **missing_util_out);


guint gdu_utils_get_max_label_length (const gchar *fstype);

gboolean _gtk_entry_buffer_truncate_bytes (GtkEntryBuffer *gtk_entry_buffer,
                                           guint           max_bytes);

gboolean gdu_util_is_same_size (GList   *blocks,
                                guint64 *out_min_size);

gchar *gdu_utils_get_pretty_uri (GFile *file);

GList *gdu_utils_get_all_contained_objects (UDisksClient *client,
                                            UDisksObject *object);

gboolean gdu_utils_is_in_use (UDisksClient *client,
                              UDisksObject *object);

void gdu_utils_ensure_unused (UDisksClient         *client,
                              GtkWindow            *parent_window,
                              UDisksObject         *object,
                              GAsyncReadyCallback   callback,
                              GCancellable         *cancellable,
                              gpointer              user_data);
gboolean gdu_utils_ensure_unused_finish (UDisksClient  *client,
                                         GAsyncResult  *res,
                                         GError       **error);

void gdu_utils_ensure_unused_list (UDisksClient         *client,
                                   GtkWindow            *parent_window,
                                   GList                *objects,
                                   GAsyncReadyCallback   callback,
                                   GCancellable         *cancellable,
                                   gpointer              user_data);
gboolean gdu_utils_ensure_unused_list_finish (UDisksClient  *client,
                                              GAsyncResult  *res,
                                              GError       **error);

guint64 gdu_utils_calc_space_to_grow (UDisksClient *client,
                                      UDisksPartitionTable *table,
                                      UDisksPartition *partition);

guint64 gdu_utils_calc_space_to_shrink_extended (UDisksClient *client,
                                                 UDisksPartitionTable *table,
                                                 UDisksPartition *partition);

gint64 gdu_utils_get_unused_for_block (UDisksClient *client,
                                       UDisksBlock  *block);


#define NUM_UNITS 11

typedef enum {
  Byte,
  KByte,
  MByte,
  GByte,
  TByte,
  PByte,
  KiByte,
  MiByte,
  GiByte,
  TiByte,
  PiByte
} UnitSizeIndices;

/* Keep in sync with Glade file */
static const guint64 unit_sizes[NUM_UNITS] = {
  (1ULL),                /*  0: bytes */
  (1000ULL),             /*  1: kB */
  (1000000ULL),          /*  2: MB */
  (1000000000ULL),       /*  3: GB */
  (1000000000000ULL),    /*  4: TB */
  (1000000000000000ULL), /*  5: PB */
  ((1ULL)<<10),          /*  6: KiB */
  ((1ULL)<<20),          /*  7: MiB */
  ((1ULL)<<30),          /*  8: GiB */
  ((1ULL)<<40),          /*  9: TiB */
  ((1ULL)<<50),          /* 10: PiB */
};

gint gdu_utils_get_default_unit (guint64 size);

G_END_DECLS

#endif /* __GDU_UTILS_H__ */
