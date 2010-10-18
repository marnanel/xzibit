#ifndef GET_AVATAR_H
#define GET_AVATAR_H 1

#include <glib.h>

/**
 * Returns an avatar to use to represent
 * the current user.
 *
 * \result  The avatar, PNG-encoded.  The
 *          string may be zero-length if we
 *          could not find one.
 */
GString* get_avatar(void);

#endif /* !GET_AVATAR */
