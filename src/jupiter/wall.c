/*
  Simple demonstration of an Xzibit client:
  displays a picture of the planet Jupiter
  and then sends a wall message.
*/
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include "xzibit-client.h"
#include "rocket.h"

XzibitClient *xzibit = NULL;
int channel;

static gboolean
send_wall (gpointer data)
{
  xzibit_client_send_wall (xzibit,
			   0,
			   "Something there is that does not love a wall.");
  return FALSE;
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
  xzibit_client_set_title (xzibit, channel, "Wall test");
  xzibit_client_set_icon (xzibit, channel, rocket);

  g_timeout_add (3000,
                 send_wall,
                 NULL);

  g_main_loop_run (gmainloop);
}
