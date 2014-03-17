/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <stdlib.h>

#include "gduvolumegrid.h"
#include "gduapplication.h"

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
  UDisksObject *object;
  gint64 offset;
  gint64 size;
  gint64 unused;

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

  gboolean show_spinner;
  gboolean show_padlock_open;
  gboolean show_padlock_closed;
  gboolean show_mounted;
  gboolean show_configured;
};

static void
grid_element_free (GridElement *element)
{
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

  GduApplication *application;
  UDisksClient *client;
  UDisksObject *block_object;

  GList *elements;

  GridElement *selected;
  GridElement *focused;

  gboolean animating_spinner;

  gchar *no_media_string;
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
  PROP_APPLICATION,
  PROP_BLOCK_OBJECT,
  PROP_NO_MEDIA_STRING,
};

enum
{
  CHANGED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

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

static void on_client_changed (UDisksClient   *client,
                               gpointer        user_data);

static void
gdu_volume_grid_finalize (GObject *object)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (object);

  g_signal_handlers_disconnect_by_func (grid->client,
                                        G_CALLBACK (on_client_changed),
                                        grid);

  g_list_foreach (grid->elements, (GFunc) grid_element_free, NULL);
  g_list_free (grid->elements);

  if (grid->block_object != NULL)
    g_object_unref (grid->block_object);
  g_object_unref (grid->application);

  g_free (grid->no_media_string);

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
    case PROP_APPLICATION:
      g_value_set_object (value, grid->application);
      break;

    case PROP_BLOCK_OBJECT:
      g_value_set_object (value, grid->block_object);
      break;

    case PROP_NO_MEDIA_STRING:
      g_value_set_string (value, grid->no_media_string);
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
    case PROP_APPLICATION:
      grid->application = g_value_dup_object (value);
      grid->client = gdu_application_get_client (grid->application);
      break;

    case PROP_BLOCK_OBJECT:
      gdu_volume_grid_set_block_object (grid, g_value_get_object (value));
      break;

    case PROP_NO_MEDIA_STRING:
      gdu_volume_grid_set_no_media_string (grid, g_value_get_string (value));
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

