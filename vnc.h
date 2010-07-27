#ifndef VNC_H
#define VNC_H 1

#include <X11/X.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

extern int vnc_latestTime;
extern int vnc_latestSerial;

/**
 * Creates a new VNC server for the given X ID.
 * If there is already a VNC server for the given ID,
 * does nothing.
 */
void vnc_start (Window id);

/**
 * Returns the port number which the server for the
 * given X ID is listening on.  If there is no server
 * for the given X ID, returns 0.
 */
unsigned int vnc_port (Window id);

/**
 * Supplies a pixmap to the VNC server for the
 * given X ID.  If there is no VNC server for the
 * given X ID, creates it first.
 */
void vnc_supply_pixmap (Window id,
			GdkPixbuf *pixbuf);

/**
 * Closes the VNC server for the given X ID.
 * If there is no VNC server for the given X ID,
 * does nothing.
 */
void vnc_stop (Window id);

extern char* window_types[][2];

#define XZIBIT_METADATA_TRANSIENCY 1
#define XZIBIT_METADATA_NAME 2
#define XZIBIT_METADATA_TYPE 3
#define XZIBIT_METADATA_ICON 4

#endif /* !VNC_H */

/* eof vnc.h */
