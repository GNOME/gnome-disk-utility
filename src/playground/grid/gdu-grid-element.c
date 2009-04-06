/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#include <math.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include <gdu-gtk/gdu-gtk.h>

#include "gdu-grid-view.h"
#include "gdu-grid-element.h"

struct GduGridElementPrivate
{
        GduGridView *view;
        GduPresentable *presentable;
        GduDevice *device;
        guint minimum_size;
        gdouble percent_size;
        GduGridElementFlags flags;

        guint job_spinner_timeout_id;
        gdouble job_spinner_animation_step;
};

enum
{
        PROP_0,
        PROP_VIEW,
        PROP_PRESENTABLE,
        PROP_MINIMUM_SIZE,
        PROP_PERCENT_SIZE,
        PROP_FLAGS,
};

static void gdu_grid_element_presentable_changed (GduPresentable *presentable,
                                                  gpointer        user_data);
static void gdu_grid_element_presentable_job_changed (GduPresentable *presentable,
                                                      gpointer        user_data);

G_DEFINE_TYPE (GduGridElement, gdu_grid_element, GTK_TYPE_DRAWING_AREA)

static void
gdu_grid_element_finalize (GObject *object)
{
        GduGridElement *element = GDU_GRID_ELEMENT (object);

        if (element->priv->job_spinner_timeout_id > 0) {
                g_source_remove (element->priv->job_spinner_timeout_id);
        }

        if (element->priv->presentable != NULL) {
                g_signal_handlers_disconnect_by_func (element->priv->presentable,
                                                      gdu_grid_element_presentable_changed,
                                                      element);
                g_signal_handlers_disconnect_by_func (element->priv->presentable,
                                                      gdu_grid_element_presentable_job_changed,
                                                      element);
                g_object_unref (element->priv->presentable);
        }

        if (G_OBJECT_CLASS (gdu_grid_element_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_grid_element_parent_class)->finalize (object);
}

static void
gdu_grid_element_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GduGridElement *element = GDU_GRID_ELEMENT (object);

        switch (property_id) {
        case PROP_VIEW:
                g_value_set_object (value, element->priv->view);
                break;

        case PROP_PRESENTABLE:
                g_value_set_object (value, element->priv->presentable);
                break;

        case PROP_MINIMUM_SIZE:
                g_value_set_uint (value, element->priv->minimum_size);
                break;

        case PROP_PERCENT_SIZE:
                g_value_set_double (value, element->priv->percent_size);
                break;

        case PROP_FLAGS:
                g_value_set_uint (value, element->priv->flags);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_grid_element_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
        GduGridElement *element = GDU_GRID_ELEMENT (object);

        switch (property_id) {
        case PROP_VIEW:
                /* don't increase reference count */
                element->priv->view = g_value_get_object (value);
                break;

        case PROP_PRESENTABLE:
                element->priv->presentable = g_value_dup_object (value);
                break;

        case PROP_MINIMUM_SIZE:
                element->priv->minimum_size = g_value_get_uint (value);
                break;

        case PROP_PERCENT_SIZE:
                element->priv->percent_size = g_value_get_double (value);
                break;

        case PROP_FLAGS:
                element->priv->flags = g_value_get_uint (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
round_rect (cairo_t *cr,
            gdouble x, gdouble y,
            gdouble w, gdouble h,
            gdouble r,
            gboolean top_left_round,
            gboolean top_right_round,
            gboolean bottom_right_round,
            gboolean bottom_left_round)
{
        if (top_left_round) {
                cairo_move_to  (cr,
                                x + r, y);
        } else {
                cairo_move_to  (cr,
                                x, y);
        }

        if (top_right_round) {
                cairo_line_to  (cr,
                                x + w - r, y);
                cairo_curve_to (cr,
                                x + w, y,
                                x + w, y,
                                x + w, y + r);
        } else {
                cairo_line_to  (cr,
                                x + w, y);
        }

        if (bottom_right_round) {
                cairo_line_to  (cr,
                                x + w, y + h - r);
                cairo_curve_to (cr,
                                x + w, y + h,
                                x + w, y + h,
                                x + w - r, y + h);
        } else {
                cairo_line_to  (cr,
                                x + w, y + h);
        }

        if (bottom_left_round) {
                cairo_line_to  (cr,
                                x + r, y + h);
                cairo_curve_to (cr,
                                x, y + h,
                                x, y + h,
                                x, y + h - r);
        } else {
                cairo_line_to  (cr,
                                x, y + h);
        }

        if (top_left_round) {
                cairo_line_to  (cr,
                                x, y + r);
                cairo_curve_to (cr,
                                x, y,
                                x, y,
                                x + r, y);
        } else {
                cairo_line_to  (cr,
                                x, y);
        }
}

static void
render_pixbuf (cairo_t   *cr,
               gdouble    x,
               gdouble    y,
               GdkPixbuf *pixbuf)
{
        gdk_cairo_set_source_pixbuf (cr, pixbuf, x, y);
        cairo_rectangle (cr,
                         x,
                         y,
                         gdk_pixbuf_get_width (pixbuf),
                         gdk_pixbuf_get_height (pixbuf));
        cairo_fill (cr);
}

static void
render_spinner (cairo_t *cr,
                gdouble  x,
                gdouble  y,
                gdouble  radius,
                gdouble  anim_step)
{
        guint n;
        gdouble angle;
        gdouble inner_radius;
        guint num_circles;

        num_circles = 8;
        inner_radius = radius / 4.0;

        for (n = 0, angle = 0; n < num_circles; n++, angle += 2 * M_PI / num_circles) {
                gdouble color;
                gdouble this_anim_step;

                this_anim_step = anim_step + ((gdouble) n) / num_circles;
                if (this_anim_step > 1.0)
                        this_anim_step -= 1.0;
                if (this_anim_step < 0.5)
                        this_anim_step = 2.0 * (0.5 - this_anim_step);
                else
                        this_anim_step = 2.0 * (this_anim_step - 0.5);
                color = 0.7 + this_anim_step * 0.3;

                cairo_set_source_rgba (cr, 0, 0, 0, 1 - color);

                cairo_set_dash (cr, NULL, 0, 0.0);
                cairo_set_line_width (cr, 1.0);
                cairo_arc (cr,
                           x + (radius - inner_radius) * cos (angle),
                           y + (radius - inner_radius) * sin (angle),
                           inner_radius,
                           0,
                           2 * M_PI);
                cairo_fill (cr);
        }
}

static gboolean
gdu_grid_element_expose_event (GtkWidget           *widget,
                               GdkEventExpose      *event)
{
        GduGridElement *element = GDU_GRID_ELEMENT (widget);
        cairo_t *cr;
        gdouble width, height;
        gdouble rect_x, rect_y, rect_width, rect_height;
        gboolean is_selected;
        gboolean has_focus;
        GduGridElementFlags f;
        gdouble fill_red;
        gdouble fill_green;
        gdouble fill_blue;
        gdouble fill_selected_red;
        gdouble fill_selected_green;
        gdouble fill_selected_blue;
        gdouble focus_rect_red;
        gdouble focus_rect_green;
        gdouble focus_rect_blue;
        gdouble focus_rect_selected_red;
        gdouble focus_rect_selected_green;
        gdouble focus_rect_selected_blue;
        gdouble stroke_red;
        gdouble stroke_green;
        gdouble stroke_blue;
        gdouble stroke_selected_red;
        gdouble stroke_selected_green;
        gdouble stroke_selected_blue;
        gdouble text_red;
        gdouble text_green;
        gdouble text_blue;
        gdouble text_selected_red;
        gdouble text_selected_green;
        gdouble text_selected_blue;
        gdouble border_width;
        GduDevice *d;

        f = element->priv->flags;

        if (element->priv->presentable != NULL)
                d = gdu_presentable_get_device (element->priv->presentable);

        width = widget->allocation.width;
        height = widget->allocation.height;

        border_width = 4;

        rect_x = 0.5;
        rect_y = 0.5;
        rect_width = width;
        rect_height = height;
        if (f & GDU_GRID_ELEMENT_FLAGS_EDGE_LEFT) {
                rect_x += border_width;
                rect_width -= border_width;
        }
        if (f & GDU_GRID_ELEMENT_FLAGS_EDGE_TOP) {
                rect_y += border_width;
                rect_height -= border_width;
        }
        if (f & GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT) {
                rect_width -= border_width;
        }
        if (f & GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM) {
                rect_height -= border_width;
        }

        cr = gdk_cairo_create (widget->window);
        cairo_rectangle (cr,
                         event->area.x, event->area.y,
                         event->area.width, event->area.height);
        cairo_clip (cr);

        has_focus = GTK_WIDGET_HAS_FOCUS (widget);
        if (element->priv->presentable != NULL)
                is_selected = gdu_grid_view_is_selected (element->priv->view, element->priv->presentable);
        else
                is_selected = FALSE;

        fill_red     = 1;
        fill_green   = 1;
        fill_blue    = 1;
        fill_selected_red     = 0.40;
        fill_selected_green   = 0.60;
        fill_selected_blue    = 0.80;
        focus_rect_red     = 0.75;
        focus_rect_green   = 0.75;
        focus_rect_blue    = 0.75;
        focus_rect_selected_red     = 0.70;
        focus_rect_selected_green   = 0.70;
        focus_rect_selected_blue    = 0.80;
        stroke_red   = 0.75;
        stroke_green = 0.75;
        stroke_blue  = 0.75;
        stroke_selected_red   = 0.3;
        stroke_selected_green = 0.45;
        stroke_selected_blue  = 0.6;
        text_red     = 0;
        text_green   = 0;
        text_blue    = 0;
        text_selected_red     = 1;
        text_selected_green   = 1;
        text_selected_blue    = 1;

        /* draw element */
        if (is_selected) {
                cairo_pattern_t *gradient;
                gradient = cairo_pattern_create_radial (rect_x + rect_width / 2,
                                                        rect_y + rect_height / 2,
                                                        0.0,
                                                        rect_x + rect_width / 2,
                                                        rect_y + rect_height / 2,
                                                        rect_width/2.0);
                cairo_pattern_add_color_stop_rgb (gradient,
                                                  0.0,
                                                  1.0 * fill_selected_red,
                                                  1.0 * fill_selected_green,
                                                  1.0 * fill_selected_blue);
                cairo_pattern_add_color_stop_rgb (gradient,
                                                  1.0,
                                                  0.8 * fill_selected_red,
                                                  0.8 * fill_selected_green,
                                                  0.8 * fill_selected_blue);
                cairo_set_source (cr, gradient);
                cairo_pattern_destroy (gradient);
        } else {
                if (d != NULL && gdu_device_is_drive (d)) {
                        cairo_set_source_rgb (cr,
                                              fill_red,
                                              fill_green,
                                              fill_blue);
                } else {
                        if (element->priv->presentable != NULL &&
                            (gdu_presentable_is_allocated (element->priv->presentable) &&
                             gdu_presentable_is_recognized (element->priv->presentable))) {
                                cairo_set_source_rgb (cr,
                                                      fill_red,
                                                      fill_green,
                                                      fill_blue);
                        } else {
                                cairo_set_source_rgb (cr,
                                                      0.975 * fill_red,
                                                      0.975 * fill_green,
                                                      0.975 * fill_blue);
                        }
                }

        }
        f = element->priv->flags;
        round_rect (cr,
                    rect_x, rect_y,
                    rect_width, rect_height,
                    20,
                    (f & GDU_GRID_ELEMENT_FLAGS_EDGE_LEFT)  && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_TOP),
                    (f & GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT) && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_TOP),
                    (f & GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT) && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM),
                    (f & GDU_GRID_ELEMENT_FLAGS_EDGE_LEFT)  && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM));
        cairo_fill_preserve (cr);
        if (is_selected)
                cairo_set_source_rgb (cr, stroke_selected_red, stroke_selected_green, stroke_selected_blue);
        else
                cairo_set_source_rgb (cr, stroke_red, stroke_green, stroke_blue);
        cairo_set_line_width (cr, 1);
        cairo_stroke (cr);

        /* draw a spinner if one or more jobs are pending on the device */
        if (element->priv->job_spinner_timeout_id > 0) {
                render_spinner (cr,
                                ceil (rect_x + rect_width - 6 - 4),
                                ceil (rect_y + rect_height - 6 - 4),
                                6,
                                element->priv->job_spinner_animation_step);
                element->priv->job_spinner_animation_step += 0.1;
                if (element->priv->job_spinner_animation_step > 1.0)
                        element->priv->job_spinner_animation_step -= 1.0;
        }

        /* draw focus indicator */
        if (has_focus) {
                gdouble dashes[] = {2.0};
                round_rect (cr,
                            rect_x + 3, rect_y + 3,
                            rect_width - 3 * 2, rect_height - 3 * 2,
                            20,
                            (f & GDU_GRID_ELEMENT_FLAGS_EDGE_LEFT)  && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_TOP),
                            (f & GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT) && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_TOP),
                            (f & GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT) && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM),
                            (f & GDU_GRID_ELEMENT_FLAGS_EDGE_LEFT)  && (f & GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM));
                if (is_selected)
                        cairo_set_source_rgb (cr, focus_rect_selected_red, focus_rect_selected_green, focus_rect_selected_blue);
                else
                        cairo_set_source_rgb (cr, focus_rect_red, focus_rect_green, focus_rect_blue);
                cairo_set_dash (cr, dashes, 1, 0.0);
                cairo_set_line_width (cr, 1.0);
                cairo_stroke (cr);
        }

        /* adjust clip rect */
        cairo_rectangle (cr,
                         rect_x + 3, rect_y + 3,
                         rect_width - 3 * 2, rect_height - 3 * 2);
        cairo_clip (cr);

        /* draw icons/text */
        if (element->priv->presentable != NULL && GDU_IS_DRIVE (element->priv->presentable)) {
                GdkPixbuf *pixbuf;
                gint icon_width;
                cairo_text_extents_t te;
                gchar *s;
                gdouble y;
                gint line_height;

                y = 0;

                pixbuf = gdu_util_get_pixbuf_for_presentable (element->priv->presentable, GTK_ICON_SIZE_SMALL_TOOLBAR);
                icon_width = 0;
                if (pixbuf != NULL) {
                        icon_width = gdk_pixbuf_get_width (pixbuf);
                        render_pixbuf (cr,
                                       ceil (rect_x) + 4,
                                       ceil (rect_y) + 4,
                                       pixbuf);
                        g_object_unref (pixbuf);
                }

                if (is_selected)
                        cairo_set_source_rgb (cr, text_selected_red, text_selected_green, text_selected_blue);
                else
                        cairo_set_source_rgb (cr, text_red, text_green, text_blue);

                /* drive name */
                s = gdu_presentable_get_name (element->priv->presentable);
                cairo_select_font_face (cr,
                                        "sans",
                                        CAIRO_FONT_SLANT_NORMAL,
                                        CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr,
                               ceil (ceil (rect_x) + 4 + icon_width + 4 - te.x_bearing),
                               ceil (ceil (rect_y) + te.height - te.y_bearing));
                cairo_show_text (cr, s);
                g_free (s);
                line_height = te.height + 4;
                y += line_height;

                if (d != NULL) {
                        s = g_strdup (gdu_device_get_device_file (d));
                } else {
                        s = g_strdup (" ");
                }
                cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr, ceil (ceil (rect_x) + 4 + icon_width + 4 - te.x_bearing),
                               ceil (ceil (rect_y) + te.height - te.y_bearing + y));
                cairo_show_text (cr, s);
                g_free (s);
                y += line_height;

                //s = g_strdup ("foobar");
                //cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                //cairo_set_font_size (cr, 8.0);
                //cairo_text_extents (cr, s, &te);
                //cairo_move_to (cr, ceil (ceil (rect_x) + 4 - te.x_bearing),
                //               ceil (ceil (rect_y) + te.height - te.y_bearing + y));
                //cairo_show_text (cr, s);
                //g_free (s);
                //y += line_height;

        } else if (element->priv->presentable != NULL) {
                gchar *s;
                gchar *s1;
                cairo_text_extents_t te;
                cairo_text_extents_t te1;
                GduDevice *d;
                gdouble text_height;

                d = gdu_presentable_get_device (element->priv->presentable);

                s = NULL;
                s1 = NULL;
                if (d != NULL && g_strcmp0 (gdu_device_id_get_usage (d), "filesystem") == 0) {
                        gchar *fstype_str;
                        gchar *size_str;
                        s = g_strdup (gdu_device_id_get_label (d));
                        fstype_str = gdu_util_get_fstype_for_display (gdu_device_id_get_type (d),
                                                                      gdu_device_id_get_version (d),
                                                                      FALSE);
                        size_str = gdu_util_get_size_for_display (gdu_device_get_size (d), FALSE);
                        s1 = g_strdup_printf ("%s %s", size_str, fstype_str);
                        g_free (fstype_str);
                        g_free (size_str);
                } else if (d != NULL && gdu_device_is_partition (d) &&
                           (g_strcmp0 (gdu_device_partition_get_type (d), "0x05") == 0 ||
                            g_strcmp0 (gdu_device_partition_get_type (d), "0x0f") == 0 ||
                            g_strcmp0 (gdu_device_partition_get_type (d), "0x85") == 0)) {
                        s = g_strdup (_("Extended"));
                        s1 = gdu_util_get_size_for_display (gdu_presentable_get_size (element->priv->presentable), FALSE);
                } else if (d != NULL && g_strcmp0 (gdu_device_id_get_usage (d), "crypto") == 0) {
                        s = g_strdup (_("Encrypted"));
                        s1 = gdu_util_get_size_for_display (gdu_presentable_get_size (element->priv->presentable), FALSE);
                } else if (!gdu_presentable_is_allocated (element->priv->presentable)) {
                        s = g_strdup (_("Free"));
                        s1 = gdu_util_get_size_for_display (gdu_presentable_get_size (element->priv->presentable), FALSE);
                } else if (!gdu_presentable_is_recognized (element->priv->presentable)) {
                        s = g_strdup (_("Unknown"));
                        s1 = gdu_util_get_size_for_display (gdu_presentable_get_size (element->priv->presentable), FALSE);
                }

                if (s == NULL)
                        s = gdu_presentable_get_name (element->priv->presentable);
                if (s1 == NULL)
                        s1 = g_strdup ("");

                if (is_selected)
                        cairo_set_source_rgb (cr, text_selected_red, text_selected_green, text_selected_blue);
                else
                        cairo_set_source_rgb (cr, text_red, text_green, text_blue);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);

                cairo_text_extents (cr, s, &te);
                cairo_text_extents (cr, s1, &te1);

                text_height = te.height + te1.height;

                cairo_move_to (cr,
                               ceil (rect_x + rect_width / 2 - te.width/2  - te.x_bearing),
                               ceil (rect_y + rect_height / 2 - 2 - text_height/2 - te.y_bearing));
                cairo_show_text (cr, s);
                cairo_move_to (cr,
                               ceil (rect_x + rect_width / 2 - te1.width/2  - te1.x_bearing),
                               ceil (rect_y + rect_height / 2 + 2 - te1.y_bearing));
                cairo_show_text (cr, s1);
                g_free (s);
                g_free (s1);

                if (d != NULL)
                        g_object_unref (d);
        }

        if (d != NULL)
                g_object_unref (d);

        cairo_destroy (cr);

        return FALSE;
}

