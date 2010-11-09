#include <gdk/gdkx.h>

int sent_window_count = 0;
Window sent_window_id = None;
int received_window_count = 0;
Window received_window_id = None;

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

int
main(int argc, char **argv)
{
  gtk_init (&argc, &argv);

  check_for_sent_and_received_windows ();

  printf ("Sent: %d; received %d\n",
	  sent_window_count,
	  received_window_count);

  return 0;
}

/* EOF xzibit-test-compare.c */
