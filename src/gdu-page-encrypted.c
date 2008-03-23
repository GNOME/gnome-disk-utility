/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-encrypted.c
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <glib-object.h>
#include <string.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-page.h"
#include "gdu-page-encrypted.h"
#include "gdu-util.h"

struct _GduPageEncryptedPrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;
        GtkWidget *main_label;
        GtkWidget *delete_password_button;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_encrypted_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPageEncrypted, gdu_page_encrypted, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_encrypted_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void
gdu_page_encrypted_finalize (GduPageEncrypted *page)
{
        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_encrypted_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
        GduPageEncrypted *page = GDU_PAGE_ENCRYPTED (object);

        switch (prop_id) {
        case PROP_SHELL:
                if (page->priv->shell != NULL)
                        g_object_unref (page->priv->shell);
                page->priv->shell = g_object_ref (g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_page_encrypted_get_property (GObject     *object,
                             guint        prop_id,
                             GValue      *value,
                             GParamSpec  *pspec)
{
        GduPageEncrypted *page = GDU_PAGE_ENCRYPTED (object);

        switch (prop_id) {
        case PROP_SHELL:
                g_value_set_object (value, page->priv->shell);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_page_encrypted_class_init (GduPageEncryptedClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_encrypted_finalize;
        obj_class->set_property = gdu_page_encrypted_set_property;
        obj_class->get_property = gdu_page_encrypted_get_property;

        /**
         * GduPageEncrypted:shell:
         *
         * The #GduShell instance hosting this page.
         */
        g_object_class_install_property (obj_class,
                                         PROP_SHELL,
                                         g_param_spec_object ("shell",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_SHELL,
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_READABLE));

}

typedef struct {
        char *old_secret;
        char *new_secret;
        gboolean save_in_keyring;
        gboolean save_in_keyring_session;
        GduPresentable *presentable;
        GduPageEncrypted *page;
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
        if (data->page != NULL)
                g_object_unref (data->page);
        g_free (data);
}

static void change_passphrase_do (GduPageEncrypted *page, GduPresentable *presentable, gboolean bypass_keyring);

static void
update_delete_state (GduPageEncrypted *page)
{
        if (gdu_util_have_secret (gdu_shell_get_selected_presentable (page->priv->shell))) {
                gtk_label_set_markup (GTK_LABEL (page->priv->main_label),
                                      _("Data on this device is stored in an encrypted form "
                                        "protected by a passphrase. The passphrase is "
                                        "stored in the keyring."));
                gtk_widget_set_sensitive (page->priv->delete_password_button, TRUE);
        } else {
                gtk_label_set_markup (GTK_LABEL (page->priv->main_label),
                                      _("Data on this device is stored in an encrypted form "
                                        "protected by a passphrase. The passphrase is not "
                                        "stored in the keyring."));
                gtk_widget_set_sensitive (page->priv->delete_password_button, FALSE);
        }
}

static void
change_passphrase_completed (GduDevice  *device,
                             gboolean    result,
                             GError     *error,
                             gpointer    user_data)
{
        ChangePassphraseData *data = user_data;

        if (result) {
                /* It worked! Now update the keyring */

                if (data->save_in_keyring || data->save_in_keyring_session)
                        gdu_util_save_secret (data->presentable, data->new_secret, data->save_in_keyring_session);
                else
                        gdu_util_delete_secret (data->presentable);

                update_delete_state (data->page);
        } else {
                /* It didn't work. Likely because the given password was wrong. Try again,
                 * this time forcibly bypassing the keyring.
                 */
                change_passphrase_do (data->page, data->presentable, TRUE);
        }
        change_passphrase_data_free (data);
}

static void
change_passphrase_do (GduPageEncrypted *page, GduPresentable *presentable, gboolean bypass_keyring)
{
        GduDevice *device;
        ChangePassphraseData *data;

        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                goto out;
        }

        data = g_new0 (ChangePassphraseData, 1);
        data->presentable = g_object_ref (presentable);
        data->page = g_object_ref (page);

        if (!gdu_util_dialog_change_secret (gdu_shell_get_toplevel (page->priv->shell),
                                            gdu_shell_get_selected_presentable (page->priv->shell),
                                            &data->old_secret,
                                            &data->new_secret,
                                            &data->save_in_keyring,
                                            &data->save_in_keyring_session,
                                            bypass_keyring)) {
                change_passphrase_data_free (data);
                goto out;
        }

        gdu_device_op_change_secret_for_encrypted (device,
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
        GduPageEncrypted *page = GDU_PAGE_ENCRYPTED (user_data);
        change_passphrase_do (page, gdu_shell_get_selected_presentable (page->priv->shell), FALSE);
}

static void
forget_passphrase_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduPageEncrypted *page = GDU_PAGE_ENCRYPTED (user_data);
        gdu_util_delete_secret (gdu_shell_get_selected_presentable (page->priv->shell));
        update_delete_state (page);
}

static void
gdu_page_encrypted_init (GduPageEncrypted *page)
{
        int row;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *vbox;
        GtkWidget *table;
        GtkWidget *combo_box;
        GtkWidget *entry;
        GtkWidget *button;
        GtkWidget *button_box;

        page->priv = g_new0 (GduPageEncryptedPrivate, 1);

        page->priv->main_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        /* volume format + label */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Passphrase</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), label, FALSE, FALSE, 0);
        vbox = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox);
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);
        page->priv->main_label = label;

        /* change passphrase button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, TRUE, TRUE, 0);
        button = gtk_button_new_with_mnemonic (_("Change Pa_ssphrase..."));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (change_passphrase_button_clicked), page);

        button = gtk_button_new_with_mnemonic (_("F_orget Passphrase"));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (forget_passphrase_button_clicked), page);
        page->priv->delete_password_button = button;
}


GduPageEncrypted *
gdu_page_encrypted_new (GduShell *shell)
{
        return GDU_PAGE_ENCRYPTED (g_object_new (GDU_TYPE_PAGE_ENCRYPTED, "shell", shell, NULL));
}


static gboolean
gdu_page_encrypted_update (GduPage *_page, GduPresentable *presentable)
{
        GduPageEncrypted *page = GDU_PAGE_ENCRYPTED (_page);
        gboolean ret;
        GduDevice *device;

        ret = FALSE;

        device = gdu_presentable_get_device (presentable);
        if (device == NULL)
                goto out;

        if (strcmp (gdu_device_id_get_usage (device), "crypto") != 0)
                goto out;

        update_delete_state (page);

        ret = TRUE;
out:
        if (device != NULL)
                g_object_unref (device);

        return ret;
}

static GtkWidget *
gdu_page_encrypted_get_widget (GduPage *_page)
{
        GduPageEncrypted *page = GDU_PAGE_ENCRYPTED (_page);
        return page->priv->main_vbox;
}

static char *
gdu_page_encrypted_get_name (GduPage *page)
{
        return g_strdup (_("En_crypted Device"));
}

static void
gdu_page_encrypted_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_encrypted_get_widget;
        iface->get_name = gdu_page_encrypted_get_name;
        iface->update = gdu_page_encrypted_update;
}
