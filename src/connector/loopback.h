#ifndef LOOPBACK_H
#define LOOPBACK_H 1

#include "list-contacts.h"

/**
 * Finds a pair of accounts, both of which you are
 * currently logged into, and both of which have
 * the other on their contacts roster.
 *
 * \param callback  Called with the details of
 *                  such accounts, or with its
 *                  first three parameters NULL
 *                  if none such exists; unlike with
 *                  list_contacts(), this will
 *                  always only be called exactly once.
 * \param wanted_service  Service we're looking
 *                        for both sides to support,
 *                        such as "x-xzibit".
 * \param user_data  Arbitrary data to pass to
 *                   the callback.
 */
void find_loopback (list_contacts_cb callback,
		    gchar *wanted_service,
		    gpointer user_data);

#endif /* LOOPBACK_H */
