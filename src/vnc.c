#include "vnc.h"
#include <gtk/gtk.h>
#include <rfb/rfbproto.h>
#include <rfb/rfb.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XInput.h>

/*
 * Define this if your X server is a bit crappy
 * and gets XTest with XInput2 wrong.
 *
 * This is more likely to work, but fails if
 * the window isn't focussed (so "key" will
 * pass, but "keybehind" will fail).
 *
 * (If you're not sure, run the unit tests.)
 */
#define USE_OLD_XTEST

/**
 * The codes for various types of window,
 * mapped to their naes in the EWMH spec.
 */
char* window_types[][2] = {
  {"B", "_NET_WM_WINDOW_TYPE_TOOLBAR"},
  {"C", "_NET_WM_WINDOW_TYPE_COMBO"},
  {"D", "_NET_WM_WINDOW_TYPE_DIALOG"},
  {"M", "_NET_WM_WINDOW_TYPE_MENU"},
  {"N", "_NET_WM_WINDOW_TYPE_NOTIFICATION"},
  {"P", "_NET_WM_WINDOW_TYPE_POPUP_MENU"},
  {"R", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"},
  {"S", "_NET_WM_WINDOW_TYPE_SPLASH"},
  {"T", "_NET_WM_WINDOW_TYPE_TOOLTIP"},
  {"U", "_NET_WM_WINDOW_TYPE_UTILITY"},
  {"X", "_NET_WM_WINDOW_TYPE_NORMAL"},
  {0, 0}
};


typedef struct _VncPrivate {
  int fd;
  int other_fd;
  int width, height;
  GdkWindow *window;
  GdkPixbuf *screenshot;
  int screenshot_checksum;
  gboolean screenshot_checksum_valid;
  rfbScreenInfoPtr rfb_screen;
  XDevice *xtest_pointer;
  XDevice *xtest_keyboard;
  int master_pointer;
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

  g_warning ("Which is really %d,%d", ox+x, oy+y);

#ifdef USE_OLD_XTEST

  XTestFakeMotionEvent (display,
			0, /* FIXME use the proper screen number */
			ox+x, oy+y,
			CurrentTime);
  XTestFakeButtonEvent (display,
			1,
			True,
			CurrentTime);

  /* we don't seem to get a release event, so... */

  XTestFakeButtonEvent (display,
			1, False, 200);

#else

#error "XInput2 for mouse not yet implemented"

#endif

}

static gboolean
run_rfb_event_loop (gpointer data)
{
  VncPrivate *private = (VncPrivate*) data;
  int checksum = 0, pixelcount, i;
  char *pixels;

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

  pixels = gdk_pixbuf_get_pixels (screenshot);
  pixelcount = gdk_pixbuf_get_width(screenshot) * ((gdk_pixbuf_get_n_channels(screenshot) * gdk_pixbuf_get_bits_per_sample(screenshot)+7)/8);
  pixelcount += (gdk_pixbuf_get_height(screenshot)-1) * gdk_pixbuf_get_rowstride(screenshot);
  for (i=0; i < pixelcount; i++) {
    checksum += pixels[i];
  }

  if (!private->screenshot_checksum_valid || checksum != private->screenshot_checksum)
    {
      private->screenshot_checksum = checksum;
      private->screenshot_checksum_valid = TRUE;

      if (private->screenshot)
	{
	  g_object_unref (private->screenshot);
	}

      private->screenshot = gdk_pixbuf_add_alpha (screenshot,
						  FALSE, 0, 0, 0);

      private->rfb_screen->frameBuffer = gdk_pixbuf_get_pixels (private->screenshot);

      rfbMarkRectAsModified(private->rfb_screen,
			    0, 0,
			    gdk_pixbuf_get_width (screenshot),
			    gdk_pixbuf_get_height (screenshot));

      g_object_unref (screenshot);

    }

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

static void
handle_keyboard_event (rfbBool down,
		       rfbKeySym keySym,
		       struct _rfbClientRec* cl)
{
  VncPrivate *private = (VncPrivate*) cl->screen->screenData;
  Display *display = gdk_x11_get_default_xdisplay();
  int current_pointer;
  int count;
  int keycode;
  int dummy;
  int axes[1] = { 0 };

  /* FIXME: vino caches these, and we should too */
  keycode = XKeysymToKeycode (display, keySym);

  g_print ("Code is %x and sym was %x\n", keycode, keySym);

  /*
  g_warning ("Key event: %s, %x, on %d\n",
	     down?"down":"up",
	     keySym,
	     private->xtest_keyboard->device_id);
  */

  /* Possibly we don't need to do all this
   * on EVERY keystroke.
   */

  /* Make absolutely sure we have initialised everything */
  
  if (XTestQueryExtension (gdk_x11_get_default_xdisplay(),
			   &dummy,
			   &dummy,
			   &dummy,
			   &dummy)==False)
    {
      g_print("This should not have failed.\n");
    }

  XSetInputFocus (display,
		  GDK_WINDOW_XID (private->window),
		  RevertToNone,
		  CurrentTime);


#ifdef USE_OLD_XTEST

  XTestFakeKeyEvent (display,
		     keycode,
		     down,
		     0);

#else

  if (!XQueryExtension(gdk_x11_get_default_xdisplay (), "XInputExtension", &dummy, &dummy, &dummy)) {
    g_print("X Input extension not available.\n");
  }

  int major = 2, minor = 0;
  if (XIQueryVersion(gdk_x11_get_default_xdisplay (), &major, &minor) == BadRequest) {
    g_print("XI2 not available. Server supports %d.%d\n", major, minor);
  }

  XIGetClientPointer (gdk_x11_get_default_xdisplay (),
		      GDK_WINDOW_XID (private->window),
		      &current_pointer);
  
  XISetClientPointer (gdk_x11_get_default_xdisplay (),
		      GDK_WINDOW_XID (private->window),
		      private->master_pointer);

  XSetInputFocus (display,
		  GDK_WINDOW_XID (private->window),
		  RevertToNone,
		  CurrentTime);


  XTestFakeDeviceKeyEvent (display,
			   private->xtest_keyboard,
			   keycode,
			   down,
			   &axes, 0,
			   0);
  XISetClientPointer (gdk_x11_get_default_xdisplay (),
		      GDK_WINDOW_XID (private->window),
		      current_pointer);

#endif
}

static void
add_mpx_for_window (Window window, VncPrivate *private)
{
  XIAddMasterInfo add;
  gchar *name = g_strdup_printf("xzibit-p-%07x",
				(int) window);
  int ndevices;
  XIDeviceInfo *devices, *device;
  int i;
  int current_pointer;

  private->master_pointer = 0;
  private->xtest_pointer = NULL;
  private->xtest_keyboard = NULL;

  /* add the device */

  add.type = XIAddMaster;
  add.name = name;
  add.send_core = True;
  add.enable = True;

  g_warning ("Adding new pointer called %s", name);

  XIChangeHierarchy (gdk_x11_get_default_xdisplay (),
		     (XIAnyHierarchyChangeInfo*) &add, 1);

  /* now see whether it's in the list */

  devices = XIQueryDevice(gdk_x11_get_default_xdisplay (),
			  XIAllDevices, &ndevices);

  for (i = 0; i < ndevices; i++) {
    device = &devices[i];

    if (g_str_has_prefix (device->name,
			  name))
      {
	switch (device->use)
	  {
	  case XISlavePointer:
	    private->xtest_pointer = XOpenDevice (gdk_x11_get_default_xdisplay (),
						  device->deviceid);
	    break;

	  case XISlaveKeyboard:
	    private->xtest_keyboard = XOpenDevice (gdk_x11_get_default_xdisplay (),
						   device->deviceid);
	    break;

	  case XIMasterPointer:
	    private->master_pointer = device->deviceid;
	    break;
	  }
      }
  }

  XIFreeDeviceInfo(devices);
  g_free (name);
}

void
vnc_create (Window id)
{
  VncPrivate *private = NULL;
  int *key;
  int sockets[2];

  ensure_servers ();

  if (servers)
    {
      private = g_hash_table_lookup (servers,
				     &id);
    }

  if (private)
    return;

  g_warning ("Creating VNC server for %08x", (unsigned int) id);

  key = g_malloc (sizeof(int));
  *key = id;

  socketpair (AF_LOCAL, SOCK_STREAM, 0, sockets);

  private = g_malloc (sizeof(VncPrivate));
  private->fd = sockets[0];
  private->other_fd = sockets[1];
  private->width = private->height = 0;

  g_hash_table_insert (servers,
		       key,
		       private);
}

void
vnc_start (Window id)
{
  VncPrivate *private = NULL;
  Window root;
  int x, y, width, height, border_width, depth;

  g_warning ("Starting VNC server for %08x", (unsigned int) id);

  private = g_hash_table_lookup (servers,
				 &id);

  if (!private)
    {
      g_warning ("Attempt to start %x which has not been created",
		 (unsigned int) id);
      return;
    }

  if (private->width != 0)
    {
      g_warning ("Attempt to start %x which has already been started",
		 (unsigned int) id);
      return;
    }

  XGetGeometry (gdk_x11_get_default_xdisplay (),
		id,
		&root,
		&x, &y,
		&width, &height,
		&border_width,
		&depth);

  private->width = width;
  private->height = height;
  g_warning ("Window is %dx%d", width, height);
  private->window = gdk_window_foreign_new (id);
  private->screenshot = NULL;
  private->screenshot_checksum = 0;
  private->screenshot_checksum_valid = FALSE;

  add_mpx_for_window (id, private);

  private->rfb_screen = rfbGetScreen(/* we don't supply argc and argv */
				     0, NULL,
				     width,
				     height,
				     /* FIXME don't hardcode these */
				     8, 1, 4);

  private->rfb_screen->desktopName = "Chicken Man"; /* FIXME */
  private->rfb_screen->autoPort = FALSE;
  private->rfb_screen->port = 0;
  private->rfb_screen->fdFromParent = private->other_fd;
  private->rfb_screen->frameBuffer = NULL;

  rfbInitServer (private->rfb_screen);

  private->rfb_screen->screenData = private;
  private->rfb_screen->ptrAddEvent = handle_mouse_event;
  private->rfb_screen->kbdAddEvent = handle_keyboard_event;

  g_timeout_add (100,
		 run_rfb_event_loop,
		 private);

}

int
vnc_fd (Window id)
{
  VncPrivate *private = NULL;

  if (!servers)
    return -1;

  private = g_hash_table_lookup (servers,
				 &id);

  if (private)
    return private->fd;
  else
    return -1;
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

