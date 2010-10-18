#include "get-avatar.h"

GString*
get_avatar (void)
{
  return g_string_new ("");
}


#ifdef GET_AVATAR_TEST

int
main (int argc, char **argv)
{
  GString* result;

  gtk_init (&argc, &argv);

  g_print ("get-avatar - no parameters; try setting XZIBIT_AVATAR\n");

  result = get_avatar ();

  g_file_set_contents ("/tmp/get-avatar.png",
		       result->str,
		       result->len,
		       NULL);

  g_string_free (result, TRUE);
}

#endif /* GET_AVATAR_TEST */
