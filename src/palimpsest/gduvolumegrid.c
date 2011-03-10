/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <stdlib.h>

#include "gduvolumegrid.h"

/* ---------------------------------------------------------------------------------------------------- */

#define ELEMENT_MINIMUM_WIDTH 60

typedef enum
{
  GRID_EDGE_NONE    = 0,
  GRID_EDGE_TOP    = (1<<0),
  GRID_EDGE_BOTTOM = (1<<1),
  GRID_EDGE_LEFT   = (1<<2),
  GRID_EDGE_RIGHT  = (1<<3)
} GridEdgeFlags;

typedef struct GridElement GridElement;

struct GridElement
{
  GduVolumeGridElementType type;

  /* these values are set in recompute_grid() */
  gdouble size_ratio;
  GDBusObjectProxy *object_proxy;
  guint64 offset;
  guint64 size;

  GList *embedded_elements;
  GridElement *parent;
  GridElement *prev;
  GridElement *next;

  /* these values are set in recompute_size() */
  guint x;
  guint y;
  guint width;
  guint height;
  GridEdgeFlags edge_flags;

  gchar *text;

  /* used for the job spinner */
  guint spinner_current;
};

static void
grid_element_free (GridElement *element)
{
  if (element->object_proxy != NULL)
    g_object_unref (element->object_proxy);
  g_free (element->text);
  g_list_foreach (element->embedded_elements, (GFunc) grid_element_free, NULL);
  g_list_free (element->embedded_elements);

  g_free (element);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct _GduVolumeGridClass GduVolumeGridClass;
struct _GduVolumeGrid
{
  GtkDrawingArea parent;

  UDisksClient *client;
  GDBusObjectProxy *block_device;

  GList *elements;

  GridElement *selected;
  GridElement *focused;

  guint animation_timeout_id;
};

struct _GduVolumeGridClass
{
  GtkDrawingAreaClass parent_class;

  /* signals */
  void (*changed) (GduVolumeGrid *grid);
};

enum
{
  PROP_0,
  PROP_CLIENT,
  PROP_BLOCK_DEVICE
};

enum
{
  CHANGED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

static void on_object_proxy_added (GDBusProxyManager   *manager,
                                   GDBusObjectProxy    *object_proxy,
                                   gpointer             user_data);

static void on_object_proxy_removed (GDBusProxyManager   *manager,
                                     GDBusObjectProxy    *object_proxy,
                                     gpointer             user_data);

static void on_interface_proxy_added (GDBusProxyManager   *manager,
                                      GDBusObjectProxy    *object_proxy,
                                      GDBusProxy          *interface_proxy,
                                      gpointer             user_data);

static void on_interface_proxy_removed (GDBusProxyManager   *manager,
                                        GDBusObjectProxy    *object_proxy,
                                        GDBusProxy          *interface_proxy,
                                        gpointer             user_data);

static void on_interface_proxy_properties_changed (GDBusProxyManager   *manager,
                                                   GDBusObjectProxy    *object_proxy,
                                                   GDBusProxy          *interface_proxy,
                                                   GVariant            *changed_properties,
                                                   const gchar *const *invalidated_properties,
                                                   gpointer            user_data);

G_DEFINE_TYPE (GduVolumeGrid, gdu_volume_grid, GTK_TYPE_DRAWING_AREA)

static guint get_depth (GList *elements);

static guint get_num_elements_for_slice (GList *elements);

static void recompute_grid (GduVolumeGrid *grid);

static void recompute_size (GduVolumeGrid *grid,
                            guint          width,
                            guint          height);

static GridElement *find_element_for_position (GduVolumeGrid *grid,
                                               guint x,
                                               guint y);

static gboolean gdu_volume_grid_draw (GtkWidget *widget,
                                      cairo_t   *cr);

static void
gdu_volume_grid_finalize (GObject *object)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (object);
  GDBusProxyManager *proxy_manager;

  proxy_manager = udisks_client_get_proxy_manager (grid->client);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_object_proxy_added),
                                        grid);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_object_proxy_removed),
                                        grid);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_added),
                                        grid);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_removed),
                                        grid);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_properties_changed),
                                        grid);

  g_list_foreach (grid->elements, (GFunc) grid_element_free, NULL);
  g_list_free (grid->elements);

  if (grid->animation_timeout_id > 0)
    {
      g_source_remove (grid->animation_timeout_id);
      grid->animation_timeout_id = 0;
    }

  if (grid->block_device != NULL)
    g_object_unref (grid->block_device);
  g_object_unref (grid->client);

  G_OBJECT_CLASS (gdu_volume_grid_parent_class)->finalize (object);
}

