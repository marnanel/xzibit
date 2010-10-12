#include "sharing.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <string.h>

guint
window_get_sharing (Window window)
{
  Atom actual_type;
  int actual_format;
  long n_items, bytes_after;
  unsigned char *prop_return;
  guint result = 0;

  XGetWindowProperty (gdk_x11_get_default_xdisplay (),
		      window,
		      gdk_x11_get_xatom_by_name ("_XZIBIT_SHARE"),
		      0, 4, False,
		      gdk_x11_get_xatom_by_name ("CARDINAL"),
		      &actual_type,
		      &actual_format,
		      &n_items,
		      &bytes_after,
		      &prop_return);

  if (prop_return)
    {
      result = *((int*) prop_return);
      XFree (prop_return);
    }

  return result;
}

void
window_set_sharing (Window window,
		    guint sharing,
		    const char* source,
		    const char* target)
{
  if (sharing>3)
    {
      g_error ("Attempted to set sharing to %d on %x, "
	       "which is out of range.",
	       sharing, (int) window);
    }

  if (sharing==1)
    {
      if (!source && !target)
	{
	  g_error ("Must set source and target when "
		   "sharing a window.");
	}
    }
  else
    {
      if (source || target)
	{
	  g_error ("Must not supply source or target "
		   "unless sharing a window.");
	}
    }

  if (source)
    {
      XChangeProperty (gdk_x11_get_default_xdisplay (),
		       window,
		       gdk_x11_get_xatom_by_name("_XZIBIT_SOURCE"),
		       gdk_x11_get_xatom_by_name("UTF8_STRING"),
		       8,
		       PropModeReplace,
		       (const unsigned char*) source,
		       strlen(source));

    }

  if (target)
    {
      XChangeProperty (gdk_x11_get_default_xdisplay (),
		       window,
		       gdk_x11_get_xatom_by_name("_XZIBIT_TARGET"),
		       gdk_x11_get_xatom_by_name("UTF8_STRING"),
		       8,
		       PropModeReplace,
		       (const unsigned char*) target,
		       strlen(source));

    }

  XChangeProperty (gdk_x11_get_default_xdisplay (),
		   window,
		   gdk_x11_get_xatom_by_name ("_XZIBIT_SHARE"),
		   gdk_x11_get_xatom_by_name ("CARDINAL"),
		   32,
		   PropModeReplace,
		   (const unsigned char*) &sharing,
		   1);
}

#ifdef SHARING_TEST

static gboolean
timeout (gpointer user_data)
{
  static int stage = 0;

  GtkWidget *window =
    (GtkWidget*) user_data;
  Window id =
    GDK_WINDOW_XID (window->window);

  switch (++stage)
    {
    case 1:
    case 3:
      g_print ("State of window %x is %d\n",
	       (int) id, window_get_sharing (id));
      break;

    case 2:
      g_print ("Sharing the window.\n");
      window_set_sharing (id, 1,
			  "source@example.com",
			  "target@example.com");
      break;

    case 4:
      return FALSE;
    }

  return TRUE;
}

int
main (int argc, char **argv)
{
  GtkWidget *window;

  gtk_init (&argc, &argv);
  
  window =
    gtk_window_new (GTK_WINDOW_TOPLEVEL);

  g_signal_connect (window,
		    "delete-event",
		    G_CALLBACK (gtk_main_quit),
		    NULL);

  gtk_widget_show_all (window);

  g_timeout_add (1000,
		 timeout,
		 window);

  gtk_main ();
}

#endif /* !SHARING_TEST */

/* EOF sharing.c */
