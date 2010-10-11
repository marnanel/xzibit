#include "contact-chooser.h"

#define _(x) (x)

typedef struct _ContactContext {
  gboolean closed;
  gboolean seen_all_contacts;
  GtkTreeModel *model;
  GtkWidget *treeview;
  GtkWidget *label;
} ContactContext;

GtkWidget*
show_contact_chooser (int window_id,
		      contact_chooser_cb callback)
{
  ContactContext *context =
    g_malloc (sizeof (ContactContext));
  GtkWidget *window =
    gtk_dialog_new_with_buttons ("Xzibit",
				 NULL,
				 0,
				 GTK_STOCK_OK,
				 GTK_RESPONSE_ACCEPT,
				 GTK_STOCK_CANCEL,
				 GTK_RESPONSE_REJECT,
				 NULL);
  GtkWidget *vbox =
    gtk_vbox_new (0, FALSE);
  GtkWidget *treeview;
  GdkGeometry geometry;

  geometry.min_width = 300;
  geometry.min_height = 300;

  gtk_window_set_geometry_hints (GTK_WINDOW (window),
				 NULL,
				 &geometry,
				 GDK_HINT_MIN_SIZE);

  context->closed = FALSE;
  context->seen_all_contacts = FALSE;
  context->label =
    gtk_label_new (_("Please wait; checking contacts."));
  context->model =
    GTK_TREE_MODEL (gtk_list_store_new (1,
					G_TYPE_STRING));

  treeview =
    gtk_tree_view_new_with_model (context->model);

  gtk_container_add (GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(window))),
		     vbox);

  gtk_box_pack_end (GTK_BOX (vbox),
		    treeview,
		    TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (vbox),
		    context->label,
		    FALSE, FALSE, 0);
  
  gtk_widget_show_all (GTK_WIDGET (window));

  return window;
}


#ifdef CONTACT_CHOOSER_TEST

static void
dump_contact_chooser_result (int window,
			     const char* source,
			     const char* target)
{
  g_warning ("Result was: window %x, source %s, target %s",
	     window, source, target);
}

int
main (int argc, char **argv)
{
  GtkWidget *window;

  gtk_init (&argc, &argv);
  
  window =
    show_contact_chooser (177,
			  dump_contact_chooser_result);

  g_signal_connect (window,
		    "delete-event",
		    G_CALLBACK (gtk_main_quit),
		    NULL);

  gtk_main ();
}

#endif /* CONTACT_CHOOSER_TEST */
