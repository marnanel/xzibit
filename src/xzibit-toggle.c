/*
   Simple tool to allow selection of a window, which will then
   have sharing toggled on or off.
*/

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>

/* Based on code from xorg, which is:
Copyright 1993, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.
*/

Window
Select_Window(Display *dpy, int screen)
{
int status;
Cursor cursor;
XEvent event;
Window target_win = None, root = RootWindow(dpy,screen);
int buttons = 0;

/* Make the target cursor */
cursor = XCreateFontCursor(dpy, XC_crosshair);

/* Grab the pointer using target cursor, letting it roam all over */
status = XGrabPointer(dpy, root, False,
        ButtonPressMask|ButtonReleaseMask, GrabModeSync,
        GrabModeAsync, root, cursor, CurrentTime);
if (status != GrabSuccess) {
    fprintf (stderr, "Can't grab the mouse.\n");
    exit (1);
}

/* Let the user select a window... */
while ((target_win == None) || (buttons != 0)) {
    /* allow one more event */
    XAllowEvents(dpy, SyncPointer, CurrentTime);
    XWindowEvent(dpy, root, ButtonPressMask|ButtonReleaseMask, &event);
    switch (event.type) {
        case ButtonPress:
            if (target_win == None) {
                target_win = event.xbutton.subwindow; /* window selected */
                if (target_win == None) target_win = root;
            }
            buttons++;
            break;
        case ButtonRelease:
            if (buttons > 0) /* there may have been some down before we started */
                buttons--;
            break;
    }
}

XUngrabPointer(dpy, CurrentTime);      /* Done with pointer */

return target_win;
}

int
main (int argc, char **argv)
{
Display *dpy;
Window target;
Atom _xzibit_share, cardinal;
int window_is_shared;
Atom actual_type;
int actual_format;
long n_items, bytes_after;
unsigned char *prop_return;
Window root, parent, *kids;
int n_kids;

dpy = XOpenDisplay (NULL);

_xzibit_share = XInternAtom (dpy,
        "_XZIBIT_SHARE",
        False);

cardinal = XInternAtom (dpy,
        "CARDINAL",
        False);

target = Select_Window (dpy, 0);

if (XQueryTree (dpy, target,
            &root,
            &parent,
            &kids,
            &n_kids)==0)
  {
    fprintf (stderr, "Couldn't examine that window.\n");
    exit (1);
  }

if (target==root)
  {
    fprintf (stderr, "Can't share the root window.\n");
    exit (1);
  }

if (parent==root && n_kids==1)
  {
    /* Must be the frame */
    target = kids[0];
  }

if (kids)
  {
    XFree (kids);
  }

    XGetWindowProperty(dpy, target,
            _xzibit_share,
            0, 4, False,
            cardinal,
            &actual_type,
            &actual_format,
            &n_items,
            &bytes_after,
            &prop_return);

    window_is_shared = 0;

    if (prop_return) {
        window_is_shared = *((int*) prop_return);
        XFree (prop_return);
    }

    if (window_is_shared==0 || window_is_shared==1)
      {

        window_is_shared = 1 - window_is_shared;

        printf ("Setting %d to %d on %x\n", _xzibit_share, window_is_shared, target);
        XChangeProperty (dpy, target,
                _xzibit_share,
                cardinal,
                32,
                PropModeReplace,
                (const unsigned char*) &window_is_shared,
                1);
      }


    XCloseDisplay (dpy);
    return 0;
}
