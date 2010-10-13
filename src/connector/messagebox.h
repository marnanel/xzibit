#ifndef MESSAGEBOX_H
#define MESSAGEBOX_H 1

typedef void messagebox_unshare_cb(void *);

typedef struct _MessageBox MessageBox;

void show_unshare_messagebox(const char *message,
			     messagebox_unshare_cb *callback,
			     void *user_data);

/**
 * Creates a MessageBox object, in case you want
 * to keep a handle on a messagebox.
 */
MessageBox *messagebox_new(void);

/**
 * Displays a message box.
 *
 * \param box  The MessageBox object to keep
 *             track of this box; may be NULL.
 * \param message  The message to display.
 */
void messagebox_show (MessageBox *box,
		      const char *message);

/**
 * Removes a reference to a MessageBox.  This
 * may not free it, because the box may still
 * be open.
 */
void messagebox_unref (MessageBox *box);

#endif /* !MESSAGEBOX_H */
