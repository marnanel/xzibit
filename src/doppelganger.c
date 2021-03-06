/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Doppelganger pointers.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
 *
 * Based on the default Mutter plugin by
 * Tomas Frydrych <tf@linux.intel.com>
 *
 * Based on original ssh-contacts code from Telepathy by
 * Xavier Claessens <xclaesse@gmail.com>
 * 
 * Copyright (c) 2010 Collabora Ltd.
 * Copyright (c) 2008 Intel Corp.
 * Copyright (c) 2010 Xavier Claessens <xclaesse@gmail.com>
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

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <gdk/gdkx.h>
#include <math.h>
#include <string.h>
#include <X11/X.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XInput.h>

#include "doppelganger.h"
#include "black-cursor.h"

struct _Doppelganger {
  int mpx;
  GdkCursor *cursor;
  GdkCursor *blank;
};

static int
add_mpx_for_window (char *name)
{
  XIAddMasterInfo add;
  int ndevices;
  XIDeviceInfo *devices, *device;
  int i;
  int result;

  /* add the device */

  add.type = XIAddMaster;
  add.name = name;
  add.send_core = True;
  add.enable = True;

  XIChangeHierarchy (gdk_x11_get_default_xdisplay (),
		     (XIAnyHierarchyChangeInfo*) &add,
		     1);

  /* now see whether it's in the list */

  result = -1;

  devices = XIQueryDevice(gdk_x11_get_default_xdisplay (),
			  XIAllDevices, &ndevices);

  for (i = 0; i < ndevices; i++) {
    device = &devices[i];

    if (g_str_has_prefix (device->name,
			  name))
      {
	switch (device->use)
	  {
	  case XIMasterPointer:
	    result = device->deviceid;
	    break;
	  }
      }
  }

  if (result==-1)
    {
      g_warning ("The doppelganger pointer '%s' could not be created.",
		 name);
    }

  XIFreeDeviceInfo(devices);

  return result;
}

static void
show_cursor (Doppelganger *dg)
{
  int current_pointer;
  XIEventMask mask = { dg->mpx, 0, "" };

#if 0
  /* probably unnecessary */
  XIGetClientPointer (gdk_x11_get_default_xdisplay (),
		      None,
		      &current_pointer);
  
  /* probably unnecessary */
  XISetClientPointer (gdk_x11_get_default_xdisplay (),
		      None,
		      dg->mpx);

  if (XIGrabDevice (gdk_x11_get_default_xdisplay (),
                    dg->mpx,
                    GDK_ROOT_WINDOW(), CurrentTime,
                    gdk_x11_cursor_get_xcursor (dg->cursor),
                    GrabModeAsync, GrabModeAsync,
                    True, &mask) != GrabSuccess)
    {
      g_warning ("Grab failed.");
    }

  /* probably unnecessary */
  XISetClientPointer (gdk_x11_get_default_xdisplay (),
		      None,
		      current_pointer);

#endif
}

void
doppelganger_set_image (Doppelganger *dg,
                        GdkPixbuf *pixbuf)
{
  GdkPixbuf *scaled = NULL;
  GdkPixdata black_arrow_data;
  GdkPixbuf *black_arrow;
  GdkPixbuf *cursor_image;
  gboolean preexisting = dg->cursor!=NULL;

  if (!gdk_pixdata_deserialize (&black_arrow_data,
				-1,
				black_cursor,
				NULL) ||
      black_cursor == NULL)
    {
      /* This won't happen in practice, so it can be fatal */
      g_error ("Failed to deseralise pointer.");
    }

  black_arrow =
    gdk_pixbuf_from_pixdata (&black_arrow_data,
			     TRUE,
			     NULL);

  if (pixbuf)
    {
      scaled = gdk_pixbuf_scale_simple (pixbuf,
					64, 64,
					GDK_INTERP_BILINEAR);

      /*
       * Composite a black arrow onto the top left-hand
       * corner.
       */

      gdk_pixbuf_composite (black_arrow,
                            scaled,
                            0, 0, 13, 21,
                            0.0, 0.0, 1.0, 1.0,
                            GDK_INTERP_NEAREST,
                            255);
      gdk_pixbuf_unref (black_arrow);

      cursor_image = scaled;
    }
  else
    {
      cursor_image = black_arrow;
    }

  if (preexisting)
    {
      gdk_cursor_unref (dg->cursor);
    }

  dg->cursor = gdk_cursor_new_from_pixbuf
    (gdk_display_get_default (),
     cursor_image,
     1, 1);
  gdk_pixbuf_unref (cursor_image);

#if 0
  if (preexisting)
    {
      XIEventMask mask = { dg->mpx, 0, "" };

      if (XIUngrabDevice (gdk_x11_get_default_xdisplay (),
                          dg->mpx,
                          CurrentTime) != GrabSuccess)
        {
          g_warning ("Ungrab failed.");
        }

      if (XIGrabDevice (gdk_x11_get_default_xdisplay (),
                        dg->mpx,
                        GDK_ROOT_WINDOW(), CurrentTime,
                        gdk_x11_cursor_get_xcursor (dg->cursor),
                        GrabModeAsync, GrabModeAsync,
                        True, &mask) != GrabSuccess)
        {
          g_warning ("Grab failed.");
        }
    }
#endif
}