static gboolean
is_ctrl_pressed (void)
{
        gboolean ret;
        XkbStateRec state;
        Bool status;

        ret = FALSE;

        gdk_error_trap_push ();
        status = XkbGetState (GDK_DISPLAY (), XkbUseCoreKbd, &state);
        gdk_error_trap_pop ();

        if (status == Success) {
                ret = ((state.mods & ControlMask) != 0);
        }

        return ret;
}

static gboolean
gdu_grid_element_key_press_event (GtkWidget      *widget,
                                  GdkEventKey    *event)
{
        GduGridElement *element = GDU_GRID_ELEMENT (widget);
        gboolean handled;

        handled = FALSE;

        if (event->type != GDK_KEY_PRESS)
                goto out;

        if (event->keyval == GDK_space) {
                //g_debug ("Space pressed on %p - setting as selected", widget);
                if (!is_ctrl_pressed ()) {
                        gdu_grid_view_selection_clear (element->priv->view);
                        gdu_grid_view_selection_add (element->priv->view, element->priv->presentable);
                } else {
                        if (!gdu_grid_view_is_selected (element->priv->view, element->priv->presentable))
                                gdu_grid_view_selection_add (element->priv->view, element->priv->presentable);
                        else
                                gdu_grid_view_selection_remove (element->priv->view, element->priv->presentable);
                }
                gtk_widget_grab_focus (widget);
                handled = TRUE;
        }

 out:
        return handled;
}