static void
gdu_volume_grid_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (object);

  switch (property_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, grid->client);
      break;

    case PROP_BLOCK_DEVICE:
      g_value_set_object (value, grid->block_device);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_volume_grid_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (object);

  switch (property_id)
    {
    case PROP_CLIENT:
      grid->client = g_value_dup_object (value);
      break;

    case PROP_BLOCK_DEVICE:
      gdu_volume_grid_set_block_device (grid, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_volume_grid_constructed (GObject *object)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (object);
  GDBusProxyManager *proxy_manager;

  proxy_manager = udisks_client_get_proxy_manager (grid->client);
  g_signal_connect (proxy_manager,
                    "object-proxy-added",
                    G_CALLBACK (on_object_proxy_added),
                    grid);
  g_signal_connect (proxy_manager,
                    "object-proxy-removed",
                    G_CALLBACK (on_object_proxy_removed),
                    grid);
  g_signal_connect (proxy_manager,
                    "interface-proxy-added",
                    G_CALLBACK (on_interface_proxy_added),
                    grid);
  g_signal_connect (proxy_manager,
                    "interface-proxy-removed",
                    G_CALLBACK (on_interface_proxy_removed),
                    grid);
  g_signal_connect (proxy_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
                    grid);

  recompute_grid (grid);

  /* select the first element */
  if (grid->elements != NULL)
    {
      GridElement *element = grid->elements->data;
      grid->selected = element;
      grid->focused = element;
    }

  if (G_OBJECT_CLASS (gdu_volume_grid_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_volume_grid_parent_class)->constructed (object);
}

static gboolean
gdu_volume_grid_key_press_event (GtkWidget      *widget,
                                 GdkEventKey    *event)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (widget);
  gboolean handled;
  GridElement *target;

  handled = FALSE;

  if (event->type != GDK_KEY_PRESS)
    goto out;

  switch (event->keyval) {
  case GDK_KEY_Left:
  case GDK_KEY_Right:
  case GDK_KEY_Up:
  case GDK_KEY_Down:
    target = NULL;

    if (grid->focused == NULL)
      {
        g_warning ("TODO: handle nothing being selected/focused");
      }
    else
      {
        GridElement *element;

        element = grid->focused;
        if (element != NULL)
          {
            if (event->keyval == GDK_KEY_Left)
              {
                if (element->prev != NULL)
                  {
                    target = element->prev;
                  }
                else
                  {
                    if (element->parent && element->parent->prev != NULL)
                      target = element->parent->prev;
                  }
              }
            else if (event->keyval == GDK_KEY_Right)
              {
                if (element->next != NULL)
                  {
                    target = element->next;
                  }
                else
                  {
                    if (element->parent && element->parent->next != NULL)
                      target = element->parent->next;
                  }
              }
            else if (event->keyval == GDK_KEY_Up)
              {
                if (element->parent != NULL)
                  {
                    target = element->parent;
                  }
              }
            else if (event->keyval == GDK_KEY_Down)
              {
                if (element->embedded_elements != NULL)
                  {
                    target = (GridElement *) element->embedded_elements->data;
                  }
              }
          }
      }

    if (target != NULL)
      {
        if ((event->state & GDK_CONTROL_MASK) != 0)
          {
            grid->focused = target;
          }
        else
          {
            grid->selected = target;
            grid->focused = target;
            g_signal_emit (grid,
                           signals[CHANGED_SIGNAL],
                           0);
          }
        gtk_widget_queue_draw (GTK_WIDGET (grid));
      }
    handled = TRUE;
    break;

  case GDK_KEY_Return:
  case GDK_KEY_space:
    if (grid->focused != grid->selected &&
        grid->focused != NULL)
      {
        grid->selected = grid->focused;
        g_signal_emit (grid,
                       signals[CHANGED_SIGNAL],
                       0);
        gtk_widget_queue_draw (GTK_WIDGET (grid));
      }
    handled = TRUE;
    break;

  default:
    break;
  }

 out:
  return handled;
}

static gboolean
gdu_volume_grid_button_press_event (GtkWidget      *widget,
                                    GdkEventButton *event)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (widget);
  gboolean handled;

  handled = FALSE;

  if (event->type != GDK_BUTTON_PRESS)
    goto out;

  if (event->button == 1)
    {
      GridElement *element;

      element = find_element_for_position (grid, event->x, event->y);
      if (element != NULL)
        {
          grid->selected = element;
          grid->focused = element;
          g_signal_emit (grid,
                         signals[CHANGED_SIGNAL],
                         0);
          gtk_widget_grab_focus (GTK_WIDGET (grid));
          gtk_widget_queue_draw (GTK_WIDGET (grid));
        }
      handled = TRUE;
    }

 out:
  return handled;
}

static void
gdu_volume_grid_realize (GtkWidget *widget)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (widget);
  GdkWindow *window;
  GdkWindowAttr attributes;
  gint attributes_mask;
  GtkAllocation allocation;
  GtkStyleContext *context;

  gtk_widget_set_realized (widget, TRUE);
  gtk_widget_get_allocation (widget, &allocation);

  attributes.x = allocation.x;
  attributes.y = allocation.y;
  attributes.width = allocation.width;
  attributes.height = allocation.height;
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

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  window = gtk_widget_get_parent_window (widget);
  gtk_widget_set_window (widget, window);
  g_object_ref (window);

  window = gdk_window_new (gtk_widget_get_parent_window (widget),
                           &attributes,
                           attributes_mask);
  gtk_widget_set_window (widget, window);
  gdk_window_set_user_data (window, grid);

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_background (context, window);
}

static guint
get_num_elements_for_slice (GList *elements)
{
  GList *l;
  guint num_elements;

  num_elements = 0;
  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *element = l->data;
      num_elements += get_num_elements_for_slice (element->embedded_elements);
    }

  if (num_elements > 0)
    return num_elements;
  else
    return 1;
}

static void
gdu_volume_grid_get_preferred_width (GtkWidget *widget,
                                     gint      *minimal_width,
                                     gint      *natural_width)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (widget);
  guint num_elements;
  gint width;

  num_elements = get_num_elements_for_slice (grid->elements);
  width = num_elements * ELEMENT_MINIMUM_WIDTH;

  *minimal_width = *natural_width = width;
}

static void
gdu_volume_grid_get_preferred_height (GtkWidget *widget,
                                      gint      *minimal_height,
                                      gint      *natural_height)
{
  *minimal_height = *natural_height = 100;
}

