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

gboolean
run_rfb_event_loop (gpointer data)
{
  rfbScreenInfoPtr rfb_screen = (rfbScreenInfoPtr) data;

  rfbProcessEvents(rfb_screen,
		   40000);
  return TRUE;
}

gpointer pictures[2];
gint which_picture = 0;

gboolean
update_image (gpointer data)
{
  rfbScreenInfoPtr rfb_screen = (rfbScreenInfoPtr) data;

  which_picture = (which_picture+1)%3;

  rfb_screen->frameBuffer = pictures[which_picture];

  rfbMarkRectAsModified(rfb_screen,
			0, 0,
			rfb_screen->width,
			rfb_screen->height);

  return TRUE;
}

int
main(int argc, char **argv)
{
  rfbScreenInfoPtr rfb_screen;
  GdkPixbuf *pixbuf1, *pixbuf2, *pixbuf3;

  gtk_init (&argc, &argv);

  pixbuf1 = gdk_pixbuf_new_from_file ("test1.png", NULL);
  pixbuf2 = gdk_pixbuf_new_from_file ("test2.png", NULL);
  pixbuf3 = gdk_pixbuf_new_from_file ("test3.png", NULL);

  rfb_screen = rfbGetScreen(&argc, argv,
			    gdk_pixbuf_get_width (pixbuf1),
			    gdk_pixbuf_get_height (pixbuf1),
			    gdk_pixbuf_get_bits_per_sample (pixbuf1),
			    8, 4);

  rfb_screen->desktopName = "Chicken Man";
  rfb_screen->autoPort           = FALSE;
  rfb_screen->port = 7187;
  rfb_screen->kbdAddEvent             = handle_key_event;

  pictures[0] = gdk_pixbuf_get_pixels (pixbuf1);
  pictures[1] = gdk_pixbuf_get_pixels (pixbuf2);
  pictures[2] = gdk_pixbuf_get_pixels (pixbuf3);

  rfb_screen->frameBuffer = pictures[0];

  rfbInitServer (rfb_screen);

  printf ("Hello world.\n");

  g_timeout_add (100,
		 run_rfb_event_loop,
		 rfb_screen);

  g_timeout_add (1500,
		 update_image,
		 rfb_screen);
  
  gtk_main ();

  return 0;
}