static gboolean
gdu_grid_element_button_press_event (GtkWidget      *widget,
                                     GdkEventButton *event)
{
        GduGridElement *element = GDU_GRID_ELEMENT (widget);
        gboolean handled;

        handled = FALSE;

        if (event->type != GDK_BUTTON_PRESS)
                goto out;

        if (event->button == 1) {
                //g_debug ("Left button pressed on %p - setting as selected", widget);
                if (!is_ctrl_pressed ()) {
                        gdu_grid_view_selection_clear (element->priv->view);
                        gdu_grid_view_selection_add (element->priv->view, element->priv->presentable);
                } else {
                        if (!gdu_grid_view_is_selected (element->priv->view, element->priv->presentable))
                                gdu_grid_view_selection_add (element->priv->view, element->priv->presentable);
                        else
                                gdu_grid_view_selection_remove (element->priv->view, element->priv->presentable);
                }
                gtk_widget_grab_focus (widget);
                handled = TRUE;
        }

 out:
        return handled;
}

static gboolean
gdu_grid_element_focus (GtkWidget        *widget,
                        GtkDirectionType  direction)
{
        GduGridElement *element = GDU_GRID_ELEMENT (widget);
        gboolean handled;

        handled = GTK_WIDGET_CLASS (gdu_grid_element_parent_class)->focus (widget, direction);

        switch (direction) {
        case GTK_DIR_UP:
        case GTK_DIR_DOWN:
        case GTK_DIR_LEFT:
        case GTK_DIR_RIGHT:
                if (!is_ctrl_pressed ()) {
                        gdu_grid_view_selection_clear (element->priv->view);
                        gdu_grid_view_selection_add (element->priv->view, element->priv->presentable);
                }
                break;

        default:
                break;
        }

        return handled;
}

