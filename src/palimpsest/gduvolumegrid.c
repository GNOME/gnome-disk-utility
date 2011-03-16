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
  gint fixed_width;
  gdouble size_ratio;
  GDBusObject *object;
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

  gchar *markup;
  GIcon *icon;

  gboolean show_spinner;
  gboolean show_padlock_open;
  gboolean show_padlock_closed;

  /* used for the job spinner */
  guint spinner_current;
};

static void
grid_element_free (GridElement *element)
{
  if (element->icon != NULL)
    g_object_unref (element->icon);
  if (element->object != NULL)
    g_object_unref (element->object);
  g_free (element->markup);
  g_list_foreach (element->embedded_elements, (GFunc) grid_element_free, NULL);
  g_list_free (element->embedded_elements);

  g_free (element);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct _GduVolumeGridClass GduVolumeGridClass;
struct _GduVolumeGrid
{
  GtkWidget parent;

  UDisksClient *client;
  GDBusObject *block_device;

  gboolean container_visible;
  gchar *container_markup;
  GIcon *container_icon;

  GList *elements;

  GridElement *selected;
  GridElement *focused;

  guint animation_timeout_id;
};

struct _GduVolumeGridClass
{
  GtkWidgetClass parent_class;

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

G_DEFINE_TYPE (GduVolumeGrid, gdu_volume_grid, GTK_TYPE_WIDGET)

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

  if (grid->container_icon != NULL)
    g_object_unref (grid->container_icon);
  g_free (grid->container_markup);

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
  GList *l;

  num_elements = get_num_elements_for_slice (grid->elements);
  width = num_elements * ELEMENT_MINIMUM_WIDTH;
  for (l = grid->elements; l != NULL; l = l->next)
    {
      GridElement *element = l->data;
      width += element->fixed_width;
    }

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
                                                        G_TYPE_DBUS_OBJECT,
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
  gtk_widget_set_app_paintable (GTK_WIDGET (grid), TRUE);
}

GtkWidget *
gdu_volume_grid_new (UDisksClient *client)
{
  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  return GTK_WIDGET (g_object_new (GDU_TYPE_VOLUME_GRID,
                                   "client", client,
                                   NULL));
}

GDBusObject *
gdu_volume_grid_get_block_device (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), NULL);
  return grid->block_device;
}

