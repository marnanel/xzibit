/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Contact chooser dialogue for xzibit.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "contact-chooser.h"
#include "list-contacts.h"
#include "sharing.h"
#include <gdk/gdkx.h>

#define _(x) (x)

typedef struct _ContactContext {
  gboolean closed;
  gboolean seen_all_contacts;
  GtkListStore *model;
  GtkWidget *treeview;
  GtkWidget *label;
  GHashTable *sources;
  contact_chooser_cb *callback;
} ContactContext;

/**
 * Frees a context, but only if its window is
 * closed AND we've seen all the contacts.
 *
 * \param context  The context to free, maybe.
 */
static void
context_maybe_free (ContactContext *context)
{
  if (!context)
    return;

  if (context->closed &&
      context->seen_all_contacts)
    {
      g_hash_table_destroy (context->sources);
      g_free (context);
    }
}

static void
add_contact (const gchar *source,
	     const gchar *target,
	     gpointer user_data)
{
  ContactContext *context = (ContactContext*) user_data;
  GtkTreeIter iter;

  if (!source && !target)
    {
      context->seen_all_contacts = TRUE;

      if (!context->closed)
	{
	  gtk_label_set_text (GTK_LABEL (context->label),
			      _("Please choose a contact to "
				"share this window with."));
	}

      /*
       * It's possible that the window has been closed
       * and that we should therefore free the memory.
       */
      context_maybe_free (context);

      return;
    }

  if (g_hash_table_lookup (context->sources,
			   target))
    return; /* it's already known from another account */

  g_hash_table_insert (context->sources,
		       (gpointer) g_strdup (target),
		       (gpointer) g_strdup (source));

  gtk_list_store_append (context->model,
			 &iter);
  gtk_list_store_set (context->model,
		      &iter,
		      0, target,
		      -1);
}

/**
 * Enables the OK button, if the treeview
 * has a row selected.
 */
static void
enable_ok_button (GtkTreeSelection *selection,
		  gpointer user_data)
{
  GtkWidget *ok_button = (GtkWidget*) user_data;
  guint count =
    gtk_tree_selection_count_selected_rows (selection);

  gtk_widget_set_sensitive (ok_button,
			    count != 0);
}

static void
handle_response (GtkDialog *dialogue,
		 gint response_id,
		 gpointer user_data)
{
  ContactContext *context =
    (ContactContext*) user_data;

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      GtkTreePath *path;
      GtkTreeIter iter;
      GtkTreeSelection *selection =
	gtk_tree_view_get_selection (GTK_TREE_VIEW (context->treeview));
      GList *paths =
	gtk_tree_selection_get_selected_rows (selection,
					      NULL);
      GValue value = {0};

      if (paths == NULL)
	{
	  /* shouldn't be possible to get here in this case */
	  g_warning ("No paths were selected");
	  return;
	}

      path = (GtkTreePath*) paths->data;

      if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (context->model),
				    &iter,
				    path))
	{
	  g_warning ("Could not get the path into the tree.");
	  /* just bail */
	  g_list_foreach (paths, (GFunc) gtk_tree_path_free, NULL);
	  g_list_free (paths);
	  return;
	}

      gtk_tree_model_get_value (GTK_TREE_MODEL (context->model),
				&iter,
				0,
				&value);

      context->callback (123,
			 (char*) g_hash_table_lookup (context->sources,
						      g_value_get_string (&value)),
			 g_value_get_string (&value));

      g_value_unset (&value);
      g_list_foreach (paths, (GFunc) gtk_tree_path_free, NULL);
      g_list_free (paths);
    }

  gtk_widget_destroy (GTK_WIDGET (dialogue));

  context->closed = TRUE;

  context_maybe_free (context);

}

GtkWidget*
show_contact_chooser (int window_id,
		      contact_chooser_cb callback)
{
  ContactContext *context =
    g_malloc (sizeof (ContactContext));
  GtkWidget *window =
    gtk_dialog_new_with_buttons (_("Xzibit"),
				 NULL,
				 GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_MODAL,
				 GTK_STOCK_OK,
				 GTK_RESPONSE_ACCEPT,
				 GTK_STOCK_CANCEL,
				 GTK_RESPONSE_REJECT,
				 NULL);
  GtkWidget *vbox =
    gtk_vbox_new (0, FALSE);
  GdkGeometry geometry;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *ok_button;
  GtkTreeSelection *selection;

  gtk_dialog_set_default_response (GTK_DIALOG (window),
				   GTK_RESPONSE_ACCEPT);

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
  context->model = gtk_list_store_new (1,
				       G_TYPE_STRING);
  context->sources =
    g_hash_table_new_full (g_str_hash,
			   g_str_equal,
			   g_free,
			   g_free);
  context->callback = callback;

  context->treeview =
    gtk_tree_view_new_with_model (GTK_TREE_MODEL (context->model));

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Contact"),
						     renderer,
						     "text", 0,
						     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (context->treeview),
			       column);

  selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (context->treeview));

  gtk_container_add (GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(window))),
		     vbox);

  gtk_box_pack_end (GTK_BOX (vbox),
		    context->treeview,
		    TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (vbox),
		    context->label,
		    FALSE, FALSE, 0);

  /* 
   * Disable the OK button until they choose
   * a line from the list.
   */
  ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (window),
						  GTK_RESPONSE_ACCEPT);
  gtk_widget_set_sensitive (ok_button,
			    FALSE);

  g_signal_connect (selection,
		    "changed",
		    G_CALLBACK (enable_ok_button),
		    ok_button);


  /*
   * Set up a handler for dealing with the result
   */

  g_signal_connect (GTK_DIALOG (window),
		    "response",
		    G_CALLBACK (handle_response),
		    context);

  /*
   * Now show it all
   */

  gtk_widget_show_all (GTK_WIDGET (window));

  window_set_sharing (GDK_WINDOW_XID (GTK_WIDGET (window)->window),
                      SHARING_UNSHAREABLE,
                      NULL, NULL,
                      TRUE);

  list_contacts (add_contact,
		 "x-xzibit",
		 context);

  return window;
}


#ifdef CONTACT_CHOOSER_TEST

static void
dump_contact_chooser_result (int window,
			     const char* source,
			     const char* target)
{
  g_print ("Result was: window %x, source %s, target %s\n",
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
