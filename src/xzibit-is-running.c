#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdlib.h>
#include <X11/Xlib.h>

static gboolean
no_response (gpointer user_data)
{
  g_print ("No timely response from xzibit.\n");
  exit(255);
}

static GdkFilterReturn
event_filter (GdkXEvent *xevent,
	      GdkEvent *event,
	      gpointer user_data)
{
  XEvent *ev = (XEvent*) xevent;

  g_print ("Event filter %d %d\n", ev->type, PropertyNotify);

  if (ev->type == PropertyNotify)
    {
      XPropertyEvent *propev =
	(XPropertyEvent*) xevent;

      g_print ("Event filter is %s %d %d %x\n",
	       gdk_x11_get_xatom_name(propev->atom),
	       propev->atom,
	       gdk_x11_get_xatom_by_name("_XZIBIT_RESULT"),
	       propev->window);

      if (propev->atom == gdk_x11_get_xatom_by_name("_XZIBIT_RESULT") &&
	  propev->state == PropertyNewValue)
	{
	  /* we have a winner! */
	  g_print ("xzibit is running.\n");
	  exit (0);
	}
    }

  return GDK_FILTER_CONTINUE;
}

static gboolean
mark_unshared (gpointer user_data)
{
  unsigned int no_share = 0;

  XChangeProperty (gdk_x11_get_default_xdisplay (),
		   gdk_x11_get_default_root_xwindow (),
		   gdk_x11_get_xatom_by_name("_XZIBIT_SHARE"),
		   gdk_x11_get_xatom_by_name("CARDINAL"),
		   32,
		   PropModeReplace,
		   (const unsigned char*) &no_share,
		   1);

  return FALSE;
}

int
main (int argc, char **argv)
{
  gtk_init (&argc, &argv);

  gdk_window_add_filter (NULL, /*gdk_get_default_root_window (),*/
			 event_filter,
			 NULL);

  g_timeout_add (50,
		 mark_unshared,
		 NULL);

  g_timeout_add (500,
		 no_response,
		 NULL);

  gtk_main ();
}