void
gdu_volume_grid_set_block_device (GduVolumeGrid     *grid,
                                  GDBusObject  *block_device)
{
  g_return_if_fail (GDU_IS_VOLUME_GRID (grid));

  if (block_device == grid->block_device)
    goto out;

  if (grid->block_device != NULL)
    g_object_unref (grid->block_device);
  grid->block_device = block_device != NULL ? g_object_ref (block_device) : NULL;

  /* this causes recompute_grid() to select the first element */
  grid->selected = NULL;
  grid->focused = NULL;
  recompute_grid (grid);

  g_object_notify (G_OBJECT (grid), "block-device");

  g_signal_emit (grid,
                 signals[CHANGED_SIGNAL],
                 0);
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
  gint extra;

  /* first steal all the allocated minimum width OR fixed_width for each element - then
   * distribute remaining pixels based on the size_ratio and add
   * the allocated minimum width.
   */
  extra = width;
  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *element = l->data;
      if (element->fixed_width > 0)
        extra -= element->fixed_width;
      else
        extra -= get_num_elements_for_slice (element->embedded_elements) * ELEMENT_MINIMUM_WIDTH;
    }

  g_warn_if_fail (width >= 0);

  x = 0;
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
          element_width = width - x;
        }
      else
        {
          if (element->fixed_width > 0)
            {
              g_warn_if_fail (element->size_ratio == 0.0);
              element_width = element->fixed_width;
            }
          else
            {
              element_width = element->size_ratio * extra;
              element_width += get_num_elements_for_slice (element->embedded_elements) * ELEMENT_MINIMUM_WIDTH;
            }
        }

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
  GdkPixbuf *icon_pixbuf;
  gint icon_width;
  gint icon_height;
  gint icon_offset;
  GPtrArray *pixbufs_to_render;
  guint n;

  need_animation_timeout = FALSE;

  /* TODO: use GtkStyleContext and/or CSS etc. instead of hard-coding colors */
  fill_red     = 1;
  fill_green   = 1;
  fill_blue    = 1;
  fill_selected_red     = 0.29;
  fill_selected_green   = 0.56;
  fill_selected_blue    = 0.85;
  fill_selected_not_focused_red     = 0.29;
  fill_selected_not_focused_green   = 0.56;
  fill_selected_not_focused_blue    = 0.85;
  focus_rect_red     = 0.60;
  focus_rect_green   = 0.70;
  focus_rect_blue    = 0.80;
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

  //g_debug ("rendering element: x=%d w=%d",
  //         element->x,
  //         element->width);

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

  /* text + icon */
  layout = pango_cairo_create_layout (cr);
  pango_layout_set_markup (layout, element->markup != NULL ? element->markup : "", -1);
  desc = pango_font_description_from_string ("Sans 7.0");
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);
  pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
  pango_layout_set_width (layout, pango_units_from_double (element->width));
  pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
  pango_layout_get_size (layout, &width, &height);

  icon_width = 0;
  icon_height = 0;
  icon_pixbuf = NULL;
  if (element->icon != NULL)
    {
      GtkIconInfo *icon_info;
      icon_info = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_default (),
                                                  element->icon,
                                                  24,
                                                  0); /* GtkIconLookupFlags */
      if (icon_info != NULL)
        {
          icon_pixbuf = gtk_icon_info_load_icon (icon_info, NULL); /* GError */
          icon_width = gdk_pixbuf_get_height (icon_pixbuf);
          icon_height = gdk_pixbuf_get_height (icon_pixbuf);
          gtk_icon_info_free (icon_info);
        }
    }
  if (icon_pixbuf != NULL)
    {
      cairo_save (cr);
      render_pixbuf (cr,
                     ceil (element->x + element->width/2.0 - icon_width/2.0),
                     ceil (element->y + element->height/2.0 - pango_units_to_double (height)/2.0 - icon_height/2.0),
                     icon_pixbuf);
      cairo_restore (cr);
      g_object_unref (icon_pixbuf);
    }

  cairo_move_to (cr,
                 ceil (element->x),
                 ceil (element->y + element->height/2.0 - pango_units_to_double (height)/2.0 + icon_height/2.0));
  pango_cairo_show_layout (cr, layout);
  g_object_unref (layout);

  icon_offset = 0;

  if (element->show_spinner)
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
  if (element->show_padlock_open)
    g_ptr_array_add (pixbufs_to_render,
                     gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                               "gdu-encrypted-unlock",
                                               16, 0, NULL));
  if (element->show_padlock_closed)
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
          if (is_grid_focused)
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
do_find_element_for_offset_and_object (GList *elements,
                                       guint64 offset,
                                       GDBusObject *object)
{
  GList *l;
  GridElement *ret;

  ret = NULL;

  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *e = l->data;

      if (e->offset == offset && e->object == object)
        {
          ret = e;
          goto out;
        }

      ret = do_find_element_for_offset_and_object (e->embedded_elements, offset, object);
      if (ret != NULL)
        goto out;
    }

 out:
  return ret;
}

static GridElement *
find_element_for_offset_and_object (GduVolumeGrid  *grid,
                                    guint64         offset,
                                    GDBusObject    *object)
{
  return do_find_element_for_offset_and_object (grid->elements, offset, object);
}

static gint
partition_sort_by_offset_func (GDBusObject *a,
                               GDBusObject *b)
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

