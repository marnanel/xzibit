#ifndef DOPPELGANGER_H
#define DOPPELGANGER_H 1

#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct _Doppelganger Doppelganger;

/**
 * Creates a doppelganger.  A doppelganger is
 * a mouse pointer under the control of a remote user.
 *
 * \param pixbuf  An icon representing the remote
 *                user.  It will be scaled and used
 *                as part of the new mouse pointer.
 * \param name    An internal identifier for the mouse
 *                pointer as used by XInput2.
 */
Doppelganger* doppelganger_new (GdkPixbuf *pixbuf,
				char *name);

/**
 * Moves a doppelganger around the screen.
 * If the doppelganger is invisible, it will
 * move, but you won't see it.
 *
 * \param dg  The doppelganger.
 * \param x   The X coordinate, relative to
 *            the top left corner of the screen.
 * \param y   The Y coordinate, relative to
 *            the top left corner of the screen.
 */
void doppelganger_move (Doppelganger *dg,
			int x, int y);

/**
 * Sets a doppelganger to be invisible.
 *
 * \param dg  The doppelganger.
 */
void doppelganger_hide (Doppelganger *dg);

/**
 * Sets a doppelganger to be visible.
 * This is the default.
 *
 * \param dg  The doppelganger.
 */
void doppelganger_show (Doppelganger *dg);

/**
 * Destroys a doppelganger.
 *
 * \param dg  The doppelganger.
 */
void doppelganger_free (Doppelganger *dg);

#endif /* DOPPELGANGER_H */
