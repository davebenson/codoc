#include "libcodoc.h"
#include <stdio.h>

static char *get_initial_type (const char *init_type)
{
  const char *end_type;
  char *rv;
  end_type = init_type;
  if (! codoc_util_is_c_type (&end_type))
    return NULL;
  rv = g_new (char, end_type - init_type + 1);
  memcpy (rv, init_type, end_type - init_type);
  rv[end_type - init_type] = '\0';
  return rv;
}

static void assert_pair (const char *init_type, const char *type)
{
  char *rv;
  rv = get_initial_type (init_type);
  g_strstrip (rv);
  if (strcmp (rv, type) != 0)
    {
      fprintf (stderr, "Type pair:      %s\n"
                       "Expected type     %s"
                       "Got type          %s", init_type, type, rv);
    }
  g_free (rv);
}

int main (int argc, char **argv)
{
  assert_pair ("const char * a", "const char *");
  assert_pair ("unsigned a", "unsigned");
  assert_pair ("long int ** abc def", "long int **");
  assert_pair ("short int ** abc def", "short int **");
  assert_pair ("struct a ** abc def", "struct a **");
  return 0;
}