static void
gdu_volume_grid_class_init (GduVolumeGridClass *klass)
{
  GObjectClass *gobject_class;
  GtkWidgetClass *gtkwidget_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = gdu_volume_grid_get_property;
  gobject_class->set_property = gdu_volume_grid_set_property;
  gobject_class->constructed  = gdu_volume_grid_constructed;
  gobject_class->finalize     = gdu_volume_grid_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->realize              = gdu_volume_grid_realize;
  gtkwidget_class->key_press_event      = gdu_volume_grid_key_press_event;
  gtkwidget_class->button_press_event   = gdu_volume_grid_button_press_event;
  gtkwidget_class->get_preferred_width  = gdu_volume_grid_get_preferred_width;
  gtkwidget_class->get_preferred_height = gdu_volume_grid_get_preferred_height;
  gtkwidget_class->draw                 = gdu_volume_grid_draw;

  g_object_class_install_property (gobject_class,
                                   PROP_CLIENT,
                                   g_param_spec_object ("client",
                                                        "Client",
                                                        "The UDisksClient to use",
                                                        UDISKS_TYPE_CLIENT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_BLOCK_DEVICE,
                                   g_param_spec_object ("block-device",
                                                        "Block Device",
                                                        "The top-level block device to show a grid for",
                                                        G_TYPE_DBUS_OBJECT_PROXY,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));

  signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                          GDU_TYPE_VOLUME_GRID,
                                          G_SIGNAL_RUN_LAST,
                                          G_STRUCT_OFFSET (GduVolumeGridClass, changed),
                                          NULL,
                                          NULL,
                                          g_cclosure_marshal_VOID__VOID,
                                          G_TYPE_NONE,
                                          0);
}

static void
gdu_volume_grid_init (GduVolumeGrid *grid)
{
  gtk_widget_set_can_focus (GTK_WIDGET (grid), TRUE);
}

GtkWidget *
gdu_volume_grid_new (UDisksClient *client)
{
  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  return GTK_WIDGET (g_object_new (GDU_TYPE_VOLUME_GRID,
                                   "client", client,
                                   NULL));
}

void
gdu_volume_grid_set_block_device (GduVolumeGrid     *grid,
                                  GDBusObjectProxy  *block_device)
{
  g_return_if_fail (GDU_IS_VOLUME_GRID (grid));

  if (block_device == grid->block_device)
    goto out;

  if (grid->block_device != NULL)
    g_object_unref (grid->block_device);
  grid->block_device = block_device != NULL ? g_object_ref (block_device) : NULL;

  recompute_grid (grid);

  /* select the first element */
  if (grid->elements != NULL)
    {
      GridElement *element = grid->elements->data;
      grid->selected = element;
      grid->focused = element;
    }

  g_object_notify (G_OBJECT (grid), "block-device");
 out:
  ;
}


static guint
get_depth (GList *elements)
{
  guint depth;
  GList *l;

  depth = 0;
  if (elements == NULL)
    goto out;

  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *ee = l->data;
      guint ee_depth;

      ee_depth = get_depth (ee->embedded_elements) + 1;
      if (ee_depth > depth)
        depth = ee_depth;
    }

 out:
  return depth;
}

static void
recompute_size_for_slice (GList          *elements,
                          guint           width,
                          guint           height,
                          guint           total_width,
                          guint           total_height,
                          guint           offset_x,
                          guint           offset_y)
{
  GList *l;
  gint x;
  gint pixels_left;
  guint num_elements;

  /* first steal all the allocated minimum width - then distribute remaining pixels
   * based on the size_ratio and add the allocated minimum width.
   */
  num_elements = get_num_elements_for_slice (elements);
  width -= num_elements * ELEMENT_MINIMUM_WIDTH;
  g_warn_if_fail (width >= 0);

  x = 0;
  pixels_left = width;
  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *element = l->data;
      gint element_width;
      gboolean is_last;
      guint element_depth;

      is_last  = (l->next == NULL);

      element_depth = get_depth (element->embedded_elements);
      //g_debug ("element_depth = %d (x,y)=(%d,%d) height=%d", element_depth, offset_x, offset_y, height);

      if (is_last)
        {
          element_width = pixels_left;
          pixels_left = 0;
        }
      else
        {
          element_width = element->size_ratio * width;
          if (element_width > pixels_left)
            element_width = pixels_left;
          pixels_left -= element_width;
        }

      num_elements = get_num_elements_for_slice (element->embedded_elements);
      element_width += num_elements * ELEMENT_MINIMUM_WIDTH;

      element->x = x + offset_x;
      element->y = offset_y;
      element->width = element_width;
      if (element_depth > 0)
        {
          element->height = height / (element_depth + 1);
        }
      else
        {
          element->height = height;
        }

      if (element->x == 0)
        element->edge_flags |= GRID_EDGE_LEFT;
      if (element->y == 0)
        element->edge_flags |= GRID_EDGE_TOP;
      if (element->x + element->width == total_width)
        element->edge_flags |= GRID_EDGE_RIGHT;
      if (element->y + element->height == total_height)
        element->edge_flags |= GRID_EDGE_BOTTOM;

      x += element_width;

      recompute_size_for_slice (element->embedded_elements,
                                element->width,
                                height - element->height,
                                total_width,
                                total_height,
                                element->x,
                                element->height + element->y);
    }
}

static void
recompute_size (GduVolumeGrid *grid,
                guint          width,
                guint          height)
{
  recompute_size_for_slice (grid->elements,
                            width,
                            height,
                            width,
                            height,
                            0,
                            0);
}