static void
gdu_grid_element_realize (GtkWidget *widget)
{
        GduGridElement *element = GDU_GRID_ELEMENT (widget);
        GdkWindowAttr attributes;
        gint attributes_mask;

        GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

        attributes.x = widget->allocation.x;
        attributes.y = widget->allocation.y;
        attributes.width = widget->allocation.width;
        attributes.height = widget->allocation.height;
        attributes.wclass = GDK_INPUT_OUTPUT;
        attributes.window_type = GDK_WINDOW_CHILD;
        attributes.event_mask = gtk_widget_get_events (widget) |
                GDK_KEY_PRESS_MASK |
                GDK_EXPOSURE_MASK |
                GDK_BUTTON_PRESS_MASK |
                GDK_BUTTON_RELEASE_MASK |
                GDK_ENTER_NOTIFY_MASK |
                GDK_LEAVE_NOTIFY_MASK;
        attributes.visual = gtk_widget_get_visual (widget);
        attributes.colormap = gtk_widget_get_colormap (widget);

        attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

        widget->window = gtk_widget_get_parent_window (widget);
        g_object_ref (widget->window);

        widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
                                         &attributes, attributes_mask);
        gdk_window_set_user_data (widget->window, element);

        widget->style = gtk_style_attach (widget->style, widget->window);

        gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
}

