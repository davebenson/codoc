#include "hfile.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define LEFT_BRACE  "{"
#define RIGHT_BRACE "}"
gpointer hfile_cast(gpointer object, int type, const char* type_name,
                           const char *file, int line)
{
  if (object == NULL)
    {
      g_warning("object != NULL, casting to %s, %s:%d", type_name, file, line);
      return NULL;
    }
  if (* (int*) object == type)
    return object;

  g_warning("type mismatch, casting to %s, %s:%d", type_name, file, line);
  return NULL;
}
HFile   *hfile_base_cast(gpointer ptr, const char *file, int line)
{
  int type;
  if (ptr == NULL)
    {
      g_warning("object != NULL, casting to HFile, %s:%d",
                file, line);
      return NULL;
    }
  type = * (int*) ptr;
  if (type < _HFILE_TYPE_MIN)
    {
      g_warning ("hfile object type (%d) less than min type", type);
      return NULL;
    }
  if (type > _HFILE_TYPE_MAX)
    {
      g_warning ("hfile object type (%d) greater than max type", type);
      return NULL;
    }
  return (HFile*) ptr;
}

static HFileSemicolon * hfile_semicolon_new (const char* filename, int lineno)
{
  HFileSemicolon* rv;
  rv = g_new (HFileSemicolon, 1);
  rv->type = HFILE_TYPE_SEMICOLON;
  rv->filename = g_strdup (filename);
  rv->lineno = lineno;
  return rv;
}
static HFileComment * hfile_comment_new (const char *comment,
                                         const char *filename,
					 int         lineno)
{
  HFileComment* rv;
  rv = g_new (HFileComment, 1);
  rv->type = HFILE_TYPE_COMMENT;
  rv->comment = g_strdup (comment);
  rv->filename = g_strdup (filename);
  rv->lineno = lineno;
  return rv;
}
static HFileText * hfile_text_new       (const char *text,
                                         const char *filename,
					 int         lineno)
{
  HFileText* rv;
  rv = g_new (HFileText, 1);
  rv->type = HFILE_TYPE_TEXT;
  rv->text = g_strdup (text);
  rv->filename = g_strdup (filename);
  rv->lineno = lineno;
  return rv;
}
static HFileList * hfile_list_new       (GSList      *list,
                                         gboolean     braces,
                                         const char *filename,
					 int         lineno)
{
  HFileList *rv;
  rv = g_new (HFileList, 1);
  rv->type = HFILE_TYPE_LIST;
  rv->has_braces = braces;
  rv->list = list;
  rv->filename = g_strdup (filename);
  rv->lineno = lineno;
  return rv;
}
static GSList* scan_before_matching (GSList* cur)
{
  int depth = 1;

  g_assert (strcmp (HFILE_TEXT (cur->data)->text, LEFT_BRACE) == 0);

  while (cur->next != NULL)
    {
      HFileText *text = HFILE_TEXT(cur->next->data);
      if (cur->next == NULL)
        return NULL;
      if (strcmp (text->text, LEFT_BRACE) == 0)
        depth++;
      else if (strcmp (text->text, RIGHT_BRACE) == 0)
        depth--;
      if (depth == 0)
        return cur;
      cur = cur->next;
    }
  return NULL;
}

static HFileList *hfile_list_from_content_list (GSList   *hfile_list,
                                                const char *filename)
{
  GSList * cur_list = NULL;
  GSList * at;
  for (at = hfile_list; at != NULL; )
    {
      HFile *token = at->data;

      switch (token->type)
        {
	case HFILE_TYPE_LIST:
	  g_error ("shouldn't get a list at this phase");
	case HFILE_TYPE_SEMICOLON:
	  g_error ("shouldn't get a SEMICOLON at this phase");
	case HFILE_TYPE_COMMENT:
	  cur_list = g_slist_prepend (cur_list, token);
	  at = at->next;
	  break;
	case HFILE_TYPE_TEXT:
	  {
	    const char *text = HFILE_TEXT(token)->text;
	    if (strcmp (text, LEFT_BRACE) == 0)
	      {
	        GSList *match;
		HFileList *subhfile;
		GSList *sublist;
		match = scan_before_matching (at);
		if (match == NULL)
		  {
		    g_warning("mismatched " LEFT_BRACE " character");
		    return FALSE;
		  }
		sublist = at->next;
		at = match->next;
		match->next = NULL;
		subhfile = hfile_list_from_content_list (sublist, filename);
		if (subhfile == NULL)
		  {
		    /* XXX: memory leaks */
		    return NULL;
		  }
		subhfile->has_braces = TRUE;
		match->next = at;
		at = at->next;
	        cur_list = g_slist_prepend (cur_list, subhfile);
		break;
              }
	    if (strcmp(HFILE_TEXT (token)->text, ";") == 0)
	      {
		HFileAny *any = &token->any;
		HFileSemicolon *semicolon;
		semicolon = hfile_semicolon_new (any->filename, any->lineno);
	        cur_list = g_slist_prepend (cur_list, semicolon);
		at = at->next;
		break;
	      }
	    cur_list = g_slist_prepend (cur_list, token);
	    at = at->next;
	    break;
	  }
	default:
	  g_assert_not_reached ();
        }
    }
  cur_list = g_slist_reverse (cur_list);
  {
    HFileList *rv;
    if (cur_list == NULL)
      {
	rv = hfile_list_new (cur_list, FALSE, filename, 1);
      }
    else
      {
	HFileAny *any;
	any = HFILE_ANY (cur_list->data);
	rv = hfile_list_new (cur_list, FALSE, any->filename, any->lineno);
      }
    return rv;
  }
}