static GDBusObject *
lookup_cleartext_device_for_crypto_device (GduVolumeGrid *grid,
                                           const gchar   *object_path)
{
  GDBusProxyManager *proxy_manager;
  GDBusObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  proxy_manager = udisks_client_get_proxy_manager (grid->client);
  objects = g_dbus_proxy_manager_get_objects (proxy_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObject *object = G_DBUS_OBJECT (l->data);
      UDisksBlockDevice *block;

      block = UDISKS_PEEK_BLOCK_DEVICE (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_device_get_crypto_backing_device (block),
                     object_path) == 0)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static GridElement *
maybe_add_crypto (GduVolumeGrid    *grid,
                  GridElement      *element)
{
  UDisksBlockDevice *block;
  GridElement *cleartext_element;

  cleartext_element = NULL;

  if (element->object == NULL)
    goto out;

  block = UDISKS_PEEK_BLOCK_DEVICE (element->object);
  if (block == NULL)
    goto out;

  if (g_strcmp0 (udisks_block_device_get_id_usage (block), "crypto") == 0)
    {
      GDBusObject *cleartext_object;
      GridElement *embedded_cleartext_element;

      cleartext_object = lookup_cleartext_device_for_crypto_device (grid,
                                   g_dbus_object_get_object_path (element->object));
      if (cleartext_object == NULL)
        {
          element->show_padlock_closed = TRUE;
        }
      else
        {
          element->show_padlock_open = TRUE;
          cleartext_element = g_new0 (GridElement, 1);
          cleartext_element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE;
          cleartext_element->parent = element;
          cleartext_element->size_ratio = 1.0;
          cleartext_element->object = g_object_ref (cleartext_object);
          cleartext_element->offset = 0;
          cleartext_element->size = udisks_block_device_get_size (UDISKS_PEEK_BLOCK_DEVICE (cleartext_object));
          grid_element_set_details (grid, cleartext_element);

          /* recurse to handle multiple layers of encryption... */
          embedded_cleartext_element = maybe_add_crypto (grid, cleartext_element);
          if (embedded_cleartext_element != NULL)
            cleartext_element->embedded_elements = g_list_prepend (NULL, embedded_cleartext_element);

          g_object_unref (cleartext_object);
        }
    }

 out:
  return cleartext_element;
}

static GList *
recompute_grid_add_partitions (GduVolumeGrid  *grid,
                               guint64         total_size,
                               GridElement    *parent,
                               guint64         free_space_slack,
                               guint64         top_offset,
                               guint64         top_size,
                               GList          *partitions,
                               GDBusObject    *extended_partition,
                               GList          *logical_partitions)
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
      GDBusObject *object = G_DBUS_OBJECT (l->data);
      UDisksBlockDevice *block;
      guint64 begin, end, size;

      block = UDISKS_PEEK_BLOCK_DEVICE (object);
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
      element->object = g_object_ref (object);
      element->offset = begin;
      element->size = size;
      element->prev = prev_element;
      if (prev_element != NULL)
        prev_element->next = element;
      ret = g_list_append (ret, element);
      prev_element = element;
      prev_end = end;
      grid_element_set_details (grid, element);

      if (object == extended_partition)
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
      else
        {
          GridElement *cleartext_element;
          cleartext_element = maybe_add_crypto (grid, element);
          if (cleartext_element != NULL)
            element->embedded_elements = g_list_prepend (NULL, cleartext_element);
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
  GDBusObject *extended_partition;
  GList *objects;
  GDBusProxyManager *proxy_manager;
  GList *l;
  const gchar *top_object_path;
  UDisksBlockDevice *top_block;
  guint64 top_size;
  guint64 free_space_slack;
  GridElement *element;
  guint64 cur_selected_offset;
  guint64 cur_focused_offset;
  GDBusObject *cur_selected_object;
  GDBusObject *cur_focused_object;

  cur_selected_offset = G_MAXUINT64;
  cur_selected_object = NULL;
  if (grid->selected != NULL)
    {
      cur_selected_offset = grid->selected->offset;
      cur_selected_object = grid->selected->object;
    }
  cur_focused_offset = G_MAXUINT64;
  cur_focused_object = NULL;
  if (grid->focused != NULL)
    {
      cur_focused_offset = grid->focused->offset;
      cur_focused_object = grid->focused->object;
    }

  /* delete all old elements */
  g_list_foreach (grid->elements, (GFunc) grid_element_free, NULL);
  g_list_free (grid->elements);
  grid->elements = NULL;

  //g_debug ("TODO: recompute grid for %s, container_visible=%d",
  //         grid->block_device != NULL ?
  //         g_dbus_object_get_object_path (grid->block_device) : "<nothing selected>",
  //         grid->container_visible);

  if (grid->container_visible)
    {
      element = g_new0 (GridElement, 1);
      element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER;
      element->fixed_width = 40;
      element->offset = 0;
      element->size = 0;
      element->markup = g_strdup (grid->container_markup);
      element->icon = grid->container_icon != NULL ? g_object_ref (grid->container_icon) : NULL;
      grid->elements = g_list_append (grid->elements, element);
    }

  if (grid->block_device == NULL)
    {
      element = g_new0 (GridElement, 1);
      element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA;
      element->size_ratio = 1.0;
      element->offset = 0;
      element->size = 0;
      if (grid->elements != NULL)
        {
          ((GridElement *) grid->elements->data)->next = element;
          element->prev = ((GridElement *) grid->elements->data);
        }
      grid->elements = g_list_append (grid->elements, element);
      grid_element_set_details (grid, element);
      goto out;
    }

  top_object_path = g_dbus_object_get_object_path (grid->block_device);
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
  objects = g_dbus_proxy_manager_get_objects (proxy_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObject *object = G_DBUS_OBJECT (l->data);
      UDisksBlockDevice *block;
      gboolean is_logical;

      block = UDISKS_PEEK_BLOCK_DEVICE (object);
      if (block != NULL &&
          g_strcmp0 (udisks_block_device_get_part_entry_table (block),
                     top_object_path) == 0)
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
                      extended_partition = object;
                    }
                }
            }

          if (is_logical)
            logical_partitions = g_list_prepend (logical_partitions, object);
          else
            partitions = g_list_prepend (partitions, object);
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
          element->size = top_size;
          if (grid->elements != NULL)
            {
              ((GridElement *) grid->elements->data)->next = element;
              element->prev = ((GridElement *) grid->elements->data);
            }
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
          element->object = g_object_ref (grid->block_device);
          if (grid->elements != NULL)
            {
              ((GridElement *) grid->elements->data)->next = element;
              element->prev = ((GridElement *) grid->elements->data);
            }
          grid->elements = g_list_append (grid->elements, element);
          grid_element_set_details (grid, element);
          maybe_add_crypto (grid, element);
        }
    }
  else
    {
      GList *result;
      result = recompute_grid_add_partitions (grid,
                                              top_size,
                                              NULL,
                                              free_space_slack,
                                              0,
                                              top_size,
                                              partitions,
                                              extended_partition,
                                              logical_partitions);
      if (grid->elements != NULL)
        {
          ((GridElement *) grid->elements->data)->next = ((GridElement *) result->data);
          ((GridElement *) result->data)->prev =((GridElement *) grid->elements->data);
        }
      grid->elements = g_list_concat (grid->elements, result);
    }

  g_list_free (logical_partitions);
  g_list_free (partitions);
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);

 out:

  /* reselect focused and selected elements */
  grid->selected = find_element_for_offset_and_object (grid, cur_selected_offset, cur_selected_object);
  grid->focused = find_element_for_offset_and_object (grid, cur_focused_offset, cur_focused_object);

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

