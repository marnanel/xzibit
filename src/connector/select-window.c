/*
   Simple tool to allow selection of a window, which will then
   have sharing toggled on or off.
*/

#include <stdio.h>
#include <stdlib.h>
#include <X11/cursorfont.h>
#include "select-window.h"

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

/* EOF select-window.c */