static gboolean is_whitespace(const char* str)
{
  while (str != '\0' && isspace (*str))
     str++;
  return (str == '\0');
}
 

static char* get_backslashed_line (FILE* fp)
{
  char buf[4096];
  GString *str;
  char *rv;
  str = NULL;
  while (fgets(buf, sizeof(buf), fp))
    {
      char *backslash = strrchr (buf, '\\');
      if (str == NULL)
        str = g_string_new ("");
      if (!backslash)
        {
	  g_string_append (str, buf);
	  break;
	}
      if (is_whitespace (backslash + 1))
        {
	  strcpy(backslash, " ");
	  g_string_append (str, buf);
	}
    }
  if (str == NULL)
    return NULL;
  rv = str->str;
  g_string_free (str, FALSE);
  return rv;
}
char *hfile_get_word (const char *text)
{
  const char *end_word;
  char *rv;
  while (*text != '\0' && isspace(*text))
    text++;
  if (*text == '\0')
    return NULL;
  end_word = text;
  while (*end_word != '\0' && ! isspace(*end_word))
    end_word++;
  rv = g_new (char, end_word - text + 1);
  memcpy (rv, text, end_word - text);
  rv[end_word - text] = '\0';
  return rv;
}

HFileList*     hfile_parse_preprocessed   (const char      *filename,
                                           FILE            *fp)
{
  char *line;
  GString *tmp;
  GSList *content_list;
  gboolean in_comment;
  gboolean in_correct_file = TRUE;
  int lineno = 1;

  content_list = NULL;
  in_comment = FALSE;
  tmp = g_string_new ("");
  for (    ;
       (line = get_backslashed_line (fp)) != NULL;
       g_free (line))
    {
      const char *pos;
      lineno++;
      if (! in_comment)
        {
	  const char *non_ws;
	  non_ws = line;
	  while (non_ws != '\0' && isspace (*non_ws))
	    non_ws++;
	  /* discard blank lines. */
	  if (*non_ws == '\0')
	    continue;
	  if (*non_ws == '#')
	    {
	      /* simulate the preprocessor. */
	      non_ws++;
	      while (non_ws != '\0' && isspace (*non_ws))
	        non_ws++;
	      if (*non_ws == '\0')
	        continue;
	      if (strncmp (non_ws, "file", 4) == 0)
	        {
		  char* cpp_filename;
		  cpp_filename = hfile_get_word (non_ws + 4);
		  if (strcmp (cpp_filename, filename) == 0)
		    in_correct_file = TRUE;
		  else
		    in_correct_file = FALSE;
		}
	      else if (isdigit (*non_ws))
	        {
		  lineno = atoi (non_ws);
		  while (*non_ws != '\0' && isdigit (*non_ws))
		    non_ws++;
		  while (*non_ws != '\0' && isspace (*non_ws))
		    non_ws++;
		  if (*non_ws == '"')
		    {
		      const char *end;
		      non_ws++;
		      end = strchr (non_ws, '"');
		      if (end - non_ws == (int) strlen (filename)
		       && memcmp (filename, non_ws, end - non_ws) == 0)
		        in_correct_file = TRUE;
		      else
		        in_correct_file = FALSE;
		    }
	        }
	        
	      continue;
	    }
	}

      /* perform the c-chunking phase, breaking out comments,
       * and semicolons and making braces into their own atoms.
       */
      pos = line;
      while (*pos != '\0')
        {
	  char c = *pos;
	  pos++;

	  if (in_comment)
	    {
	      if (strcmp (tmp->str + tmp->len - 2, "*/") == 0)
	        {
		  HFileComment *comment;
                  g_string_append_c (tmp, c);
		  if (in_correct_file)
		    {
		      comment = hfile_comment_new (tmp->str, filename, lineno);
	              content_list = g_slist_prepend (content_list, comment);
		    }
	          g_string_erase (tmp, 0, tmp->len);
		}
	      continue;
	    }

	  if (strchr ("{};", c) != NULL)
	    {
	      HFileText *text;
	      char tmp_str[2];
	      if (in_correct_file)
	        {
	          text = hfile_text_new (tmp->str, filename, lineno);
	          tmp_str[0] = c;
	          tmp_str[1] = '\0';
	          content_list = g_slist_prepend (content_list, text);
	          text = hfile_text_new (tmp_str, filename, lineno);
	          content_list = g_slist_prepend (content_list, text);

		  g_string_free (tmp, TRUE);
		  tmp = g_string_new ("");
	        }
	      continue;
	    }
	  if (in_correct_file)
            g_string_append_c (tmp, c);
	}
    }
  content_list = g_slist_reverse (content_list);

  /* now, erect the tree structure by grouping the braces.
   */
  return hfile_list_from_content_list (content_list, filename);
}