  g_signal_connect (grid->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
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
    GDK_LEAVE_NOTIFY_MASK |
    GDK_POINTER_MOTION_MASK;
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

  *natural_width = width;

  if (width > 300)
    width = 300;
  *minimal_width = width;
}

static void
gdu_volume_grid_get_preferred_height (GtkWidget *widget,
                                      gint      *minimal_height,
                                      gint      *natural_height)
{
  *minimal_height = *natural_height = 120;
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
                                   PROP_APPLICATION,
                                   g_param_spec_object ("application",
                                                        "Application",
                                                        "The GduApplication to use",
                                                        GDU_TYPE_APPLICATION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_BLOCK_OBJECT,
                                   g_param_spec_object ("block-object",
                                                        "Block Object",
                                                        "The top-level block object to show a grid for",
                                                        G_TYPE_DBUS_OBJECT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_NO_MEDIA_STRING,
                                   g_param_spec_string ("no-media-string",
                                                        "No Media String",
                                                        "The string to show when there is no media or block device",
                                                        _("No Media"),
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT |
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
gdu_volume_grid_new (GduApplication *application)
{
  g_return_val_if_fail (GDU_IS_APPLICATION (application), NULL);
  return GTK_WIDGET (g_object_new (GDU_TYPE_VOLUME_GRID,
                                   "application", application,
                                   NULL));
}

UDisksObject *
gdu_volume_grid_get_block_object (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), NULL);
  return grid->block_object;
}

void
gdu_volume_grid_set_block_object (GduVolumeGrid *grid,
                                  UDisksObject  *block_object)
{
  g_return_if_fail (GDU_IS_VOLUME_GRID (grid));

  if (block_object == grid->block_object)
    goto out;

  if (grid->block_object != NULL)
    g_object_unref (grid->block_object);
  grid->block_object = block_object != NULL ? g_object_ref (block_object) : NULL;

  /* this causes recompute_grid() to select the first element */
  grid->selected = NULL;
  grid->focused = NULL;
  recompute_grid (grid);

  g_object_notify (G_OBJECT (grid), "block-object");

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
#if 0
      if (element->y + element->height == total_height)
        element->edge_flags |= GRID_EDGE_BOTTOM;
#endif

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

/* returns true if an animation timeout is needed */
static gboolean
render_element (GduVolumeGrid *grid,
                cairo_t       *cr,
                GridElement   *element,
                gboolean       is_selected,
                gboolean       is_focused,
                gboolean       is_grid_focused)
{
  gboolean animate_spinner;
  PangoLayout *layout;
  PangoFontDescription *desc;
  gint text_width, text_height;
  GPtrArray *icons_to_render;
  guint n;
  gdouble x, y, w, h;
  GtkStyleContext *context;
  GtkStateFlags state;
  GtkJunctionSides sides;
  GtkBorder border;
  const gchar *markup;

  animate_spinner = FALSE;

  cairo_save (cr);

  x = element->x;
  y = element->y;
  w = element->width;
  h = element->height;

  context = gtk_widget_get_style_context (GTK_WIDGET (grid));
  gtk_style_context_save (context);
  state = gtk_widget_get_state_flags (GTK_WIDGET (grid));

  state &= ~(GTK_STATE_FLAG_SELECTED | GTK_STATE_FLAG_FOCUSED | GTK_STATE_FLAG_ACTIVE);
  if (is_selected)
    state |= GTK_STATE_FLAG_SELECTED;
  if (is_grid_focused)
    state |= GTK_STATE_FLAG_FOCUSED;
  if (element->show_spinner)
    state |= GTK_STATE_FLAG_ACTIVE;
  gtk_style_context_set_state (context, state);

  /* frames */
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_NOTEBOOK);
  gtk_style_context_add_class (context, "gnome-disk-utility-grid");
  gtk_style_context_get_border (context, state, &border);
  sides = GTK_JUNCTION_NONE;
  if (!(element->edge_flags & GRID_EDGE_TOP))
    {
      sides |= GTK_JUNCTION_TOP;
    }
  if (!(element->edge_flags & GRID_EDGE_BOTTOM))
    {
      sides |= GTK_JUNCTION_BOTTOM;
      h += border.bottom;
    }
  if (!(element->edge_flags & GRID_EDGE_LEFT))
    {
      sides |= GTK_JUNCTION_LEFT;
    }
  if (!(element->edge_flags & GRID_EDGE_RIGHT))
    {
      sides |= GTK_JUNCTION_RIGHT;
      w += border.right;
    }
  gtk_style_context_set_junction_sides (context, sides);
  gtk_render_background (context, cr, x, y, w, h);
  gtk_render_frame (context, cr, x, y, w, h);
  if (is_focused && is_grid_focused)
    gtk_render_focus (context, cr, x + 2, y + 2, w - 4, h - 4);
  if (element->unused > 0)
    {
      gdouble unused_height = element->unused * h / element->size;
      cairo_pattern_t *gradient;
      cairo_save (cr);
      gradient = cairo_pattern_create_linear (x, y + unused_height - 10, x, y + unused_height);
      cairo_pattern_add_color_stop_rgba (gradient, 0.0,  1.0, 1.0, 1.0, 0.25);
      cairo_pattern_add_color_stop_rgba (gradient, 1.0,  1.0, 1.0, 1.0, 0.00);
      cairo_set_source (cr, gradient);
      cairo_pattern_destroy (gradient);
      cairo_rectangle (cr,
                       x,
                       y,
                       w,
                       unused_height);
      cairo_fill (cr);
      cairo_restore (cr);
    }
  gtk_style_context_restore (context);

  /* icons */
  icons_to_render = g_ptr_array_new_with_free_func (NULL);
  if (element->show_padlock_open)
    g_ptr_array_add (icons_to_render, (gpointer) "changes-allow-symbolic");
  if (element->show_padlock_closed)
    g_ptr_array_add (icons_to_render, (gpointer) "changes-prevent-symbolic");
  if (element->show_mounted)
    g_ptr_array_add (icons_to_render, (gpointer) "media-playback-start-symbolic");
  if (element->show_configured)
    g_ptr_array_add (icons_to_render, (gpointer) "user-bookmarks-symbolic");
  if (icons_to_render->len > 0)
    {
      guint icon_offset = 0;
      gtk_style_context_save (context);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_IMAGE);
      for (n = 0; n < icons_to_render->len; n++)
        {
          const gchar *name = icons_to_render->pdata[n];
          GtkIconInfo *info;
          info = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (), name, 12, 0);
          if (info == NULL)
            {
              g_warning ("Error lookup up icon %s", name);
            }
          else
            {
              GdkPixbuf *pixbuf;
              GError *error = NULL;
              pixbuf = gtk_icon_info_load_symbolic_for_context (info, context, NULL, &error);
              if (pixbuf == NULL)
                {
                  g_warning ("Error loading icon %s: %s (%s, %d)",
                             name, error->message, g_quark_to_string (error->domain), error->code);
                  g_error_free (error);
                }
              else
                {
                  guint icon_width;
                  guint icon_height;
                  icon_width = gdk_pixbuf_get_width (pixbuf);
                  icon_height = gdk_pixbuf_get_height (pixbuf);
                  gtk_render_icon (context, cr, pixbuf,
                                   ceil (element->x + element->width - icon_width - icon_offset - 4),
                                   ceil (element->y + element->height - icon_height - 4));
                  icon_offset += icon_width + 2; /* padding */
                  g_object_unref (pixbuf);
                }
              g_object_unref (info);
            }
        }
      gtk_style_context_restore (context);
    }
  g_ptr_array_free (icons_to_render, TRUE);

  /* spinner */
  if (element->show_spinner)
    {
      gtk_style_context_save (context);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_SPINNER);
      gtk_render_activity (context, cr,
                           ceil (element->x) + 4,
                           ceil (element->y + element->height - 16 - 4),
                           16, 16);
      gtk_style_context_restore (context);
      animate_spinner = TRUE;
    }

