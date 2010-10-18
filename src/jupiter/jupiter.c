/*
  Simple demonstration of an Xzibit client:
  displays a picture of the planet Jupiter
  and plays an excerpt from "Jupiter" from
  the Planets Suite.
*/
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include "xzibit-client.h"

#define IMAGE_FILENAME "src/jupiter/jupiter.jpg"

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

static gboolean
orbit_mouse (gpointer data)
{
  GdkPixbuf *planet = (GdkPixbuf*) data;
  int screensize =
    gdk_pixbuf_get_width (planet); /* assume it's square */
  const int half = screensize / 2;
  static gdouble mouse_position = 0.0;
  int x = sin(mouse_position)*half+half;
  int y = cos(mouse_position)*half+half;

#if 0
  if (mouse_position > 5)
    xzibit_client_hide_pointer (xzibit,
				channel);
  else
#endif
    xzibit_client_move_pointer (xzibit,
				channel,
				x,
				y);

  /* and now move the mouse around */

  mouse_position += M_PI/10;

  if (mouse_position > 2*M_PI)
    mouse_position = 0.0;
}

void
set_up_jupiter(int socket)
{
  GdkPixbuf *planet, *planet_icon;

  xzibit = xzibit_client_new_from_fd (socket);

  planet = gdk_pixbuf_new_from_file (IMAGE_FILENAME,
				     NULL);

  if (!planet)
    {
      g_error ("Image %s not found",
	       IMAGE_FILENAME);
    }

  planet_icon = gdk_pixbuf_new_from_file ("src/jupiter/jupiter.png",
					  NULL);

#if 0
  g_file_get_contents ("jupiter.wav",
		       &holst,
		       &holst_size,
		       NULL);
#endif
  channel = xzibit_client_open_channel (xzibit);

  xzibit_client_send_video (xzibit, channel, planet);
  xzibit_client_set_title (xzibit, channel, "Jupiter");
  xzibit_client_set_icon (xzibit, channel, planet_icon);
  xzibit_client_send_avatar (xzibit, planet_icon);

  g_timeout_add (100,
                 orbit_mouse,
                 planet);

}