GDBusObject *
gdu_volume_grid_get_selected_device (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), NULL);
  return grid->selected->object;
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
    case GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER:
      g_assert_not_reached ();
      break;

    case GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA:
      element->markup = g_strdup (_("No Media"));
      break;

    case GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE:
      {
        gchar *size_str;
        size_str = udisks_util_get_size_for_display (element->size, FALSE, FALSE);
        element->markup = g_strdup_printf ("%s\n%s",
                                           C_("volume-grid", "Free Space"),
                                           size_str);
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
        const gchar *version;
        gint partition_type;
        gchar *type_for_display;

        size_str = udisks_util_get_size_for_display (element->size, FALSE, FALSE);
        block = UDISKS_PEEK_BLOCK_DEVICE (element->object);

        usage = udisks_block_device_get_id_usage (block);
        type = udisks_block_device_get_id_type (block);
        version = udisks_block_device_get_id_version (block);
        partition_type = strtol (udisks_block_device_get_part_entry_type (block), NULL, 0);

        if (udisks_block_device_get_part_entry (block) &&
            g_strcmp0 (udisks_block_device_get_part_entry_scheme (block), "mbr") == 0 &&
            (partition_type == 0x05 || partition_type == 0x0f || partition_type == 0x85))
          {
            s = g_strdup_printf ("%s\n%s",
                                 C_("volume-grid", "Extended Partition"),
                                 size_str);
          }
        else if (g_strcmp0 (usage, "filesystem") == 0)
          {
            const gchar *label;
            label = udisks_block_device_get_id_label (block);
            type_for_display = udisks_util_get_id_for_display (usage, type, version, FALSE);
            if (strlen (label) == 0)
              label = C_("volume-grid", "Filesystem");
            s = g_strdup_printf ("%s\n%s %s", label, size_str, type_for_display);
            g_free (type_for_display);
          }
        else if (g_strcmp0 (usage, "other") == 0 && g_strcmp0 (type, "swap") == 0)
          {
            const gchar *label;
            label = udisks_block_device_get_id_label (block);
            type_for_display = udisks_util_get_id_for_display (usage, type, version, FALSE);
            if (strlen (label) == 0)
              label = C_("volume-grid", "Swap");
            s = g_strdup_printf ("%s\n%s %s", label, size_str, type_for_display);
            g_free (type_for_display);
          }
        else
          {
            type_for_display = udisks_util_get_id_for_display (usage, type, version, FALSE);
            s = g_strdup_printf ("%s\n%s",
                                 type_for_display,
                                 size_str);
            g_free (type_for_display);
          }
        element->markup = s;
        g_free (size_str);
      }
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_disk_or_partition_in_grid (GduVolumeGrid *grid,
                              GDBusObject   *object)
{
  UDisksBlockDevice *block;
  gboolean ret;

  ret = FALSE;

  block = UDISKS_PEEK_BLOCK_DEVICE (object);
  if (block == NULL)
    goto out;

  if (object == grid->block_device ||
      g_strcmp0 (udisks_block_device_get_part_entry_table (block),
                 g_dbus_object_get_object_path (grid->block_device)) == 0)
    ret = TRUE;

 out:
  return ret;
}