  /* text */
  layout = pango_cairo_create_layout (cr);
  markup = element->markup;
  if (markup == NULL)
    markup = grid->no_media_string;
  pango_layout_set_markup (layout, markup, -1);
  desc = pango_font_description_from_string ("Sans 7.0");
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);
  pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
  pango_layout_set_width (layout, pango_units_from_double (w));
  pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
  pango_layout_get_size (layout, &text_width, &text_height);
  gtk_render_layout (context, cr, x, y + floor (h / 2.0 - text_height/2/PANGO_SCALE), layout);
  g_object_unref (layout);

  gtk_style_context_restore (context);
  cairo_restore (cr);

  return animate_spinner;
}

static gboolean
render_slice (GduVolumeGrid *grid,
              cairo_t       *cr,
              GList         *elements)
{
  GList *l;
  gboolean animate_spinner;

  animate_spinner = FALSE;
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

      animate_spinner |= render_element (grid,
                                         cr,
                                         element,
                                         is_selected,
                                         is_focused,
                                         is_grid_focused);

      animate_spinner |= render_slice (grid,
                                       cr,
                                       element->embedded_elements);
    }

  return animate_spinner;
}

static gboolean
gdu_volume_grid_draw (GtkWidget *widget,
                      cairo_t   *cr)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (widget);
  GtkAllocation allocation;
  gboolean animate_spinner;

  gtk_widget_get_allocation (widget, &allocation);
  recompute_size (grid, allocation.width, allocation.height);

  animate_spinner = render_slice (grid, cr, grid->elements);

  if (animate_spinner != grid->animating_spinner)
    {
      if (animate_spinner)
        gtk_widget_set_state_flags (widget, GTK_STATE_FLAG_ACTIVE, FALSE);
      else
        gtk_widget_unset_state_flags (widget, GTK_STATE_FLAG_ACTIVE);
    }
  if (animate_spinner)
    grid->animating_spinner = TRUE;
  else
    grid->animating_spinner = FALSE;

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
do_find_element_for_offset_and_object (GList        *elements,
                                       gint64        offset,
                                       UDisksObject *object)
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
find_element_for_offset_and_object (GduVolumeGrid   *grid,
                                    gint64           offset,
                                    UDisksObject    *object)
{
  return do_find_element_for_offset_and_object (grid->elements, offset, object);
}