#if 0
static void
gdu_grid_element_unrealize (GtkWidget *widget)
{
        GduGridElement *element = GDU_GRID_ELEMENT (widget);

        if (element->priv->event_window != NULL) {
                gdk_window_set_user_data (element->priv->event_window, NULL);
                gdk_window_destroy (element->priv->event_window);
                element->priv->event_window = NULL;
        }

        GTK_WIDGET_CLASS (gdu_grid_element_parent_class)->unrealize (widget);
}
#endif

static gboolean
job_spinner_timeout_cb (gpointer user_data)
{
        GduGridElement *element = GDU_GRID_ELEMENT (user_data);
        gtk_widget_queue_draw (GTK_WIDGET (element));
        return TRUE;
}

static void
update_job_spinner (GduGridElement *element)
{
        GduDevice *d;

        d = NULL;
        if (element->priv->presentable == NULL)
                goto out;
        d = gdu_presentable_get_device (element->priv->presentable);
        if (d == NULL)
                goto out;

        if (gdu_device_job_in_progress (d)) {
                if (element->priv->job_spinner_timeout_id == 0) {
                        element->priv->job_spinner_timeout_id = g_timeout_add (100, job_spinner_timeout_cb, element);
                        element->priv->job_spinner_animation_step = 0.0;
                        gtk_widget_queue_draw (GTK_WIDGET (element));
                }
        } else {
                if (element->priv->job_spinner_timeout_id > 0) {
                        g_source_remove (element->priv->job_spinner_timeout_id);
                        element->priv->job_spinner_timeout_id = 0;
                        gtk_widget_queue_draw (GTK_WIDGET (element));
                }
        }

 out:
        if (d != NULL)
                g_object_unref (d);
}