gboolean
gdu_volume_grid_includes_object (GduVolumeGrid       *grid,
                                       GDBusObject    *object)
{
  UDisksBlockDevice *block;
  const gchar *crypto_backing_device;
  GDBusObject *crypto_object;
  gboolean ret;

  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), FALSE);
  g_return_val_if_fail (G_IS_DBUS_OBJECT (object), FALSE);

  ret = FALSE;
  crypto_object = NULL;

  if (grid->block_device == NULL)
    goto out;

  if (is_disk_or_partition_in_grid (grid, object))
    {
      ret = TRUE;
      goto out;
    }

  /* handle when it's a crypt devices for our grid or a partition in it */
  block = UDISKS_PEEK_BLOCK_DEVICE (object);
  if (block != NULL)
    {
      crypto_backing_device = udisks_block_device_get_crypto_backing_device (block);
      crypto_object = g_dbus_proxy_manager_get_object (udisks_client_get_proxy_manager (grid->client),
                                                       crypto_backing_device);
      if (crypto_object != NULL)
        {
          if (is_disk_or_partition_in_grid (grid, crypto_object))
            {
              ret = TRUE;
              goto out;
            }
        }
    }

 out:
  if (crypto_object != NULL)
    g_object_unref (crypto_object);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
maybe_update (GduVolumeGrid    *grid,
              GDBusObjectProxy *object_proxy)
{
  if (gdu_volume_grid_includes_object (grid, G_DBUS_OBJECT (object_proxy)))
    recompute_grid (grid);
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

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_volume_grid_set_container_visible (GduVolumeGrid  *grid,
                                       gboolean        visible)
{
  g_return_if_fail (GDU_IS_VOLUME_GRID (grid));
  if (!!grid->container_visible != !!visible)
    {
      grid->container_visible = visible;
      recompute_grid (grid);
    }
}

void
gdu_volume_grid_set_container_markup (GduVolumeGrid  *grid,
                                      const gchar    *markup)
{
  g_return_if_fail (GDU_IS_VOLUME_GRID (grid));
  if (g_strcmp0 (grid->container_markup, markup) != 0)
    {
      g_free (grid->container_markup);
      grid->container_markup = g_strdup (markup);
      recompute_grid (grid);
    }
}

void
gdu_volume_grid_set_container_icon (GduVolumeGrid       *grid,
                                    GIcon               *icon)
{
  g_return_if_fail (GDU_IS_VOLUME_GRID (grid));
  if (!g_icon_equal (grid->container_icon, icon))
    {
      if (grid->container_icon != NULL)
        g_object_unref (grid->container_icon);
      grid->container_icon = icon != NULL ? g_object_ref (icon) : NULL;
      recompute_grid (grid);
    }
}
