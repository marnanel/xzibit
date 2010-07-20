#include "vnc.h"
#include <gtk/gtk.h>
#include <rfb/rfbproto.h>
#include <rfb/rfb.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>


typedef struct _VncPrivate {
  /* FIXME: This is also rfb_screen->port;
     don't store it in both places */
  int port;
  int width, height;
  GdkWindow *window;
  GdkPixbuf *screenshot;
  rfbScreenInfoPtr rfb_screen;
} VncPrivate;

GHashTable *servers = NULL;

/*
 * We take note of the time and serial of each
 * X event, and reuse them to make our fake events
 * more convincing.
 */
int vnc_latestTime = 0;
int vnc_latestSerial = 0;

static void
ensure_servers (void)
{
  if (servers)
    return;

  servers = g_hash_table_new_full (g_int_hash,
				   g_int_equal,
				   g_free,
				   g_free);
}

static void
fakeMouseClick (GdkWindow *window,
		int x, int y,
		gboolean is_press)
{
  Display *display = gdk_x11_get_default_xdisplay();
  int ox, oy;

  gdk_window_get_origin (window,
			 &ox, &oy);

  g_warning ("Faking click at %d,%d", x, y);

  XTestFakeMotionEvent (display,
			0, /* FIXME use the proper screen number */
			ox+x, oy+y,
			CurrentTime);
  XTestFakeButtonEvent (display,
			1, is_press, 100);

  /* we don't seem to get a release event, so... */

  XTestFakeButtonEvent (display,
			1, False, 200);

}

static gboolean
run_rfb_event_loop (gpointer data)
{
  VncPrivate *private = (VncPrivate*) data;

  /* FIXME: We really want to snoop on what the
     compositor already knows
  */

  GdkPixbuf *screenshot =
    gdk_pixbuf_get_from_drawable (NULL,
				  private->window,
				  gdk_colormap_get_system (),
				  0, 0, 0, 0,
				  private->width,
				  private->height);

  if (screenshot==NULL)
    {
      g_warning ("Screenshot was null; bailing");
      return TRUE;
    }

  if (private->screenshot)
    g_object_unref (private->screenshot);

  private->screenshot = gdk_pixbuf_add_alpha (screenshot,
					   FALSE, 0, 0, 0);

  private->rfb_screen->frameBuffer = gdk_pixbuf_get_pixels (private->screenshot);

  rfbMarkRectAsModified(private->rfb_screen,
			0, 0,
			gdk_pixbuf_get_width (screenshot),
			gdk_pixbuf_get_height (screenshot));

  g_object_unref (screenshot);

  rfbProcessEvents(private->rfb_screen,
		   40000);

  return TRUE;
}

static void
handle_mouse_event (int buttonMask,
		    int x, int y,
		    struct _rfbClientRec* cl)
{
  VncPrivate *private = (VncPrivate*) cl->screen->screenData;

  if (buttonMask==0)
    return; /* just a move; we currently ignore this */

  fakeMouseClick (private->window,
		  x, y,
		  True);
}

void
vnc_start (Window id)
{
  VncPrivate *private = NULL;
  int *key;
  Window root;
  int x, y, width, height, border_width, depth;

  ensure_servers ();

  private = g_hash_table_lookup (servers,
				 &id);

  if (private)
    return;

  XGetGeometry (gdk_x11_get_default_xdisplay (),
		id,
		&root,
		&x, &y,
		&width, &height,
		&border_width,
		&depth);

  g_warning ("Starting VNC server for %08x", (unsigned int) id);

  key = g_malloc (sizeof(int));
  *key = id;

  private = g_malloc (sizeof(VncPrivate));
  private->port = (random() % 60000) + 1024;
  private->width = width;
  private->height = height;
  private->window = gdk_window_foreign_new (id);
  private->screenshot = NULL;

  private->rfb_screen = rfbGetScreen(/* we don't supply argc and argv */
				     0, NULL,
				     width,
				     height,
				     /* FIXME don't hardcode these */
				     8, 1, 4);

  private->rfb_screen->desktopName = "Chicken Man"; /* FIXME */
  private->rfb_screen->autoPort = FALSE;
  private->rfb_screen->port = private->port;
  private->rfb_screen->frameBuffer = NULL;

  rfbInitServer (private->rfb_screen);

  private->rfb_screen->screenData = private;
  private->rfb_screen->ptrAddEvent = handle_mouse_event;

  /*
  private->rfb_screen->kbdAddEvent             = handle_key_event;
  */

  g_warning ("(on port %d)", private->port);

  g_hash_table_insert (servers,
		       key,
		       private);

  g_timeout_add (100,
		 run_rfb_event_loop,
		 private);
}

unsigned int
vnc_port (Window id)
{
  VncPrivate *private = NULL;

  if (!servers)
    return 0;

  private = g_hash_table_lookup (servers,
				 &id);
  
  if (private)
    return private->port;
  else
    return 0;
}

void
vnc_supply_pixmap (Window id,
		   GdkPixbuf *pixbuf)
{
  g_warning ("Pixmap supplied for %08x", (unsigned int) id);

  /* not implemented */
}

void
vnc_stop (Window id)
{
  g_warning ("Stopping VNC server for %08x", (unsigned int) id);

  /* not implemented */
}

/* eof vnc.c */

