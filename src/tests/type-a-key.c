#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

int
main (int argc, char **argv)
{
  char *dpyid = NULL;
  Display *dpy;
  int dummy;
  int keycode;

  dpy = XOpenDisplay (dpyid);

  if (XTestQueryExtension (dpy,
			   &dummy,
			   &dummy,
			   &dummy,
			   &dummy)==False)
    {
      printf("This should not have failed.\n");
      return 1;
    }

  keycode = XKeysymToKeycode (dpy,
			      XK_c);

  XTestFakeKeyEvent (dpy,
		     keycode,
		     True,
		     CurrentTime);

  XTestFakeKeyEvent (dpy,
		     keycode,
		     False,
		     20);

  XCloseDisplay (dpy);
}
