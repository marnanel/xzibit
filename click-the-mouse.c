#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

Atom xzibit_share, cardinal;

/*
 * FIXME: Many of these functions can fail
 * and we don't currently check.
 */

static void
click_the_mouse (Display *dpy,
		 int x, int y)
{
  XTestFakeMotionEvent(dpy,
		       0 /* screen number */,
		       x, y,
		       CurrentTime);

  XTestFakeButtonEvent (dpy,
			1,
			True,
			CurrentTime);

  XTestFakeButtonEvent (dpy,
			1,
			False,
			20);
}

/*
 * Scan all descendants of "window" and call
 * click_the_mouse() on windows which are marked
 * as being sent or received over xzibit.
 * This code will probably come in useful in
 * other places as well.
 */
static int
scan_windows (Display *dpy,
	      Window window)
{
  Window dummywin;
  Window *kids;
  int n_kids, i;
  int count = 0;

  XQueryTree(dpy,
	     window,
	     &dummywin,
	     &dummywin,
	     &kids,
	     &n_kids);

  for (i=0; i<n_kids; i++)
    {
      /*
       * For each one, check whether it's
       * shared (in either direction).
       */

      Atom actual_type;
      int actual_format;
      unsigned long n_items, bytes_after;
      unsigned char *property;
      long int sharing_status;
      int x, y;
      
      /* Recur so we consider everything */
      
      count += scan_windows(dpy, kids[i]);

      /* Is it shared? */
      
      if (XGetWindowProperty(dpy,
			     kids[i],
			     xzibit_share,
			     0,
			     4,
			     False,
			     cardinal,
			     &actual_type,
			     &actual_format,
			     &n_items,
			     &bytes_after,
			     &property)!=Success)
	continue; /* try the next one */

      if (n_items==0)
	continue;

      sharing_status = *((long int*) property);
      XFree (property);

      if (sharing_status!=1 &&
	  sharing_status!=2)
	continue;

      XTranslateCoordinates(dpy,
			    kids[i],
			    DefaultRootWindow(dpy),
			    0, 0,
			    &x, &y,
			    &dummywin);

      click_the_mouse (dpy,
		       x+50,
		       y+50);

      count++;
    }

  if (kids)
    XFree(kids);

  return count;
}

int
main (int argc, char **argv)
{
  char *dpyid = NULL;
  Display *dpy;
  int dummy;
  int x, y;

  dpy = XOpenDisplay (dpyid);

  xzibit_share = XInternAtom(dpy,
			     "_XZIBIT_SHARE",
			     False);
  cardinal = XInternAtom(dpy,
			 "CARDINAL",
			 False);

  if (XTestQueryExtension (dpy,
			   &dummy,
			   &dummy,
			   &dummy,
			   &dummy)==False)
    {
      printf("This should not have failed.\n");
      return 1;
    }

  if (scan_windows (dpy,
		    DefaultRootWindow(dpy))==0)
    {
      printf("Warning: no windows were clicked on\n");
    }

  XCloseDisplay (dpy);
}