static gint
partition_sort_by_offset_func (UDisksObject *a,
                               UDisksObject *b)
{
  gint64 oa;
  gint64 ob;
  oa = udisks_partition_get_offset (udisks_object_peek_partition (a));
  ob = udisks_partition_get_offset (udisks_object_peek_partition (b));
  if (oa > ob)
    return 1;
  else if (oa < ob)
    return -1;
  else
    return 0;
}

static void grid_element_set_details (GduVolumeGrid  *grid,
                                      GridElement    *element);

static UDisksObject *
lookup_cleartext_device_for_crypto_device (GduVolumeGrid *grid,
                                           const gchar   *object_path)
{
  GDBusObjectManager *object_manager;
  UDisksObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  object_manager = udisks_client_get_object_manager (grid->client);
  objects = g_dbus_object_manager_get_objects (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_crypto_backing_device (block),
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
  UDisksBlock *block;
  GridElement *cleartext_element;

  cleartext_element = NULL;

  if (element->object == NULL)
    goto out;

  block = udisks_object_peek_block (element->object);
  if (block == NULL)
    goto out;

  if (g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0)
    {
      UDisksObject *cleartext_object;
      GridElement *embedded_cleartext_element;

      cleartext_object = lookup_cleartext_device_for_crypto_device (grid,
                                                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (element->object)));
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
          cleartext_element->size = udisks_block_get_size (udisks_object_peek_block (cleartext_object));
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
                               gint64          total_size,
                               GridElement    *parent,
                               gint64          free_space_slack,
                               gint64          top_offset,
                               gint64          top_size,
                               GList          *partitions,
                               UDisksObject    *extended_partition,
                               GList          *logical_partitions)
{
  gint64 prev_end;
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
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksPartition *partition;
      gint64 begin, end, size;

      partition = udisks_object_peek_partition (object);

      begin = udisks_partition_get_offset (partition);
      size = udisks_partition_get_size (partition);

      if (begin < prev_end)
        begin = prev_end;

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
      element->size_ratio = ((gdouble) size) / top_size;
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
                                                                      begin,
                                                                      size,
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
  UDisksObject *extended_partition;
  GList *objects;
  GDBusObjectManager *object_manager;
  GList *l;
  const gchar *top_object_path;
  UDisksBlock *top_block;
  UDisksPartitionTable *partition_table;
  gint64 top_size;
  gint64 free_space_slack;
  GridElement *element;
  gint64 cur_selected_offset;
  gint64 cur_focused_offset;
  UDisksObject *cur_selected_object;
  UDisksObject *cur_focused_object;

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

  //g_debug ("TODO: recompute grid for %s",
  //         grid->block_object != NULL ?
  //         g_dbus_object_get_object_path (grid->block_object) : "<nothing selected>");

  if (grid->block_object == NULL)
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

  top_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (grid->block_object));
  top_block = udisks_object_peek_block (grid->block_object);
  partition_table = udisks_object_peek_partition_table (grid->block_object);
  top_size = udisks_block_get_size (top_block);

  /* include "Free Space" elements if there is at least this much slack between
   * partitions (currently 1% of the disk, but at most 1MiB)
   */
  free_space_slack = MIN (top_size / 100, 1024*1024);

  partitions = NULL;
  logical_partitions = NULL;
  extended_partition = NULL;
  object_manager = udisks_client_get_object_manager (grid->client);
  objects = g_dbus_object_manager_get_objects (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksPartition *partition;
      gboolean is_logical;

      partition = udisks_object_peek_partition (object);
      if (partition != NULL && partition_table != NULL &&
          g_strcmp0 (udisks_partition_get_table (partition), top_object_path) == 0)
        {
          is_logical = FALSE;
          if (udisks_partition_get_is_contained (partition))
            {
              is_logical = TRUE;
            }
          else if (udisks_partition_get_is_container (partition))
            {
              g_warn_if_fail (extended_partition == NULL);
              extended_partition = object;
            }
          if (is_logical)
            logical_partitions = g_list_prepend (logical_partitions, object);
          else
            partitions = g_list_prepend (partitions, object);
        }
    }

  if (partitions == NULL && partition_table == NULL)
    {
      /* No partitions and whole-disk has no partition table signature... */
      if (top_size == 0)
        {
          UDisksDrive *drive;
          drive = udisks_client_get_drive_for_block (grid->client, top_block);
          if (drive != NULL && !udisks_drive_get_media_change_detected (drive))
            {
              /* If we can't detect media change, just always assume media */
              element = g_new0 (GridElement, 1);
              element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE;
              element->size_ratio = 1.0;
              element->offset = 0;
              element->size = 0;
              element->object = g_object_ref (grid->block_object);
              grid->elements = g_list_append (grid->elements, element);
              grid_element_set_details (grid, element);
            }
          else
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
          g_clear_object (&drive);
        }
      else
        {
          GridElement *cleartext_element;
          element = g_new0 (GridElement, 1);
          element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE;
          element->size_ratio = 1.0;
          element->offset = 0;
          element->size = top_size;
          element->object = g_object_ref (grid->block_object);
          if (grid->elements != NULL)
            {
              ((GridElement *) grid->elements->data)->next = element;
              element->prev = ((GridElement *) grid->elements->data);
            }
          grid->elements = g_list_append (grid->elements, element);
          grid_element_set_details (grid, element);
          cleartext_element = maybe_add_crypto (grid, element);
          if (cleartext_element != NULL)
            element->embedded_elements = g_list_prepend (NULL, cleartext_element);
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

  /* ensure we have at least one element */
  if (grid->elements == NULL)
    {
      element = g_new0 (GridElement, 1);
      element->type = GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA;
      element->size_ratio = 1.0;
      element->offset = 0;
      element->size = 0;
      grid->elements = g_list_append (NULL, element);
      grid_element_set_details (grid, element);
    }

  /* ensure something is always focused/selected */
  if (grid->selected == NULL)
    grid->selected = grid->elements->data;
  if (grid->focused == NULL)
    grid->focused = grid->elements->data;

  /* queue a redraw */
  gtk_widget_queue_draw (GTK_WIDGET (grid));

  g_signal_emit (grid, signals[CHANGED_SIGNAL], 0);
}

/* ---------------------------------------------------------------------------------------------------- */

GduVolumeGridElementType
gdu_volume_grid_get_selected_type (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), 0);
  return grid->selected->type;
}

