#include "vnc.h"
#include <gtk/gtk.h>

void
vnc_start (Window id)
{
  g_warning ("Starting VNC server for %08x", (unsigned int) id);
}

unsigned int
vnc_port (Window id)
{
  return 7177;
}

void
vnc_supply_pixmap (Window id,
		   GdkPixbuf *pixbuf)
{
  g_warning ("Pixmap supplied for %08x", (unsigned int) id);
}

void
vnc_stop (Window id)
{
  g_warning ("Stopping VNC server for %08x", (unsigned int) id);
}

/* eof vnc.c */

