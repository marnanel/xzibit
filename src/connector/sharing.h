#ifndef SHARING_H
#define SHARING_H

#include <glib.h>
#include <X11/Xlib.h>

/**
 * Finds whether a window on the current display
 * is shared.
 * 0 means it is not shared;
 * 1 means it is shared;
 * 2 means it's being received;
 * 3 means it's unshareable.
 * The answer is only a rule of thumb, because
 * it's possible to set an incorrect value.  For
 * example, you may set a window's sharing to 1,
 * but the recipient may reject it.  The sharing
 * state is not reset if this happens.
 *
 * \param window  The X ID of a window on the
 *                current display.
 * \result  The sharing state, between 0 and 3.
 */
guint window_get_sharing (Window window);

/**
 * Sets whether a window on the current display
 * is shared.
 *
 * \param window  The X ID of a window on the
 *                current display.
 * \param sharing 0 to un-share the window;
 *                1 to share it;
 *                3 to make this window
 *                unshareable.
 * \param source  The DBus address of the
 *                account which is sending
 *                this window.  Only used if
 *                sharing==1; must be NULL otherwise.
 * \param target  The address of the contact
 *                who should receive this
 *                window.  Only used if
 *                sharing==1; must be NULL otherwise.
 * \param follow_up  If TRUE, the window will be
 *                   monitored for a response from
 *                   xzibit, and dialogue boxes will
 *                   be created as appropriate.
 *                   (You usually want this to be TRUE.
 *                   It should be FALSE only on
 *                   the dialogues themselves, to
 *                   prevent an infinite loop.
 */
void window_set_sharing (Window window,
			 guint sharing,
			 const char* source,
			 const char* target,
			 gboolean follow_up);

#endif /* !SHARING_H */
