/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-main.c
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
#include <glib-object.h>
#include <string.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-shell.h"

static gboolean
show_nag_dialog (GtkWidget *toplevel)
{
        GtkWidget *dialog;
        gint response;
        gboolean ret;

        ret = TRUE;

        dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (toplevel),
                                                     GTK_DIALOG_MODAL,
                                                     GTK_MESSAGE_WARNING,
                                                     GTK_BUTTONS_OK,
                                                     _("<b><big>WARNING WARNING WARNING</big></b>"));
        gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                                    _("The Palimpsest Disk Utility is still under development and "
                                                      "may still have bugs that can lead to data loss.\n"
                                                      "\n"
                                                      "Use at your own risk."));
        response = gtk_dialog_run (GTK_DIALOG (dialog));

        if (response != GTK_RESPONSE_OK)
                ret = FALSE;

        gtk_widget_destroy (dialog);

        return ret;
}

int
main (int argc, char **argv)
{
        GduShell *shell;

        gtk_init (&argc, &argv);

        gtk_window_set_default_icon_name ("palimpsest");

        shell = gdu_shell_new ();
        gtk_widget_show_all (gdu_shell_get_toplevel (shell));
        gdu_shell_update (shell);

        if (!show_nag_dialog (gdu_shell_get_toplevel (shell)))
                goto out;

        gtk_main ();

 out:
        g_object_unref (shell);
        return 0;
}