static void
render_spinner (cairo_t   *cr,
                guint      size,
                guint      num_lines,
                guint      current,
                gdouble    x,
                gdouble    y)
{
  guint n;
  gdouble radius;
  gdouble cx;
  gdouble cy;
  gdouble half;

  cx = x + size/2.0;
  cy = y + size/2.0;
  radius = size/2.0;
  half = num_lines / 2;

  current = current % num_lines;

  for (n = 0; n < num_lines; n++)
    {
      gdouble inset;
      gdouble t;

      inset = 0.7 * radius;

      /* transparency is a function of time and intial value */
      t = (gdouble) ((n + num_lines - current) % num_lines) / num_lines;

      cairo_set_source_rgba (cr, 0, 0, 0, t);
      cairo_set_line_width (cr, 2.0);
      cairo_move_to (cr,
                     cx + (radius - inset) * cos (n * M_PI / half),
                     cy + (radius - inset) * sin (n * M_PI / half));
      cairo_line_to (cr,
                     cx + radius * cos (n * M_PI / half),
                     cy + radius * sin (n * M_PI / half));
      cairo_stroke (cr);
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
round_rect (cairo_t *cr,
            gdouble x, gdouble y,
            gdouble w, gdouble h,
            gdouble r,
            GridEdgeFlags edge_flags)
{
  gboolean top_left_round;
  gboolean top_right_round;
  gboolean bottom_right_round;
  gboolean bottom_left_round;

  top_left_round     = ((edge_flags & GRID_EDGE_TOP)    && (edge_flags & GRID_EDGE_LEFT));
  top_right_round    = ((edge_flags & GRID_EDGE_TOP)    && (edge_flags & GRID_EDGE_RIGHT));
  bottom_right_round = ((edge_flags & GRID_EDGE_BOTTOM) && (edge_flags & GRID_EDGE_RIGHT));
  bottom_left_round  = ((edge_flags & GRID_EDGE_BOTTOM) && (edge_flags & GRID_EDGE_LEFT));

  if (top_left_round)
    {
      cairo_move_to  (cr,
                      x + r, y);
    }
  else
    {
      cairo_move_to  (cr,
                      x, y);
    }

  if (top_right_round)
    {
      cairo_line_to  (cr,
                      x + w - r, y);
      cairo_curve_to (cr,
                      x + w, y,
                      x + w, y,
                      x + w, y + r);
    }
  else
    {
      cairo_line_to  (cr,
                      x + w, y);
    }

  if (bottom_right_round)
    {
      cairo_line_to  (cr,
                      x + w, y + h - r);
      cairo_curve_to (cr,
                      x + w, y + h,
                      x + w, y + h,
                      x + w - r, y + h);
    }
  else
    {
      cairo_line_to  (cr,
                      x + w, y + h);
    }

  if (bottom_left_round)
    {
      cairo_line_to  (cr,
                      x + r, y + h);
      cairo_curve_to (cr,
                      x, y + h,
                      x, y + h,
                      x, y + h - r);
    }
  else
    {
      cairo_line_to  (cr,
                      x, y + h);
    }

  if (top_left_round)
    {
      cairo_line_to  (cr,
                      x, y + r);
      cairo_curve_to (cr,
                      x, y,
                      x, y,
                      x + r, y);
    }
  else
    {
      cairo_line_to  (cr,
                      x, y);
    }
}

/* returns true if an animation timeout is needed */
static gboolean
render_element (GduVolumeGrid *grid,
                cairo_t       *cr,
                GridElement   *element,
                gboolean       is_selected,
                gboolean       is_focused,
                gboolean       is_grid_focused)
{
  gboolean need_animation_timeout;
  gdouble fill_red;
  gdouble fill_green;
  gdouble fill_blue;
  gdouble fill_selected_red;
  gdouble fill_selected_green;
  gdouble fill_selected_blue;
  gdouble fill_selected_not_focused_red;
  gdouble fill_selected_not_focused_green;
  gdouble fill_selected_not_focused_blue;
  gdouble focus_rect_red;
  gdouble focus_rect_green;
  gdouble focus_rect_blue;
  gdouble focus_rect_selected_red;
  gdouble focus_rect_selected_green;
  gdouble focus_rect_selected_blue;
  gdouble focus_rect_selected_not_focused_red;
  gdouble focus_rect_selected_not_focused_green;
  gdouble focus_rect_selected_not_focused_blue;
  gdouble stroke_red;
  gdouble stroke_green;
  gdouble stroke_blue;
  gdouble stroke_selected_red;
  gdouble stroke_selected_green;
  gdouble stroke_selected_blue;
  gdouble stroke_selected_not_focused_red;
  gdouble stroke_selected_not_focused_green;
  gdouble stroke_selected_not_focused_blue;
  gdouble text_red;
  gdouble text_green;
  gdouble text_blue;
  gdouble text_selected_red;
  gdouble text_selected_green;
  gdouble text_selected_blue;
  gdouble text_selected_not_focused_red;
  gdouble text_selected_not_focused_green;
  gdouble text_selected_not_focused_blue;
  PangoLayout *layout;
  PangoFontDescription *desc;
  gint width, height;

  need_animation_timeout = FALSE;

  /* TODO: use GtkStyleContext and/or CSS etc. instead of hard-coding colors */
  fill_red     = 1;
  fill_green   = 1;
  fill_blue    = 1;
  fill_selected_red     = 0.40;
  fill_selected_green   = 0.60;
  fill_selected_blue    = 0.80;
  fill_selected_not_focused_red     = 0.60;
  fill_selected_not_focused_green   = 0.60;
  fill_selected_not_focused_blue    = 0.60;
  focus_rect_red     = 0.75;
  focus_rect_green   = 0.75;
  focus_rect_blue    = 0.75;
  focus_rect_selected_red     = 0.70;
  focus_rect_selected_green   = 0.70;
  focus_rect_selected_blue    = 0.80;
  focus_rect_selected_not_focused_red     = 0.70;
  focus_rect_selected_not_focused_green   = 0.70;
  focus_rect_selected_not_focused_blue    = 0.70;
  stroke_red   = 0.75;
  stroke_green = 0.75;
  stroke_blue  = 0.75;
  stroke_selected_red   = 0.3;
  stroke_selected_green = 0.45;
  stroke_selected_blue  = 0.6;
  stroke_selected_not_focused_red   = 0.45;
  stroke_selected_not_focused_green = 0.45;
  stroke_selected_not_focused_blue  = 0.45;
  text_red     = 0;
  text_green   = 0;
  text_blue    = 0;
  text_selected_red     = 1;
  text_selected_green   = 1;
  text_selected_blue    = 1;
  text_selected_not_focused_red     = 1;
  text_selected_not_focused_green   = 1;
  text_selected_not_focused_blue    = 1;

#if 0
  g_debug ("rendering element: x=%d w=%d",
           element->x,
           element->width);
#endif

  cairo_save (cr);
  cairo_rectangle (cr,
                   element->x + 0.5,
                   element->y + 0.5,
                   element->width,
                   element->height);
  cairo_clip (cr);

  round_rect (cr,
              element->x + 0.5,
              element->y + 0.5,
              element->width,
              element->height,
              10,
              element->edge_flags);

  if (is_selected)
    {
      cairo_pattern_t *gradient;
      gradient = cairo_pattern_create_radial (element->x + element->width / 2,
                                              element->y + element->height / 2,
                                              0.0,
                                              element->x + element->width / 2,
                                              element->y + element->height / 2,
                                              element->width/2.0);
      if (is_grid_focused)
        {
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
        }
      else
        {
          cairo_pattern_add_color_stop_rgb (gradient,
                                            0.0,
                                            1.0 * fill_selected_not_focused_red,
                                            1.0 * fill_selected_not_focused_green,
                                            1.0 * fill_selected_not_focused_blue);
          cairo_pattern_add_color_stop_rgb (gradient,
                                            1.0,
                                            0.8 * fill_selected_not_focused_red,
                                            0.8 * fill_selected_not_focused_green,
                                            0.8 * fill_selected_not_focused_blue);
        }
      cairo_set_source (cr, gradient);
      cairo_pattern_destroy (gradient);
    }
  else
    {
      cairo_set_source_rgb (cr,
                            fill_red,
                            fill_green,
                            fill_blue);
    }
  cairo_fill_preserve (cr);
  if (is_selected)
    {
      if (is_grid_focused)
        {
          cairo_set_source_rgb (cr,
                                stroke_selected_red,
                                stroke_selected_green,
                                stroke_selected_blue);
        }
      else
        {
          cairo_set_source_rgb (cr,
                                stroke_selected_not_focused_red,
                                stroke_selected_not_focused_green,
                                stroke_selected_not_focused_blue);
        }
    }
  else
    {
      cairo_set_source_rgb (cr,
                            stroke_red,
                            stroke_green,
                            stroke_blue);
    }
  cairo_set_dash (cr, NULL, 0, 0.0);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  /* focus indicator */
  if (is_focused && is_grid_focused)
    {
      gdouble dashes[] = {2.0};
      round_rect (cr,
                  element->x + 0.5 + 3,
                  element->y + 0.5 + 3,
                  element->width - 3 * 2,
                  element->height - 3 * 2,
                  20,
                  element->edge_flags);
      cairo_set_source_rgb (cr, focus_rect_red, focus_rect_green, focus_rect_blue);
      cairo_set_dash (cr, dashes, 1, 0.0);
      cairo_set_line_width (cr, 1.0);
      cairo_stroke (cr);
    }

  if (is_selected)
    {
      if (is_grid_focused)
        {
          cairo_set_source_rgb (cr,
                                text_selected_red,
                                text_selected_green,
                                text_selected_blue);
        }
      else
        {
          cairo_set_source_rgb (cr,
                                text_selected_not_focused_red,
                                text_selected_not_focused_green,
                                text_selected_not_focused_blue);
        }
    }
  else
    {
      cairo_set_source_rgb (cr, text_red, text_green, text_blue);
    }
  layout = pango_cairo_create_layout (cr);
  pango_layout_set_text (layout, element->text != NULL ? element->text : "", -1);
  desc = pango_font_description_from_string ("Sans 7.0");
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);
  pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
  pango_layout_set_width (layout, pango_units_from_double (element->width));
  pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
  pango_layout_get_size (layout, &width, &height);
  cairo_move_to (cr,
                 ceil(element->x),
                 ceil (element->y + element->height / 2 - pango_units_to_double (height) / 2));
  pango_cairo_show_layout (cr, layout);
  g_object_unref (layout);

  gint icon_offset;
  gboolean render_padlock_closed;
  gboolean render_padlock_open;
  gboolean render_job_in_progress;
  GPtrArray *pixbufs_to_render;
  guint n;

  icon_offset = 0;
  render_padlock_closed = FALSE;
  render_padlock_open = FALSE;
  render_job_in_progress = FALSE;

  if (render_job_in_progress)
    {
      render_spinner (cr,
                      16,
                      12,
                      element->spinner_current,
                      ceil (element->x + element->width - 16 - icon_offset - 4),
                      ceil (element->y + element->height - 16 - 4));

      icon_offset += 16 + 2; /* padding */

      element->spinner_current += 1;

      need_animation_timeout = TRUE;
    }

  /* icons */
  pixbufs_to_render = g_ptr_array_new_with_free_func (g_object_unref);
  if (render_padlock_open)
    g_ptr_array_add (pixbufs_to_render,
                     gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                               "gdu-encrypted-unlock",
                                               16, 0, NULL));
  if (render_padlock_closed)
    g_ptr_array_add (pixbufs_to_render,
                     gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                               "gdu-encrypted-lock",
                                               16, 0, NULL));
  for (n = 0; n < pixbufs_to_render->len; n++)
    {
      GdkPixbuf *pixbuf = GDK_PIXBUF (pixbufs_to_render->pdata[n]);
      guint icon_width;
      guint icon_height;

      icon_width = gdk_pixbuf_get_width (pixbuf);
      icon_height = gdk_pixbuf_get_height (pixbuf);

      render_pixbuf (cr,
                     ceil (element->x + element->width - icon_width - icon_offset - 4),
                     ceil (element->y + element->height - icon_height - 4),
                     pixbuf);

      icon_offset += icon_width + 2; /* padding */
    }
  g_ptr_array_free (pixbufs_to_render, TRUE);

  cairo_restore (cr);

  return need_animation_timeout;
}

