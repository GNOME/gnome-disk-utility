/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  format-window-operation.h
 *
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Tomas Bzatek <tbzatek@redhat.com>
 *
 */

#ifndef FORMAT_WINDOW_OPERATION_H
#define FORMAT_WINDOW_OPERATION_H

#include <gtk/gtk.h>
#include <gdu/gdu.h>
#include "format-window.h"


G_BEGIN_DECLS

typedef struct
{
        FormatDialogPrivate *priv;
        gchar *encrypt_passphrase;
        gboolean save_in_keyring;
        gboolean save_in_keyring_session;
        gchar *fslabel;
        const gchar *fstype;
        GduDevice *device;
        GduPresentable *presentable;
        gboolean take_ownership;
        const gchar *recommended_part_type;
        const gchar *scheme;

        guint job_progress_pulse_timer_id;

        PolKitAction *pk_format_action;
        PolKitGnomeAction *format_action;
        PolKitAction *pk_part_modify_action;
        PolKitGnomeAction *part_modify_action;
        PolKitAction *pk_part_table_new_action;
        PolKitGnomeAction *part_table_new_action;
        PolKitAction *pk_part_new_action;
        PolKitGnomeAction *part_new_action;
} FormatProcessData;


/*  update UI controls when operation is in progress  */
void update_ui_progress  (FormatDialogPrivate *priv,
                          FormatProcessData   *data,
                          gboolean             working);

/*  start the format operation  */
void do_format           (FormatDialogPrivate *priv);


G_END_DECLS

#endif  /* FORMAT_WINDOW_OPERATION_H */

