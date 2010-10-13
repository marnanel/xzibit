#ifndef MESSAGEBOX_H
#define MESSAGEBOX_H 1

typedef void messagebox_unshare_cb(void *);

void show_unshare_messagebox(const char *message,
			     messagebox_unshare_cb *callback,
			     void *user_data);

void show_messagebox(const char* message);

#endif /* !MESSAGEBOX_H */