static gboolean
on_animation_timeout (gpointer data)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (data);

  gtk_widget_queue_draw (GTK_WIDGET (grid));

  return TRUE; /* keep timeout around */
}

static gboolean
render_slice (GduVolumeGrid *grid,
              cairo_t       *cr,
              GList         *elements)
{
  GList *l;
  gboolean need_animation_timeout;

  need_animation_timeout = FALSE;
  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *element = l->data;
      gboolean is_selected;
      gboolean is_focused;
      gboolean is_grid_focused;

      is_selected = FALSE;
      is_focused = FALSE;
      is_grid_focused = gtk_widget_has_focus (GTK_WIDGET (grid));

      if (element == grid->selected)
        is_selected = TRUE;

      if (element == grid->focused)
        {
          if (grid->focused != grid->selected && is_grid_focused)
            is_focused = TRUE;
        }

      need_animation_timeout |= render_element (grid,
                                                cr,
                                                element,
                                                is_selected,
                                                is_focused,
                                                is_grid_focused);

      need_animation_timeout |= render_slice (grid,
                                              cr,
                                              element->embedded_elements);
    }

  return need_animation_timeout;
}

static gboolean
gdu_volume_grid_draw (GtkWidget *widget,
                      cairo_t   *cr)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (widget);
  GtkAllocation allocation;
  gdouble width;
  gdouble height;
  gboolean need_animation_timeout;

  gtk_widget_get_allocation (widget, &allocation);
  width = allocation.width;
  height = allocation.height;

  recompute_size (grid,
                  width - 1,
                  height -1);

  need_animation_timeout = render_slice (grid, cr, grid->elements);

  if (need_animation_timeout)
    {
      if (grid->animation_timeout_id == 0)
        {
          grid->animation_timeout_id = g_timeout_add (80,
                                                      on_animation_timeout,
                                                      grid);
        }
    }
  else
    {
      if (grid->animation_timeout_id > 0)
        {
          g_source_remove (grid->animation_timeout_id);
          grid->animation_timeout_id = 0;
        }
    }

  return FALSE;
}

