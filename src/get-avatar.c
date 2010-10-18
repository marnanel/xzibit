#include "get-avatar.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

#define IMAGE_FILENAME "/tmp/get-avatar.png"

static char*
get_filename (int which)
{
  switch (which)
    {
    case 0:
      {
	const char *env = g_getenv ("XZIBIT_AVATAR");
	if (env)
	  return g_strdup (env);
	else
	  return "";
      }

    case 1:
      {
	const char *xdg = g_get_user_config_dir ();
	char *result = g_build_filename
	  (xdg,
	   "xzibit",
	   "avatar.png",
	   NULL);
	
	return result;
      }

    case 2:
      {
	char *result = g_build_filename
	  (g_get_home_dir (),
	   ".face",
	   NULL);

	return result;
      }

    default:
      return NULL;
    }
}

GString*
get_avatar (void)
{
  int i=0;
  char *filename;

  do
    {
      filename = get_filename (i);

      g_print ("Filename %d: %s\n",
	       i, filename);

      if (filename && *filename &&
	  g_file_test (filename,
		       G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
	{
	  GdkPixbuf *pixbuf = NULL;
	  gchar *buffer;
	  gsize buffer_size;

	  g_print ("A possibility!\n");

	  pixbuf = gdk_pixbuf_new_from_file (filename,
					     NULL);

	  if (!pixbuf)
	    {
	      g_warning ("File %s was not an image; ignoring", filename);
	      i++;
	      continue;
	    }

	  /* FIXME: we also need to do scaling */

	  /* FIXME: error checking?? */
	  gdk_pixbuf_save_to_buffer (pixbuf,
				     &buffer,
				     &buffer_size,
				     "png",
				     NULL, NULL);

	  return g_string_new_len (buffer,
				   buffer_size);
	}

      i++;
    }
  while (filename);

  return g_string_new ("");
}


#ifdef GET_AVATAR_TEST

int
main (int argc, char **argv)
{
  GString* result;
  char* args[3];

  gtk_init (&argc, &argv);

  g_print ("get-avatar - no parameters; try setting XZIBIT_AVATAR\n");

  result = get_avatar ();

  g_file_set_contents (IMAGE_FILENAME,
		       result->str,
		       result->len,
		       NULL);

  g_string_free (result, TRUE);

  args[0] = "eog";
  args[1] = IMAGE_FILENAME;
  args[2] = NULL;

  g_spawn_async ("/",
		 args,
		 NULL,
		 G_SPAWN_SEARCH_PATH,
		 NULL, NULL,
		 NULL, NULL);
}

#endif /* GET_AVATAR_TEST */
