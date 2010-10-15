#include "loopback.h"

typedef struct _LoopbackContext {
  list_contacts_cb *callback;
  gpointer user_data;
  GHashTable *targets;
} LoopbackContext;

static void
find_loopback_cb (const gchar *source_path,
		  const gchar *source,
		  const gchar *target,
		  gpointer user_data)
{
  LoopbackContext *context =
    (LoopbackContext*) user_data;
  gchar *finding;

  if (!source_path)
    {
      /* All done. */
      if (context->targets)
	{
	  /* We didn't find anything. */
	  g_hash_table_destroy (context->targets);

	  context->callback (NULL, NULL, NULL,
			     context->user_data);
	}
      g_free (context);
      return;
    }

  if (context->targets==NULL)
    return;

  finding = g_hash_table_lookup (context->targets,
				 source);

  if (finding &&
      strcmp (finding, target)==0)
    {
      /* Found! */

      context->callback (source_path,
			 source,
			 target,
			 context->user_data);

      g_hash_table_destroy (context->targets);
      context->targets = NULL;
    }
  else
    {
      g_hash_table_insert (context->targets,
			   g_strdup (target),
			   g_strdup (source));
    }
}

void
find_loopback (list_contacts_cb callback,
	       gchar *wanted_service,
	       gpointer user_data)
{
  LoopbackContext *context =
    g_malloc (sizeof (LoopbackContext));

  context->callback = callback;
  context->user_data = user_data;
  context->targets = g_hash_table_new_full (g_str_hash,
					    g_str_equal,
					    g_free,
					    g_free);

  list_contacts (find_loopback_cb,
		 wanted_service,
		 context);
		 
}

#ifdef LOOPBACK_TEST

void
dumper (const gchar *source_path, const gchar *source, const gchar *target,
	gpointer user_data)
{
  g_warning ("Loopback is %s (%s) -> %s (%p)",
	     source, source_path, target, user_data);
}

int
main(int argc, char **argv)
{
  GMainLoop *loop;

  g_type_init ();

  loop = g_main_loop_new (NULL, FALSE);

  find_loopback (dumper,
		 "x-xzibit",
		 NULL);

  g_main_loop_run (loop);
}

#endif