static GridElement *
do_find_element_for_position (GList *elements,
                              guint  x,
                              guint  y)
{
  GList *l;
  GridElement *ret;

  ret = NULL;

  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *e = l->data;

      if ((x >= e->x) &&
          (x  < e->x + e->width) &&
          (y >= e->y) &&
          (y  < e->y + e->height))
        {
          ret = e;
          goto out;
        }

      ret = do_find_element_for_position (e->embedded_elements, x, y);
      if (ret != NULL)
        goto out;
    }

 out:
  return ret;
}

static GridElement *
find_element_for_position (GduVolumeGrid *grid,
                           guint x,
                           guint y)
{
  return do_find_element_for_position (grid->elements, x, y);
}

static GridElement *
do_find_element_for_offset_and_object_proxy (GList *elements,
                                             guint64 offset,
                                             GDBusObjectProxy *object_proxy)
{
  GList *l;
  GridElement *ret;

  ret = NULL;

  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *e = l->data;

      if (e->offset == offset && e->object_proxy == object_proxy)
        {
          ret = e;
          goto out;
        }

      ret = do_find_element_for_offset_and_object_proxy (e->embedded_elements, offset, object_proxy);
      if (ret != NULL)
        goto out;
    }

 out:
  return ret;
}

static GridElement *
find_element_for_offset_and_object_proxy (GduVolumeGrid    *grid,
                                          guint64           offset,
                                          GDBusObjectProxy *object_proxy)
{
  return do_find_element_for_offset_and_object_proxy (grid->elements, offset, object_proxy);
}

static gint
partition_sort_by_offset_func (GDBusObjectProxy *a,
                               GDBusObjectProxy *b)
{
  guint64 oa;
  guint64 ob;
  oa = udisks_block_device_get_part_entry_offset (UDISKS_PEEK_BLOCK_DEVICE (a));
  ob = udisks_block_device_get_part_entry_offset (UDISKS_PEEK_BLOCK_DEVICE (b));
  if (oa > ob)
    return 1;
  else if (oa < ob)
    return -1;
  else
    return 0;
}

static void grid_element_set_details (GduVolumeGrid  *grid,
                                      GridElement    *element);

