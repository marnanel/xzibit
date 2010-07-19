/*
  The goal here is to build a simple VNC server
  that can flip between three images, using
  only libvncserver.
  
  libvncserver is not installed by vino, just built
  and linked in, so we can't link to it in the
  ordinary way.  At the moment we're just taking a
  copy in the current directory and statically
  linking it.
*/

#include <stdint.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <rfb/rfbproto.h>
#include <rfb/rfb.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

static void
handle_key_event (rfbBool      down,
		  rfbKeySym    keySym,
		  rfbClientPtr rfb_client)
{
  g_warning ("key event");
}

static void
handle_pointer_event (int          buttonMask,
		      int          x,
		      int          y,
		      rfbClientPtr rfb_client)
{
  g_warning ("pointer event");
}

static void
handle_clipboard_event (char         *str,
			int           len,
			rfbClientPtr  rfb_client)
{
  g_warning ("clipboard event");
}

static enum rfbNewClientAction
handle_new_client (rfbClientPtr rfb_client)
{
  g_warning ("new client");
  return RFB_CLIENT_ACCEPT;
}

static enum rfbNewClientAction
handle_authenticated_client (rfbClientPtr rfb_client)
{
  g_warning ("client authd");
  return RFB_CLIENT_ACCEPT;
}

static enum rfbNewClientAction
check_vnc_password (rfbClientPtr  rfb_client,
		    const char   *response,
		    int           length)
{
  g_warning ("Password is %s.", response);
  return RFB_CLIENT_ACCEPT;
}


int
main(int argc, char **argv)
{
  rfbScreenInfoPtr rfb_screen;
  GdkPixbuf *pixbuf1;

  gtk_init (&argc, &argv);

  pixbuf1 = gdk_pixbuf_new_from_file ("test1.png", NULL);

  rfb_screen = rfbGetScreen(&argc, argv,
			    gdk_pixbuf_get_width (pixbuf1),
			    gdk_pixbuf_get_height (pixbuf1),
			    gdk_pixbuf_get_bits_per_sample (pixbuf1),
			    8, 4);

  rfb_screen->desktopName = "Chicken Man";
  rfb_screen->autoPort           = FALSE;
  rfb_screen->port = 7182;
  rfb_screen->kbdAddEvent             = handle_key_event;

  rfb_screen->frameBuffer = gdk_pixbuf_get_pixels (pixbuf1);

  rfbInitServer (rfb_screen);

  printf ("Hello world.\n");
  rfbRunEventLoop(rfb_screen,
		  40000,
		  FALSE);

  return 0;
}
