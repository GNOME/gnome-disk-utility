/*
 * @file libbling/bling-window.c A RGBA color type, for use with libbling.
 *
 * @Copyright (C) 2007 John Stowers, Neil Jagdish Patel.
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
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <string.h>
#include <bling-color.h>


static int 
getdec(char hexchar)
{
   if ((hexchar >= '0') && (hexchar <= '9')) return hexchar - '0';
   if ((hexchar >= 'A') && (hexchar <= 'F')) return hexchar - 'A' + 10;
   if ((hexchar >= 'a') && (hexchar <= 'f')) return hexchar - 'a' + 10;

   return -1; // Wrong character

}

static void
hex2float(const char* HexColor, float* FloatColor)
{
   const char* HexColorPtr = HexColor;

   int i = 0;
   for (i = 0; i < 4; i++)
   {
     int IntColor = (getdec(HexColorPtr[0]) * 16) +
                     getdec(HexColorPtr[1]);

     FloatColor[i] = (float) IntColor / 255.0;
     HexColorPtr += 2;
   }

}

void 
bling_color_parse_string (BlingColor *color, const gchar *str)
{
	gfloat colors[4];
	size_t len;
	
	len = strlen (str);
	if (len != 8) {
		/* Always return a valid color */
		color->red = 0.0;
		color->green = 0.0;
		color->blue = 0.0;
		color->alpha = 1.0;
	} else {
		hex2float (str, colors);
		color->red = colors[0];
		color->green = colors[1];
		color->blue = colors[2];
		color->alpha = colors[3];
	}
}







