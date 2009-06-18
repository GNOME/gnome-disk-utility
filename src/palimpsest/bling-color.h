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


#ifndef _BLING_COLOR_H_
#define _BLING_COLOR_H_

typedef struct {
	gfloat red;
	gfloat green;
	gfloat blue;
	gfloat alpha;
	
} BlingColor;


void bling_color_parse_string (BlingColor *color, const gchar *str);


#endif /* _BLING_COLOR_H_ */
