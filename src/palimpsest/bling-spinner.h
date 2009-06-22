/* @Copyright (C) 2007 John Stowers, Neil Jagdish Patel.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 */


#ifndef _BLING_SPINNER_H_
#define _BLING_SPINNER_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BLING_TYPE_SPINNER           (bling_spinner_get_type ())
#define BLING_SPINNER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), BLING_TYPE_SPINNER, BlingSpinner))
#define BLING_SPINNER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), BLING_SPINNER,  BlingSpinnerClass))
#define BLING_IS_SPINNER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BLING_TYPE_SPINNER))
#define BLING_IS_SPINNER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), BLING_TYPE_SPINNER))
#define BLING_SPINNER_GET_CLASS      (G_TYPE_INSTANCE_GET_CLASS ((obj), BLING_TYPE_SPINNER, BlingSpinnerClass))

typedef struct _BlingSpinner      BlingSpinner;
typedef struct _BlingSpinnerClass BlingSpinnerClass;
typedef struct _BlingSpinnerPrivate  BlingSpinnerPrivate;

struct _BlingSpinner
{
	GtkDrawingArea parent;
};

struct _BlingSpinnerClass
{
	GtkDrawingAreaClass parent_class;
	BlingSpinnerPrivate *priv;
};

GType bling_spinner_get_type (void);

GtkWidget * bling_spinner_new (void);

void bling_spinner_start (BlingSpinner *spinner);
void bling_spinner_stop  (BlingSpinner *spinner);

G_END_DECLS

#endif
