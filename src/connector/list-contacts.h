#ifndef LIST_CONTACTS_H
#define LIST_CONTACTS_H 1

typedef void list_contacts_cb(gchar*, gchar*);

void list_contacts (list_contacts_cb *callback,
		    gchar *wanted_service);

#endif /* !LIST_CONTACTS_H */