static void
gdu_grid_element_presentable_changed (GduPresentable *presentable,
                                      gpointer        user_data)
{
        GduGridElement *element = GDU_GRID_ELEMENT (user_data);
        g_debug ("changed for %s", gdu_presentable_get_name (presentable));
        update_job_spinner (element);
}

static void
gdu_grid_element_presentable_job_changed (GduPresentable *presentable,
                                          gpointer        user_data)
{
        GduGridElement *element = GDU_GRID_ELEMENT (user_data);
        g_debug ("job changed for %s", gdu_presentable_get_name (presentable));
        update_job_spinner (element);
}

static void
gdu_grid_element_constructed (GObject *object)
{
        GduGridElement *element = GDU_GRID_ELEMENT (object);

        if (element->priv->presentable != NULL) {
                g_signal_connect (element->priv->presentable,
                                  "changed",
                                  G_CALLBACK (gdu_grid_element_presentable_changed),
                                  element);
                g_signal_connect (element->priv->presentable,
                                  "job-changed",
                                  G_CALLBACK (gdu_grid_element_presentable_job_changed),
                                  element);
        }

        update_job_spinner (element);

        if (G_OBJECT_CLASS (gdu_grid_element_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_grid_element_parent_class)->constructed (object);
}

static void
gdu_grid_element_class_init (GduGridElementClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduGridElementPrivate));

        object_class->get_property = gdu_grid_element_get_property;
        object_class->set_property = gdu_grid_element_set_property;
        object_class->constructed  = gdu_grid_element_constructed;
        object_class->finalize     = gdu_grid_element_finalize;

        widget_class->realize            = gdu_grid_element_realize;
        //widget_class->unrealize          = gdu_grid_element_unrealize;
        widget_class->expose_event       = gdu_grid_element_expose_event;
        widget_class->key_press_event    = gdu_grid_element_key_press_event;
        widget_class->button_press_event = gdu_grid_element_button_press_event;
        widget_class->focus              = gdu_grid_element_focus;

        g_object_class_install_property (object_class,
                                         PROP_VIEW,
                                         g_param_spec_object ("view",
                                                              _("View"),
                                                              _("The GduGridView object that the element is associated with"),
                                                              GDU_TYPE_GRID_VIEW,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_PRESENTABLE,
                                         g_param_spec_object ("presentable",
                                                              _("Presentable"),
                                                              _("The presentable shown or NULL if this is a element representing lack of media"),
                                                              GDU_TYPE_PRESENTABLE,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_MINIMUM_SIZE,
                                         g_param_spec_uint ("minimum-size",
                                                            _("Minimum Size"),
                                                            _("The mininum size of the element"),
                                                            0,
                                                            G_MAXUINT,
                                                            40,
                                                            G_PARAM_READABLE |
                                                            G_PARAM_WRITABLE |
                                                            G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_PERCENT_SIZE,
                                         g_param_spec_double ("percent-size",
                                                              _("Percent Size"),
                                                              _("The size in percent this element should claim or 0 to always claim the specified minimum size"),
                                                              0.0,
                                                              100.0,
                                                              0.0,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_FLAGS, /* TODO: proper type */
                                         g_param_spec_uint ("flags",
                                                            _("Flags"),
                                                            _("Flags for the element"),
                                                            0,
                                                            G_MAXUINT,
                                                            0,
                                                            G_PARAM_READABLE |
                                                            G_PARAM_WRITABLE |
                                                            G_PARAM_CONSTRUCT_ONLY));
}

static void
gdu_grid_element_init (GduGridElement *element)
{
        element->priv = G_TYPE_INSTANCE_GET_PRIVATE (element, GDU_TYPE_GRID_ELEMENT, GduGridElementPrivate);

        GTK_WIDGET_SET_FLAGS (element, GTK_CAN_FOCUS);
}

GtkWidget *
gdu_grid_element_new (GduGridView        *view,
                      GduPresentable     *presentable,
                      guint               minimum_size,
                      gdouble             percent_size,
                      GduGridElementFlags flags)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_GRID_ELEMENT,
                                         "view", view,
                                         "presentable", presentable,
                                         "minimum-size", minimum_size,
                                         "percent-size", percent_size,
                                         "flags", flags,
                                         NULL));
}

GduGridView *
gdu_grid_element_get_view (GduGridElement *element)
{
        return g_object_ref (element->priv->view);
}

GduPresentable *
gdu_grid_element_get_presentable (GduGridElement *element)
{
        return g_object_ref (element->priv->presentable);
}

guint
gdu_grid_element_get_minimum_size (GduGridElement *element)
{
        return element->priv->minimum_size;
}

gdouble
gdu_grid_element_get_percent_size (GduGridElement *element)
{
        return element->priv->percent_size;
}

GduGridElementFlags
gdu_grid_element_get_flags (GduGridElement *element)
{
        return element->priv->flags;
}

