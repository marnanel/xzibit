#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <gdk/gdkx.h>
#include <math.h>
#include <X11/X.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XInput.h>

#include "black-cursor.h"

typedef struct _Doppelganger {
  int mpx;
  GdkCursor *cursor;
} Doppelganger;

static int
add_mpx_for_window (char *name)
{
  XIAddMasterInfo add;
  int ndevices;
  XIDeviceInfo *devices, *device;
  int i;
  int current_pointer;
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

Doppelganger*
doppelganger_new (GdkPixbuf *pixbuf,
		  char *name)
{
  Doppelganger *result =
    g_malloc (sizeof (Doppelganger));
  int current_pointer;
  GdkPixbuf *scaled =
    gdk_pixbuf_scale_simple (pixbuf,
			     64, 64,
			     GDK_INTERP_BILINEAR);
  GdkPixdata black_arrow_data;
  GdkPixbuf *black_arrow;

  result->mpx = add_mpx_for_window (name);

  /*
   * Composite a black arrow onto the top left-hand
   * corner.
   */
  if (!gdk_pixdata_deserialize (&black_arrow_data,
				-1,
				black_cursor,
				NULL))
    {
      /* This won't happen in practice, so it can be fatal */
      g_error ("Failed to deseralise pointer.");
    }
  black_arrow =
    gdk_pixbuf_from_pixdata (&black_arrow_data,
			     TRUE,
			     NULL);
  gdk_pixbuf_composite (black_arrow,
			scaled,
			0, 0, 13, 21,
			0.0, 0.0, 1.0, 1.0,
			GDK_INTERP_NEAREST,
			255);
  /*gdk_pixbuf_unref (black_arrow);*/

  result->cursor = gdk_cursor_new_from_pixbuf
    (gdk_display_get_default (),
     scaled,
     0, 0);
  gdk_pixbuf_unref (scaled);

  XIGetClientPointer (gdk_x11_get_default_xdisplay (),
		      None,
		      &current_pointer);
  
  XISetClientPointer (gdk_x11_get_default_xdisplay (),
		      None,
		      result->mpx);

  if (XGrabPointer(gdk_x11_get_default_xdisplay (),
		   GDK_ROOT_WINDOW(), False,
		   ButtonPressMask|ButtonReleaseMask, GrabModeSync,
		   GrabModeAsync, GDK_ROOT_WINDOW(),
		   gdk_x11_cursor_get_xcursor (result->cursor),
		   CurrentTime) != GrabSuccess)
    {
      g_warning ("Grab failed.");
    }

  XISetClientPointer (gdk_x11_get_default_xdisplay (),
		      None,
		      current_pointer);

  return result;
}

void
doppelganger_move (Doppelganger *dg,
		   int x, int y)
{
  int current_pointer;

  if (dg->mpx==-1)
    return;

  XIWarpPointer (gdk_x11_get_default_xdisplay (),
		 dg->mpx,
		 None, GDK_ROOT_WINDOW(),
		 0, 0, 0, 0,
		 x, y);
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

  doppelganger_move (dg,
		     100+100*sin(place*(M_PI/180)),
		     100+100*cos(place*(M_PI/180)));

  return TRUE;
}

int
main (int argc, char **argv)
{
  Doppelganger *dg;
  GdkPixbuf *pixbuf;

  gtk_init (&argc, &argv);

  initialise_extensions ();

  pixbuf = gdk_pixbuf_new_from_file ("jupiter/jupiter.png",
				     NULL);

  dg = doppelganger_new (pixbuf,
			 "fred2");

  g_timeout_add (10,
		 doppelganger_turn,
		 dg);

  gtk_main ();
}

#endif /* DOPPELGANGER_TEST */

/* EOF doppelganger.c */
