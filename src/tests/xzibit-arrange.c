/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * xzibit-arrange: arranges and labels windows which are sent or
 * received by xzibit.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
 * Copyright (c) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <stdlib.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>

#define LABEL_HEIGHT 50
#define HORIZONTAL_MARGIN 50
#define VERTICAL_MARGIN 20

int cursor_x = 0;
unsigned int count = 0;

static void
create_label (gboolean is_sent,
	      int x, int y,
	      int width)
{
  XWindowChanges changes;
  GdkGeometry geometry;
  const char *message =
    is_sent? "Sent": "Received";
  GtkWidget *window =
    gtk_window_new (GTK_WINDOW_TOPLEVEL);
  GtkWidget *label =
    gtk_label_new (message);

  gtk_container_add (GTK_CONTAINER (window),
		     label);

  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);

  gtk_widget_show_all (window);

  changes.x = x;
  changes.y = y;

  XConfigureWindow (gdk_x11_get_default_xdisplay (),
		    GDK_WINDOW_XID (window->window),
		    CWX | CWY,
		    &changes);

  geometry.min_width = geometry.max_width = width;
  geometry.min_height = geometry.max_height = LABEL_HEIGHT;

  gtk_window_set_geometry_hints (GTK_WINDOW (window),
				 NULL,
				 &geometry,
				 GDK_HINT_MIN_SIZE|
				 GDK_HINT_MAX_SIZE);

}

static void
arrange_windows_tail (Window window)
{
  Window root, parent;
  Window *kids;
  Window found = 0;
  int window_is_shared;
  Atom actual_type;
  int actual_format;
  long n_items, bytes_after;
  unsigned char *prop_return;
  unsigned int n_kids, i;

  if (XQueryTree (gdk_x11_get_default_xdisplay (),
		  window,
		  &root,
		  &parent,
		  &kids,
		  &n_kids)==0)
    {
      return;
    }

  for (i=0; i<n_kids; i++)
    {
      arrange_windows_tail (kids[i]);
    }

  if (kids)
    {
      XFree (kids);
    }

  XGetWindowProperty (gdk_x11_get_default_xdisplay (),
		      window,
		      gdk_x11_get_xatom_by_name("_XZIBIT_SHARE"),
		      0, 4, False,
		      gdk_x11_get_xatom_by_name("CARDINAL"),
		      &actual_type,
		      &actual_format,
		      &n_items,
		      &bytes_after,
		      &prop_return);

  window_is_shared = 0;

  if (prop_return)
    {
      window_is_shared = *((int*) prop_return);
      XFree (prop_return);
    }

  if (window_is_shared==1 || window_is_shared==2)
    {
      XWindowChanges changes;
      Window root;
      int x, y, width, height, borderwidth, depth;

      /* found one! */
      count++;

      XGetGeometry (gdk_x11_get_default_xdisplay (),
		    window,
		    &root,
		    &x, &y, &width, &height, &borderwidth, &depth);

      cursor_x += HORIZONTAL_MARGIN;

      changes.x = cursor_x;
      changes.y = VERTICAL_MARGIN*5 + LABEL_HEIGHT;

      XConfigureWindow (gdk_x11_get_default_xdisplay (),
			window,
			CWX | CWY,
			&changes);

      create_label (window_is_shared==1,
		    cursor_x, VERTICAL_MARGIN + LABEL_HEIGHT,
		    width);

      cursor_x += width;
    }
}

static void
arrange_windows (void)
{
  return arrange_windows_tail (gdk_x11_get_default_root_xwindow ());
}

int
main(int argc, char **argv)
{
  gtk_init (&argc, &argv);

  arrange_windows ();
  
  if (count==0)
    {
      g_print ("There were no xzibit windows on this display.\n");
    }
  else
    {
      gtk_main ();
    }
}

/* EOF xzibit-arrange.c */