static FILE  * open_cpp (const char    *filename,
                         const char    *options)
{
  FILE *fp;
  char *str;
  str = g_strdup_printf ("cpp %s %s", options, filename);
  fp = popen (str, "r");
  if (fp == NULL)
    g_error ("popen cpp failed");
  return fp;
}

void hfile_dump (HFile *hfile,
                 int    indent,
		 FILE  *out)
{
  int i;
  for (i = 0; i < indent; i++)
    fprintf (out, " ");
  switch (hfile->type)
    {
    case HFILE_TYPE_SEMICOLON:
      fprintf (out, "SEMICOLON");
      break;
    case HFILE_TYPE_TEXT:
      {
        int len;
	len = strlen (HFILE_TEXT (hfile)->text);
	if (len < 50)
          fprintf (out, "TEXT: `%s'", HFILE_TEXT (hfile)->text);
	else
          fprintf (out, "TEXT: %d characters.", len);
      }
      break;
    case HFILE_TYPE_LIST:
      fprintf (out, "LIST");
      {
        GSList *list = HFILE_LIST (hfile)->list;
	while (list != NULL)
	  {
	    hfile_dump ((HFile*)list->data, indent + 1, out);
	    list = list->next;
	  }
      }
      break;
    default:
      fprintf (out, "???");
    }
  fprintf (out, "\n");
}

HFileList     *hfile_parse   (const char      *filename,
                              const char      *options)
{
  FILE *fp;
  int exit_code;
  HFileList *rv;
  fp = open_cpp (filename, options);
  if (fp == NULL)
    return NULL;
  rv = hfile_parse_preprocessed (filename, fp);
  exit_code = pclose (fp);
  if (rv == NULL)
    {
      g_warning ("error parsing %s", filename);
      return NULL;
    }
  if (exit_code != 0)
    {
      g_warning ("failed running cpp %s", filename);
      hfile_destroy ((HFile*)rv);
      return NULL;
    }

#if 0
  fp = fopen (filename, "r");
  if (fp == NULL)
    {
      g_warning ("couldn't fopen %s: %s", fp, g_strerror (errno));
      /* XXX: leaks */
      return NULL;
    }
  rv_comments = comment_scan (fp, filename);
  ...
  fclose (fp);

  /* merge comments ('rv_comments') with 'rv' */
  ...
#endif

  return rv;
}

void
hfile_destroy (HFile *to_destroy)
{
  switch (to_destroy->type)
    {
    case HFILE_TYPE_LIST:
      {
        GSList *list = HFILE_LIST (to_destroy)->list;
        g_slist_foreach (list, (GFunc) hfile_destroy, NULL);
        g_slist_free (list);
        break;
      }
    case HFILE_TYPE_TEXT:
      g_free (HFILE_TEXT (to_destroy)->text);
      break;
    case HFILE_TYPE_COMMENT:
      g_free (HFILE_COMMENT (to_destroy)->comment);
      break;
    case HFILE_TYPE_SEMICOLON:
      break;
    default:
      g_error ("hfile_destroy: unknown hfile type: %d", to_destroy->type);
    }
  g_free (to_destroy);
}
