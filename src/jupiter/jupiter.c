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
char *holst = NULL;
gsize holst_size = 0;
gsize holst_cursor = 44; /* size of .wav header */

static gboolean
play_holst (gpointer data)
{
  const int second = 44100; /* Hz (shd really read this out of the file) */

  int length;
  gboolean result;

  if (holst_cursor + second < holst_size)
    {
      length = second;
      result = TRUE;
    }
  else
    {
      length = holst_size - holst_cursor;
      result = FALSE;
    }

  xzibit_client_send_audio (xzibit, channel,
			    holst+holst_cursor,
			    length);

  holst_cursor += length;
  g_print ("Cursor now at %d / %d\n", holst_cursor, holst_size);

  return result;
}

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

  g_file_get_contents ("jupiter.wav",
		       &holst,
		       &holst_size,
		       NULL);

  channel = xzibit_client_open_channel (xzibit);

  xzibit_client_send_video (xzibit, channel, planet);
  xzibit_client_set_title (xzibit, channel, "Jupiter");
  xzibit_client_set_icon (xzibit, channel, planet);

  g_timeout_add (1000,
                 play_holst,
                 NULL);

  g_main_loop_run (gmainloop);

  g_print ("DONE.\n");
}
