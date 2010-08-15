/*
  Simple demonstration of an Xzibit client:
  displays a picture of the planet Jupiter
  and makes the mouse cursor orbit it.
*/
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include "xzibit-client.h"
#include "rocket.h"

XzibitClient *xzibit = NULL;
const gdouble pi = 3.14159265;
gdouble mouse_position = 0.0;
int channel;
const int screensize = 256;

static gboolean
orbit_mouse (gpointer data)
{
  const int half = screensize / 2;
  int x = sin(mouse_position)*half+half;
  int y = cos(mouse_position)*half+half;

  xzibit_client_move_pointer (xzibit,
			      channel,
			      x,
			      y);

  /* and now move the mouse around */

  mouse_position += pi/10;

  if (mouse_position > 2*pi)
    mouse_position = 0.0;
}

int
main(int argc, char **argv)
{
  GdkPixbuf *planet;
  GdkPixbuf *rocket;
  GMainLoop *gmainloop;
  
  g_type_init ();

  rocket = gdk_pixbuf_new_from_xpm_data (rocket_image);

  gmainloop = g_main_loop_new (NULL, FALSE);

  xzibit = xzibit_client_new ();
  xzibit_client_send_avatar (xzibit, rocket);

  planet = gdk_pixbuf_new_from_file ("jupiter.jpg",
				     NULL);
  channel = xzibit_client_open_channel (xzibit);
  xzibit_client_send_video (xzibit, channel, planet);

  g_timeout_add (100,
                 orbit_mouse,
                 NULL);

  g_main_loop_run (gmainloop);

  g_print ("DONE.\n");
}
