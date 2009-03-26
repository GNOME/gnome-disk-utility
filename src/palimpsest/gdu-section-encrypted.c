/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-encrypted.c
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
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>

#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>

#include "gdu-section-encrypted.h"

struct _GduSectionEncryptedPrivate
{
        GtkWidget *encrypted_change_passphrase_button;
        GtkWidget *encrypted_forget_passphrase_button;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionEncrypted, gdu_section_encrypted, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

typedef struct {
        char *old_secret;
        char *new_secret;
        gboolean save_in_keyring;
        gboolean save_in_keyring_session;
        GduPresentable *presentable;
        GduSectionEncrypted *section;
} ChangePassphraseData;

static void
change_passphrase_data_free (ChangePassphraseData *data)
{
        /* scrub the secrets */
        if (data->old_secret != NULL) {
                memset (data->old_secret, '\0', strlen (data->old_secret));
                g_free (data->old_secret);
        }
        if (data->new_secret != NULL) {
                memset (data->new_secret, '\0', strlen (data->new_secret));
                g_free (data->new_secret);
        }
        if (data->presentable != NULL)
                g_object_unref (data->presentable);
        if (data->section != NULL)
                g_object_unref (data->section);
        g_free (data);
}

static void change_passphrase_do (GduSectionEncrypted *section,
                                  GduPresentable      *presentable,
                                  gboolean             bypass_keyring,
                                  gboolean             indicate_wrong_passphrase);

static void
update (GduSectionEncrypted *section)
{
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is NULL", __FUNCTION__);
                goto out;
        }

        gtk_widget_set_sensitive (section->priv->encrypted_forget_passphrase_button, gdu_util_have_secret (device));

        gtk_widget_set_sensitive (section->priv->encrypted_change_passphrase_button,
                                  !gdu_device_is_read_only (device));

out:
        if (device != NULL)
                g_object_unref (device);
}

static gboolean
change_passphrase_retry (gpointer user_data)
{
        ChangePassphraseData *data = user_data;

        /* It didn't work. Because the given passphrase was wrong. Try again,
         * this time forcibly bypassing the keyring and telling the user
         * the given passphrase was wrong.
         */
        change_passphrase_do (data->section, data->presentable, TRUE, TRUE);
        change_passphrase_data_free (data);
        return FALSE;
}

static void
change_passphrase_completed (GduDevice  *device,
                             GError     *error,
                             gpointer    user_data)
{
        ChangePassphraseData *data = user_data;

        if (error == NULL) {
                /* It worked! Now update the keyring */

                if (data->save_in_keyring || data->save_in_keyring_session)
                        gdu_util_save_secret (device, data->new_secret, data->save_in_keyring_session);
                else
                        gdu_util_delete_secret (device);

                update (data->section);
                change_passphrase_data_free (data);
        } else {
                /* retry in idle so the job-spinner can be hidden */
                g_idle_add (change_passphrase_retry, data);
        }
}

static void
change_passphrase_do (GduSectionEncrypted *section,
                      GduPresentable      *presentable,
                      gboolean             bypass_keyring,
                      gboolean             indicate_wrong_passphrase)
{
        GduDevice *device;
        ChangePassphraseData *data;

        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                goto out;
        }

        data = g_new0 (ChangePassphraseData, 1);
        data->presentable = g_object_ref (presentable);
        data->section = g_object_ref (section);

        if (!gdu_util_dialog_change_secret (gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section))),
                                            presentable,
                                            &data->old_secret,
                                            &data->new_secret,
                                            &data->save_in_keyring,
                                            &data->save_in_keyring_session,
                                            bypass_keyring,
                                            indicate_wrong_passphrase)) {
                change_passphrase_data_free (data);
                goto out;
        }

        gdu_device_op_luks_change_passphrase (device,
                                              data->old_secret,
                                              data->new_secret,
                                              change_passphrase_completed,
                                              data);

out:
        if (device != NULL) {
                g_object_unref (device);
        }
}

static void
change_passphrase_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduSectionEncrypted *section = GDU_SECTION_ENCRYPTED (user_data);
        change_passphrase_do (section, gdu_section_get_presentable (GDU_SECTION (section)), FALSE, FALSE);
}

static void
forget_passphrase_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduSectionEncrypted *section = GDU_SECTION_ENCRYPTED (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL)
                goto out;

        gdu_util_delete_secret (device);
        update (section);

out:
        if (device != NULL)
                g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_encrypted_finalize (GduSectionEncrypted *section)
{
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_encrypted_class_init (GduSectionEncryptedClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_encrypted_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionEncryptedPrivate));
}

static void
gdu_section_encrypted_init (GduSectionEncrypted *section)
{
        GtkWidget *vbox2;
        GtkWidget *button;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *button_box;
        char *s;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_ENCRYPTED, GduSectionEncryptedPrivate);

        label = gtk_label_new (NULL);
        s = g_strconcat ("<b>", _("Encryption"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 6);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The volume contains encrypted data that can be unlocked "
                                                   "with a passphrase. The passphrase can optionally be stored "
                                                   "in the keyring."));
        gtk_label_set_width_chars (GTK_LABEL (label), 50);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        /* change passphrase button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = gtk_button_new_with_mnemonic (_("Change Pa_ssphrase..."));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (change_passphrase_button_clicked), section);
        section->priv->encrypted_change_passphrase_button = button;

        button = gtk_button_new_with_mnemonic (_("F_orget Passphrase"));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (forget_passphrase_button_clicked), section);
        section->priv->encrypted_forget_passphrase_button = button;

}

GtkWidget *
gdu_section_encrypted_new (GduShell       *shell,
                           GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_ENCRYPTED,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
