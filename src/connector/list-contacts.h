/*
 * list-contacts - subsystem to list all contacts with
 *                 given caps
 *
 * Copyright (C) 2010 Collabora Ltd.
 * Based on telepathy-ssh:
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef LIST_CONTACTS_H
#define LIST_CONTACTS_H 1

#include <glib.h>

/*
 * TO DO: Might be useful to have a specialised version
 * of list_contacts() which only listed pairs where you
 * were logged in from both the source account and the
 * contact, so we could have a "loopback" button for testing.
 */

/**
 * Callback for list_contacts.  This will be called with
 * the source path, the source JID, and the target JID
 * in that order.
 * When all possibilities are exhausted, it will be
 * called one final time with all set to NULL.
 */
typedef void list_contacts_cb(const gchar*, const gchar*,
			      const gchar*,
			      void *);

/**
 * Lists all (account, contact) pairs where both sides
 * have a given capability.
 *
 * \param callback  A callback which receives the
 *                  pairs.  See above for details.
 * \param wanted_service  The service you're looking for,
 *                        such as "x-xzibit".
 */
void list_contacts (list_contacts_cb *callback,
		    gchar *wanted_service,
		    gpointer user_data);

#endif /* !LIST_CONTACTS_H */