Doppelganger*
doppelganger_new (char *name)
{
  Doppelganger *result =
    g_malloc (sizeof (Doppelganger));
  GdkPixbuf *blank;
  int current_pointer;

  result->mpx = add_mpx_for_window (name);

  result->cursor = NULL;
  doppelganger_set_image (result, NULL);

  blank = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
			  TRUE,
			  8, 1, 1);
  gdk_pixbuf_fill (blank,
                   0); /* transparent */

  result->blank = gdk_cursor_new_from_pixbuf
    (gdk_display_get_default (),
     blank,
     0, 0);
  gdk_pixbuf_unref (blank);

  show_cursor (result);

  return result;
}

void
doppelganger_move_by_window (Doppelganger *dg,
                             Window w,
                             int x, int y)
{
  int current_pointer;

  if (dg->mpx==-1)
    return;

  XIWarpPointer (gdk_x11_get_default_xdisplay (),
		 dg->mpx,
		 None, w,
		 0, 0, 0, 0,
		 x, y);
}

void
doppelganger_move (Doppelganger *dg,
                   int x, int y)
{
  doppelganger_move_by_window (dg,
                               GDK_ROOT_WINDOW (),
                               x, y);
}

void
doppelganger_hide (Doppelganger *dg)
{
#if 0
  XChangeActivePointerGrab (gdk_x11_get_default_xdisplay (),
			    0,
			    gdk_x11_cursor_get_xcursor (dg->blank),
			    CurrentTime);
#endif
}

void
doppelganger_show (Doppelganger *dg)
{
#if 0
  XChangeActivePointerGrab (gdk_x11_get_default_xdisplay (),
			    0,
			    gdk_x11_cursor_get_xcursor (dg->cursor),
			    CurrentTime);
#endif
}

void
doppelganger_free (Doppelganger *dg)
{
  g_free (dg);
}

#ifdef DOPPELGANGER_TEST

/**
 * Initialises appropriate X extensions.
 */
static void
initialise_extensions (void)
{
  int major = 2, minor = 0;
  if (XIQueryVersion(gdk_x11_get_default_xdisplay (), &major, &minor) == BadRequest) {
    g_error("XI2 not available. Server supports %d.%d\n", major, minor);
  }
}

gboolean
doppelganger_turn (gpointer user_data)
{
  Doppelganger *dg =
    (Doppelganger*) user_data;
  static double place = 0.0;

  place++;
  while (place>360.0)
    {
      place -= 360.0;
    }

  if (place==300.0)
    {
      doppelganger_hide (dg);
    }
  else if (place==350.0)
    {
      doppelganger_show (dg);
    }

  doppelganger_move (dg,
		     100+100*sin(place*(M_PI/180)),
		     100+100*cos(place*(M_PI/180)));

  return TRUE;
}

gboolean
doppelganger_appear (gpointer user_data)
{
  Doppelganger *dg =
    (Doppelganger*) user_data;
  GdkPixbuf *pixbuf;
  static gboolean romeo = FALSE;

  pixbuf = gdk_pixbuf_new_from_file
    (romeo? "romeo.png": "juliet.png",
     NULL);

  romeo = !romeo;

  doppelganger_set_image (dg, pixbuf);

  gdk_pixbuf_unref (pixbuf);

  return TRUE;
}

int
main (int argc, char **argv)
{
  Doppelganger *dg;

  gtk_init (&argc, &argv);

  initialise_extensions ();

  dg = doppelganger_new ("fred2");

  g_timeout_add (10,
		 doppelganger_turn,
		 dg);

  g_timeout_add (750,
		 doppelganger_appear,
		 dg);

  gtk_main ();
}

#endif /* DOPPELGANGER_TEST */

/* EOF doppelganger.c */