static GList *
recompute_grid_add_partitions (GduVolumeGrid    *grid,
                               guint64           total_size,
                               GridElement      *parent,
                               guint64           free_space_slack,
                               guint64           top_offset,
                               guint64           top_size,
                               GList            *partitions,
                               GDBusObjectProxy *extended_partition,
                               GList            *logical_partitions)
{
  guint64 prev_end;
  GridElement *element;
  GridElement *prev_element;
  GList *l;
  GList *ret;

  ret = NULL;

  /* Partitioned... first handle primary partitions, adding free space as needed */
  partitions = g_list_sort (partitions, (GCompareFunc) partition_sort_by_offset_func);
  prev_end = top_offset;
  prev_element = NULL;
  for (l = partitions; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksBlockDevice *block;
      guint64 begin, end, size;

      block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
      begin = udisks_block_device_get_part_entry_offset (block);
      size = udisks_block_device_get_part_entry_size (block);
      end = begin + size;

      if (begin - prev_end > free_space_slack)
        {
          element = g_new0 (GridElement, 1);
          element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE;
          element->parent = parent;
          element->size_ratio = ((gdouble) (begin - prev_end)) / top_size;
          element->prev = prev_element;
          element->offset = prev_end;
          element->size = begin - prev_end;
          if (prev_element != NULL)
            prev_element->next = element;
          ret = g_list_append (ret, element);
          prev_element = element;
          grid_element_set_details (grid, element);
        }

      element = g_new0 (GridElement, 1);
      element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE;
      element->parent = parent;
      element->size_ratio = ((gdouble) udisks_block_device_get_part_entry_size (block)) / top_size;
      element->object_proxy = g_object_ref (object_proxy);
      element->offset = begin;
      element->size = size;
      element->prev = prev_element;
      if (prev_element != NULL)
        prev_element->next = element;
      ret = g_list_append (ret, element);
      prev_element = element;
      prev_end = end;
      grid_element_set_details (grid, element);

      if (object_proxy == extended_partition)
        {
          element->embedded_elements = recompute_grid_add_partitions (grid,
                                                                      total_size,
                                                                      element,
                                                                      free_space_slack,
                                                                      udisks_block_device_get_part_entry_offset (block),
                                                                      udisks_block_device_get_part_entry_size (block),
                                                                      logical_partitions,
                                                                      NULL,
                                                                      NULL);
        }
    }
  if (top_size + top_offset - prev_end > free_space_slack)
    {
      element = g_new0 (GridElement, 1);
      element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE;
      element->parent = parent;
      element->size_ratio = ((gdouble) (top_size - prev_end)) / top_size;
      element->prev = prev_element;
      element->offset = prev_end;
      element->size = top_size + top_offset - prev_end;
      if (prev_element != NULL)
        prev_element->next = element;
      ret = g_list_append (ret, element);
      prev_element = element;
      grid_element_set_details (grid, element);
    }

  return ret;
}

static void
recompute_grid (GduVolumeGrid *grid)
{
  GList *partitions;
  GList *logical_partitions;
  GDBusObjectProxy *extended_partition;
  GList *object_proxies;
  GDBusProxyManager *proxy_manager;
  GList *l;
  const gchar *top_object_proxy_path;
  UDisksBlockDevice *top_block;
  guint64 top_size;
  guint64 free_space_slack;
  GridElement *element;
  guint64 cur_selected_offset;
  guint64 cur_focused_offset;
  GDBusObjectProxy *cur_selected_object_proxy;
  GDBusObjectProxy *cur_focused_object_proxy;

  cur_selected_offset = 0;
  cur_selected_object_proxy = NULL;
  if (grid->selected != NULL)
    {
      cur_selected_offset = grid->selected->offset;
      cur_selected_object_proxy = grid->selected->object_proxy;
    }
  cur_focused_offset = 0;
  cur_focused_object_proxy = NULL;
  if (grid->focused != NULL)
    {
      cur_focused_offset = grid->focused->offset;
      cur_focused_object_proxy = grid->focused->object_proxy;
    }

  /* delete all old elements */
  g_list_foreach (grid->elements, (GFunc) grid_element_free, NULL);
  g_list_free (grid->elements);
  grid->elements = NULL;

#if 0
  g_debug ("TODO: recompute grid for %s",
           grid->block_device != NULL ?
           g_dbus_object_proxy_get_object_path (grid->block_device) : "<nothing selected>");
#endif

  if (grid->block_device == NULL)
    {
      element = g_new0 (GridElement, 1);
      element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA;
      element->size_ratio = 1.0;
      element->offset = 0;
      element->size = 0;
      grid->elements = g_list_append (grid->elements, element);
      grid_element_set_details (grid, element);
      goto out;
    }

  top_object_proxy_path = g_dbus_object_proxy_get_object_path (grid->block_device);
  top_block = UDISKS_PEEK_BLOCK_DEVICE (grid->block_device);
  top_size = udisks_block_device_get_size (top_block);

  /* include "Free Space" elements if there is at least this much slack between
   * partitions (currently 1% of the disk, at least 1MB)
   */
  free_space_slack = MAX (top_size / 100, 1000*1000);

  partitions = NULL;
  logical_partitions = NULL;
  extended_partition = NULL;
  proxy_manager = udisks_client_get_proxy_manager (grid->client);
  object_proxies = g_dbus_proxy_manager_get_all (proxy_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksBlockDevice *block;
      gboolean is_logical;

      block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
      if (block != NULL &&
          g_strcmp0 (udisks_block_device_get_part_entry_table (block),
                     top_object_proxy_path) == 0)
        {
          is_logical = FALSE;
          if (g_strcmp0 (udisks_block_device_get_part_entry_scheme (block), "mbr") == 0)
            {
              if (udisks_block_device_get_part_entry_number (block) >= 5)
                {
                  is_logical = TRUE;
                }
              else
                {
                  gint type;
                  type = strtol (udisks_block_device_get_part_entry_type (block), NULL, 0);
                  if (type == 0x05 || type == 0x0f || type == 0x85)
                    {
                      g_warn_if_fail (extended_partition == NULL);
                      extended_partition = object_proxy;
                    }
                }
            }

          if (is_logical)
            logical_partitions = g_list_prepend (logical_partitions, object_proxy);
          else
            partitions = g_list_prepend (partitions, object_proxy);
        }
    }

  if (partitions == NULL && !udisks_block_device_get_part_table (top_block))
    {
      /* No partitions and whole-disk has no partition table signature... */
      if (top_size == 0)
        {
          element = g_new0 (GridElement, 1);
          element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA;
          element->size_ratio = 1.0;
          element->offset = 0;
          element->size = 0;
          grid->elements = g_list_append (grid->elements, element);
          grid_element_set_details (grid, element);
        }
      else
        {
          element = g_new0 (GridElement, 1);
          element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE;
          element->size_ratio = 1.0;
          element->offset = 0;
          element->size = top_size;
          element->object_proxy = g_object_ref (grid->block_device);
          grid->elements = g_list_append (grid->elements, element);
          grid_element_set_details (grid, element);
        }
    }
  else
    {
      grid->elements = recompute_grid_add_partitions (grid,
                                                      top_size,
                                                      NULL,
                                                      free_space_slack,
                                                      0,
                                                      top_size,
                                                      partitions,
                                                      extended_partition,
                                                      logical_partitions);
    }

  g_list_free (logical_partitions);
  g_list_free (partitions);
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);

 out:

  /* reselect focused and selected elements */
  grid->selected = find_element_for_offset_and_object_proxy (grid, cur_selected_offset, cur_selected_object_proxy);
  grid->focused = find_element_for_offset_and_object_proxy (grid, cur_focused_offset, cur_focused_object_proxy);

  /* ensure something is always focused/selected */
  if (grid->selected == NULL)
    grid->selected = grid->elements->data;
  if (grid->focused == NULL)
    grid->focused = grid->elements->data;

  /* queue a redraw */
  gtk_widget_queue_draw (GTK_WIDGET (grid));
}