UDisksObject *
gdu_volume_grid_get_selected_device (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), NULL);
  return grid->selected->object;
}

guint64
gdu_volume_grid_get_selected_offset (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), 0);
  return (guint64) grid->selected->offset;
}

guint64
gdu_volume_grid_get_selected_size (GduVolumeGrid *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), 0);
  return (guint64) grid->selected->size;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
maybe_add_partition (GduVolumeGrid   *grid,
                     GPtrArray       *lines,
                     UDisksPartition *partition)
{
  const gchar *name;
  guint number;
  gchar *s;

  if (partition == NULL)
    goto out;

  name = udisks_partition_get_name (partition);
  number = udisks_partition_get_number (partition);

  if (strlen (name) > 0)
    {
      /* Translators: This is shown in the volume grid for a partition with a name/label.
       *              The %d is the partition number. The %s is the name
       */
      s = g_strdup_printf (C_("volume-grid", "Partition %d: %s"), number, name);
    }
  else
    {
      /* Translators: This is shown in the volume grid for a partition with no name/label.
       *              The %d is the partition number
       */
      s = g_strdup_printf (C_("volume-grid", "Partition %d"), number);
    }
  g_ptr_array_add (lines, s);

 out:
  ;
}

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
      {
        element->markup = NULL; /* means that grid->no_media_string will be used */

        if (grid->block_object != NULL)
          {
            UDisksBlock *block;
            block = udisks_object_peek_block (grid->block_object);
            if (block != NULL && g_variant_n_children (udisks_block_get_configuration (block)) > 0)
              element->show_configured = TRUE;
          }
      }
      break;

    case GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE:
      {
        gchar *size_str;
        size_str = udisks_client_get_size_for_display (grid->client, element->size, FALSE, FALSE);
        element->markup = g_strdup_printf ("%s\n%s",
                                           C_("volume-grid", "Free Space"),
                                           size_str);
        g_free (size_str);
      }
      break;

    case GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE:
      {
        UDisksBlock *block;
        gchar *size_str;
        const gchar *usage;
        const gchar *type;
        const gchar *version;
        const gchar *label;
        gchar *type_for_display;
        UDisksFilesystem *filesystem;
        UDisksPartition *partition;
        GPtrArray *lines;
        GList *jobs;

        size_str = udisks_client_get_size_for_display (grid->client, element->size, FALSE, FALSE);
        block = udisks_object_peek_block (element->object);
        filesystem = udisks_object_peek_filesystem (element->object);
        partition = udisks_object_peek_partition (element->object);

        usage = udisks_block_get_id_usage (block);
        type = udisks_block_get_id_type (block);
        version = udisks_block_get_id_version (block);
        label = udisks_block_get_id_label (block);

        lines = g_ptr_array_new_with_free_func (g_free);

        if (g_variant_n_children (udisks_block_get_configuration (block)) > 0)
          element->show_configured = TRUE;

        jobs = udisks_client_get_jobs_for_object (grid->client, element->object);
        jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (grid->application, element->object));
        element->show_spinner = (jobs != NULL);
        g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
        g_list_free (jobs);

        if (partition != NULL && udisks_partition_get_is_container (partition))
          {
            g_ptr_array_add (lines, g_strdup (C_("volume-grid", "Extended Partition")));
            maybe_add_partition (grid, lines, partition);
            g_ptr_array_add (lines, g_strdup (size_str));
          }
        else if (filesystem != NULL)
          {
            const gchar *const *mount_points;
            UDisksDrive *drive;

            drive = udisks_client_get_drive_for_block (grid->client, block);
            if (drive != NULL && !udisks_drive_get_media_change_detected (drive))
              {
                /* This is for e.g. /dev/fd0 - e.g. if we can't track media
                 * changes then we don't know the size nor usage/type ... so
                 * just print the device name
                 */
                g_ptr_array_add (lines, udisks_block_dup_preferred_device (block));
              }
            else
              {
                type_for_display = udisks_client_get_id_for_display (grid->client, usage, type, version, FALSE);
                if (strlen (label) > 0)
                  g_ptr_array_add (lines, g_strdup (label));
                else
                  g_ptr_array_add (lines, g_strdup (C_("volume-grid", "Filesystem")));
                maybe_add_partition (grid, lines, partition);
                g_ptr_array_add (lines, g_strdup_printf ("%s %s", size_str, type_for_display));
                g_free (type_for_display);
              }
            g_clear_object (&drive);

            mount_points = udisks_filesystem_get_mount_points (filesystem);
            if (g_strv_length ((gchar **) mount_points) > 0)
              element->show_mounted = TRUE;

            element->unused = gdu_utils_get_unused_for_block (grid->client, block);
            if (element->unused < 0)
              element->unused = 0;
          }
        else if (g_strcmp0 (usage, "other") == 0 && g_strcmp0 (type, "swap") == 0)
          {
            UDisksSwapspace *swapspace;

            label = udisks_block_get_id_label (block);
            type_for_display = udisks_client_get_id_for_display (grid->client, usage, type, version, FALSE);
            if (strlen (label) == 0)
              g_ptr_array_add (lines, g_strdup (C_("volume-grid", "Swap")));
            else
              g_ptr_array_add (lines, g_strdup (label));
            maybe_add_partition (grid, lines, partition);
            g_ptr_array_add (lines, g_strdup_printf ("%s %s", size_str, type_for_display));
            g_free (type_for_display);

            swapspace = udisks_object_peek_swapspace (element->object);
            if (swapspace != NULL)
              {
                if (udisks_swapspace_get_active (swapspace))
                  element->show_mounted = TRUE;
              }
          }
        else
          {
            maybe_add_partition (grid, lines, partition);
            type_for_display = udisks_client_get_id_for_display (grid->client, usage, type, version, FALSE);
            g_ptr_array_add (lines, g_strdup_printf ("%s %s", size_str, type_for_display));
            g_free (type_for_display);
          }
        g_ptr_array_add (lines, NULL);
        element->markup = g_strjoinv ("\n", (gchar **) lines->pdata);
        g_ptr_array_unref (lines);
        g_free (size_str);
      }
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_disk_or_partition_in_grid (GduVolumeGrid *grid,
                              UDisksObject  *block_object)
{
  UDisksPartition *partition;
  gboolean ret;

  ret = FALSE;

  partition = udisks_object_peek_partition (block_object);
  if (partition == NULL)
    goto out;

  if (block_object == grid->block_object ||
      g_strcmp0 (udisks_partition_get_table (partition),
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (grid->block_object))) == 0)
    ret = TRUE;

 out:
  return ret;
}

