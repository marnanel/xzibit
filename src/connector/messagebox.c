#include "messagebox.h"
#include "sharing.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#define _(x) (x)

typedef struct _UnshareContext {
  messagebox_unshare_cb *callback;
  void *user_data;
} UnshareContext;

struct _MessageBox {
  int ref_count;
  GtkDialog *box;
  GtkWidget *label;
};

static void
unshare_response (GtkDialog *dialogue,
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
		    G_CALLBACK (unshare_response),
		    context);

  gtk_widget_show_all (window);
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
  if (box==NULL)
    return;

  if (--box->ref_count <= 0)
    {
      g_free (box);
    }
}

static void
messagebox_response (GtkDialog *dialogue,
		     gint response_id,
		     gpointer user_data)
{
  MessageBox *box =
    (MessageBox*) user_data;

  messagebox_unref (box);

  gtk_widget_destroy (GTK_WIDGET (dialogue));
}

void
messagebox_show (MessageBox *box,
		 const char *message)
{
  GtkDialog *dialogue = NULL;
  GtkWidget *label = NULL;

  if (box)
    {
      if (box->box==NULL)
	{
	  /*
	   * Increment the refcount because we're
	   * about to display a dialogue.
	   */
	  box->ref_count++;
	}
      dialogue = box->box;
      label = box->label;
    }

  if (dialogue == NULL)
    {
      GtkWidget *vbox;
      GdkGeometry geometry;

      dialogue = 
	GTK_DIALOG (gtk_dialog_new_with_buttons (_("Xzibit"),
						 NULL,
						 GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_MODAL,
						 GTK_STOCK_OK,
						 GTK_RESPONSE_ACCEPT,
						 NULL));

      geometry.min_width = geometry.max_width = 400;
      geometry.min_height = geometry.max_height = 100;

      gtk_window_set_geometry_hints (GTK_WINDOW (dialogue),
				     NULL,
				     &geometry,
				     GDK_HINT_MIN_SIZE|
				     GDK_HINT_MAX_SIZE);

      g_signal_connect (dialogue,
			"response",
			G_CALLBACK (messagebox_response),
			box);

      vbox =
	gtk_vbox_new (0, FALSE);

      gtk_container_add (GTK_CONTAINER(gtk_dialog_get_content_area(dialogue)),
			 vbox);

      label =
	gtk_label_new (message);

      gtk_box_pack_end (GTK_BOX (vbox),
			label,
			TRUE, TRUE, 0);
      
      gtk_widget_show_all (GTK_WIDGET (dialogue));

      window_set_sharing (GDK_WINDOW_XID (GTK_WIDGET (dialogue)->window),
	SHARING_UNSHAREABLE,
	NULL, NULL,
	FALSE);

    }
  else
    {
      gtk_label_set_text (GTK_LABEL (box->label),
			  message);
    }

  if (box)
    {
      box->box = dialogue;
      box->label = label;
    }
}

#ifdef MESSAGEBOX_TEST

static void
unshare_callback (gpointer user_data)
{
  g_print ("Here we would unshare %s.\n",
	   (char*) user_data);
}

static gboolean
messagebox_change (gpointer user_data)
{
  MessageBox *box = (MessageBox*) user_data;

  messagebox_show (box,
		   "This is a new message.");

  return FALSE;
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

  messagebox_show (box,
		   "Hello world.");

  messagebox_unref (box);

  g_timeout_add (1000,
		 messagebox_change,
		 box);

#endif

  gtk_main ();
}

#endif /* MESSAGEBOX_TEST */

/* EOF messagebox.c */

