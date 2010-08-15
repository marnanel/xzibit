/*
  Simple demonstration of an Xzibit client:
  displays a picture of the planet Jupiter
  and plays an excerpt from "Jupiter" from
  the Planets Suite.
*/
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "xzibit-client.h"

XzibitClient *xzibit = NULL;
int channel;

int
main(int argc, char **argv)
{
  GdkPixbuf *planet;
  GMainLoop *gmainloop;
  
  g_type_init ();

  gmainloop = g_main_loop_new (NULL, FALSE);

  xzibit = xzibit_client_new ();

  planet = gdk_pixbuf_new_from_file ("jupiter.jpg",
				     NULL);

  channel = xzibit_client_open_channel (xzibit);

  xzibit_client_send_video (xzibit, channel, planet);

  g_main_loop_run (gmainloop);

  g_print ("DONE.\n");
}