gboolean
gdu_volume_grid_includes_object (GduVolumeGrid   *grid,
                                 UDisksObject    *block_object)
{
  UDisksBlock *block;
  const gchar *crypto_backing_device;
  UDisksObject *crypto_object;
  gboolean ret;

  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), FALSE);
  g_return_val_if_fail (G_IS_DBUS_OBJECT (block_object), FALSE);

  ret = FALSE;
  crypto_object = NULL;

  if (grid->block_object == NULL)
    goto out;

  if (is_disk_or_partition_in_grid (grid, block_object))
    {
      ret = TRUE;
      goto out;
    }

  /* handle when it's a crypt devices for our grid or a partition in it */
  block = udisks_object_peek_block (block_object);
  if (block != NULL)
    {
      crypto_backing_device = udisks_block_get_crypto_backing_device (block);
      crypto_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (grid->client),
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

static GridElement *
find_element_for_object_list (GList         *elements,
                              UDisksObject  *object)
{
  GridElement *ret = NULL;
  GList *l;

  for (l = elements; l != NULL; l = l->next)
    {
      GridElement *e = l->data;
      if (e->object == object)
        {
          ret = e;
          goto out;
        }
      ret = find_element_for_object_list (e->embedded_elements, object);
      if (ret != NULL)
        goto out;
    }
 out:
  return ret;
}

static GridElement *
find_element_for_object (GduVolumeGrid *grid,
                         UDisksObject  *object)
{
  return find_element_for_object_list (grid->elements, object);
}


/* ---------------------------------------------------------------------------------------------------- */

gboolean
gdu_volume_grid_select_object (GduVolumeGrid   *grid,
                               UDisksObject    *block_object)
{
  gboolean ret = FALSE;
  GridElement *elem;

  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), FALSE);
  g_return_val_if_fail (G_IS_DBUS_OBJECT (block_object), FALSE);

  elem = find_element_for_object (grid, block_object);
  if (elem != NULL)
    {
      grid->selected = elem;
      grid->focused = elem;
      ret = TRUE;
      g_signal_emit (grid, signals[CHANGED_SIGNAL], 0);
      gtk_widget_queue_draw (GTK_WIDGET (grid));
    }
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_client_changed (UDisksClient   *client,
                   gpointer        user_data)
{
  GduVolumeGrid *grid = GDU_VOLUME_GRID (user_data);
  recompute_grid (grid);
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_volume_grid_set_no_media_string   (GduVolumeGrid       *grid,
                                       const gchar         *str)
{
  g_return_if_fail (GDU_IS_VOLUME_GRID (grid));
  if (g_strcmp0 (grid->no_media_string, str) == 0)
    goto out;

  g_free (grid->no_media_string);
  grid->no_media_string = g_strdup (str);

  g_object_notify (G_OBJECT (grid), "no-media-string");

  gtk_widget_queue_draw (GTK_WIDGET (grid));

 out:
  ;
}

const gchar *
gdu_volume_grid_get_no_media_string   (GduVolumeGrid      *grid)
{
  g_return_val_if_fail (GDU_IS_VOLUME_GRID (grid), NULL);
  return grid->no_media_string;
}