/* ---------------------------------------------------------------------------------------------------- */

GduVolumeGridElementType
gdu_volume_grid_get_selected_type (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), 0);
  return grid->selected->type;
}

GDBusObjectProxy *
gdu_volume_grid_get_selected_device (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), NULL);
  return grid->selected->object_proxy;
}

guint64
gdu_volume_grid_get_selected_offset (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), 0);
  return grid->selected->offset;
}

guint64
gdu_volume_grid_get_selected_size (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), 0);
  return grid->selected->size;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
grid_element_set_details (GduVolumeGrid  *grid,
                          GridElement    *element)
{
  switch (element->type)
    {
    case GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA:
      element->text = g_strdup (_("No Media"));
      break;

    case GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE:
      {
        gchar *size_str;
        size_str = udisks_util_get_size_for_display (element->size, FALSE, FALSE);
        /* Translators: This is shown in the volume grid - the first %s is the amount of free space */
        element->text = g_strdup_printf (_("%s Free Space"), size_str);
        g_free (size_str);
      }
      break;

    case GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE:
      {
        UDisksBlockDevice *block;
        gchar *s;
        gchar *size_str;
        const gchar *usage;
        const gchar *type;

        size_str = udisks_util_get_size_for_display (element->size, FALSE, FALSE);
        block = UDISKS_PEEK_BLOCK_DEVICE (element->object_proxy);

        usage = udisks_block_device_get_id_usage (block);
        type = udisks_block_device_get_id_type (block);

        if (g_strcmp0 (usage, "filesystem") == 0)
          {
            const gchar *label;
            label = udisks_block_device_get_id_label (block);
            if (strlen (label) == 0)
              label = C_("volume-grid", "Filesystem");
            s = g_strdup_printf ("%s\n%s %s", label, size_str, type);
          }
        else if (g_strcmp0 (usage, "other") == 0 && g_strcmp0 (type, "swap") == 0)
          {
            const gchar *label;
            label = udisks_block_device_get_id_label (block);
            if (strlen (label) == 0)
              s = g_strdup_printf ("%s\n%s",
                                   C_("volume-grid", "Swap"),
                                   size_str);
            else
              s = g_strdup_printf ("%s\n%s %s", label, size_str,
                                   C_("volume-grid", "Swap"));
          }
        else if (g_strcmp0 (usage, "crypto") == 0)
          {
            s = g_strdup_printf ("%s\n%s",
                                 C_("volume-grid", "Encrypted"),
                                 size_str);
            /* TODO: emblems for locked/unlocked */
          }
        else
          {
            s = g_strdup_printf (_("%s %s"), size_str,
                                 C_("volume-grid", "Unknown"));
          }
        element->text = s;
        g_free (size_str);
      }
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
maybe_update (GduVolumeGrid    *grid,
              GDBusObjectProxy *object_proxy)
{
  UDisksBlockDevice *block;

  //g_debug ("in maybe_update %s", g_dbus_object_proxy_get_object_path (object_proxy));

  if (grid->block_device == NULL)
    goto out;

  block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
  if (block == NULL)
    goto out;

  if (object_proxy == grid->block_device ||
      g_strcmp0 (udisks_block_device_get_part_entry_table (block),
                 g_dbus_object_proxy_get_object_path (grid->block_device)) == 0)
    {
      /* object_proxy is either the block device we're a grid for or a partition of it */
      recompute_grid (grid);
    }

 out:
  ;
}

static void
on_object_proxy_added (GDBusProxyManager   *manager,
                       GDBusObjectProxy    *object_proxy,
                       gpointer             user_data)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (user_data);
  maybe_update (grid, object_proxy);
}

static void
on_object_proxy_removed (GDBusProxyManager   *manager,
                         GDBusObjectProxy    *object_proxy,
                         gpointer             user_data)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (user_data);
  maybe_update (grid, object_proxy);
}

static void
on_interface_proxy_added (GDBusProxyManager   *manager,
                          GDBusObjectProxy    *object_proxy,
                          GDBusProxy          *interface_proxy,
                          gpointer             user_data)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (user_data);
  maybe_update (grid, object_proxy);
}

static void
on_interface_proxy_removed (GDBusProxyManager   *manager,
                            GDBusObjectProxy    *object_proxy,
                            GDBusProxy          *interface_proxy,
                            gpointer             user_data)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (user_data);
  maybe_update (grid, object_proxy);
}

static void
on_interface_proxy_properties_changed (GDBusProxyManager   *manager,
                                       GDBusObjectProxy    *object_proxy,
                                       GDBusProxy          *interface_proxy,
                                       GVariant            *changed_properties,
                                       const gchar *const *invalidated_properties,
                                       gpointer            user_data)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (user_data);
  maybe_update (grid, object_proxy);
}
