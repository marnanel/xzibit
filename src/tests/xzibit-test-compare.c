#include <gdk/gdkx.h>
#include <stdlib.h>

int sent_window_count = 0;
Window sent_window_id = None;
int received_window_count = 0;
Window received_window_id = None;

gboolean verbose = TRUE;

static int
check_for_sent_and_received_windows_tail (Window window)
{
  Window root, parent;
  Window *kids;
  Window found = 0;
  int window_is_shared;
  Atom actual_type;
  int actual_format;
  long n_items, bytes_after;
  unsigned char *prop_return;
  unsigned int n_kids, i;

  /*
   * If this window has kids, check them first.
   */

  if (XQueryTree (gdk_x11_get_default_xdisplay (),
		  window,
		  &root,
		  &parent,
		  &kids,
		  &n_kids)==0)
    {
      return 0;
    }

  for (i=0; i<n_kids; i++)
    {
      check_for_sent_and_received_windows_tail (kids[i]);
    }

  if (kids)
    {
      XFree (kids);
    }

  /*
   * Now check this window itself.
   */

  XGetWindowProperty(gdk_x11_get_default_xdisplay (),
		     window,
		     gdk_x11_get_xatom_by_name("_XZIBIT_SHARE"),
		     0, 4, False,
		     gdk_x11_get_xatom_by_name("CARDINAL"),
		     &actual_type,
		     &actual_format,
		     &n_items,
		     &bytes_after,
		     &prop_return);

  /* Default is zero. */
  window_is_shared = 0;

  if (prop_return) {
    window_is_shared = *((int*) prop_return);
    XFree (prop_return);
  }

  /* Now, what have we learned? */

  switch (window_is_shared)
    {
    case 1: /* Sent */
      sent_window_count++;
      sent_window_id = window;
      break;

    case 2: /* Received */
      received_window_count++;
      received_window_id = window;
      break;
    }
}

static int
check_for_sent_and_received_windows (void)
{
  return check_for_sent_and_received_windows_tail (gdk_x11_get_default_root_xwindow ());
}

static void
usage_message (void)
{
  g_print ("xzibit-test-compare : compares sent and received windows\n");
  g_print ("If there is one sent and one received window, we will compare those.\n");
  g_print ("If there is one sent and no received windows,\n");
  g_print ("we will wait for the received window to appear.\n");
  g_print ("Otherwise, you'll get this message.\n");
  g_print ("Use the --help option to get a list of possible points of comparison.\n");

  exit (255);
}

int
main(int argc, char **argv)
{
  gtk_init (&argc, &argv);

  check_for_sent_and_received_windows ();

  if (sent_window_count==1 && received_window_count==1)
    {
      /* ideal; we know what we're dealing with */
      g_warning ("Not implemented: run actual comparisons");
    }
  else if (sent_window_count==1 && received_window_count==0)
    {
      /*
       * a window has been sent; none has been retrieved;
       * block on it
       */
      g_warning ("Not implemented: wait for received");
    }
  else
    {
      g_print ("Count of sent windows: %d.  Count of received windows: %d.\n\n",
	       sent_window_count,
	       received_window_count);
      usage_message ();
    }

  return 0;
}

/* EOF xzibit-test-compare.c */
