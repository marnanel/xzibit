#include "messagebox.h"

#include <gtk/gtk.h>

#define _(x) (x)

typedef struct _UnshareContext {
  messagebox_unshare_cb *callback;
  void *user_data;
} UnshareContext;

struct _MessageBox {
  int ref_count;
  GtkDialog *box;
};

static void
handle_response (GtkDialog *dialogue,
		 gint response_id,
		 gpointer user_data)
{
  UnshareContext *context =
    (UnshareContext*) user_data;

  if (response_id==GTK_RESPONSE_ACCEPT)
    {
      context->callback (context->user_data);
    }

  gtk_widget_destroy (GTK_WIDGET (dialogue));

  g_free (context);
}

void
show_unshare_messagebox(const char *message,
			messagebox_unshare_cb *callback,
			void *user_data)
{
  GtkWidget *window =
    gtk_dialog_new_with_buttons (_("Xzibit"),
				 NULL,
				 GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_MODAL,
				 _("Un-share"),
				 GTK_RESPONSE_ACCEPT,
				 GTK_STOCK_CANCEL,
				 GTK_RESPONSE_REJECT,
				 NULL);

  GtkWidget *label =
    gtk_label_new (message);
  UnshareContext *context =
    g_malloc (sizeof (UnshareContext));

  context->callback = callback;
  context->user_data = user_data;

  gtk_container_add (GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(window))),
		     label);

  g_signal_connect (GTK_DIALOG (window),
		    "response",
		    G_CALLBACK (handle_response),
		    context);

  gtk_widget_show_all (window);
}

void
show_messagebox(const char* message)
{
  g_warning ("This would be a message box: %s",
	     message);
}

MessageBox*
messagebox_new (void)
{
  MessageBox *result =
    g_malloc (sizeof (MessageBox));

  result->ref_count = 1;
  result->box = NULL;

  return result;
}

void
messagebox_unref (MessageBox *box)
{
  if (--box->ref_count <= 0)
    {
      g_warning ("Destroying messagebox %p",
		 box);
      g_free (box);
    }
}

#ifdef MESSAGEBOX_TEST

static void
unshare_callback (gpointer user_data)
{
  g_print ("Here we would unshare %s.\n",
	   (char*) user_data);
}

int
main(int argc, char **argv)
{
  MessageBox *box;

  gtk_init (&argc, &argv);

#if 0  
  show_unshare_messagebox ("That window is already shared. "
			   "Would you like to unshare it?",
			   unshare_callback,
			   "that window");
#else

  box = messagebox_new ();

  messagebox_unref (box);

#endif

  gtk_main ();
}

#endif /* MESSAGEBOX_TEST */

/* EOF messagebox.c */

