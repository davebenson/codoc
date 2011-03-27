#include "libcodoc.h"
#include <ctype.h>
#include <string.h>
#include <libxml/parser.h>		/* UGH */
#include <stdlib.h>

static inline gboolean
is_word_char (char c)
{
  return (isalnum (c) || c == '_');
}
#define SKIP_CHAR_TYPE(ptr, cond) while (*(ptr) != '\0' && cond(*(ptr))) ptr++
#define SKIP_WHITESPACE(ptr)      SKIP_CHAR_TYPE(ptr, isspace)
#define SKIP_NON_WHITESPACE(ptr)  SKIP_CHAR_TYPE(ptr, !isspace)
#define SKIP_DIGITS(ptr)          SKIP_CHAR_TYPE(ptr, isdigit)

/* from libcodoc-xml.c */
xmlNode *codocXmlCopyNodeList (xmlNode *);

/*
 *  _                       _       _
 * | |_ ___ _ __ ___  _ __ | | __ _| |_ ___
 * | __/ _ \ '_ ` _ \| '_ \| |/ _` | __/ _ \
 * | ||  __/ | | | | | |_) | | (_| | ||  __/
 *  \__\___|_| |_| |_| .__/|_|\__,_|\__\___|
 *                   |_|
 *                       _
 *  _ __   __ _ _ __ ___(_)_ __   __ _
 * | '_ \ / _` | '__/ __| | '_ \ / _` |
 * | |_) | (_| | |  \__ \ | | | | (_| |
 * | .__/ \__,_|_|  |___/_|_| |_|\__, |
 * |_|                           |___/
 */
static xmlNode *
blurb_to_xml (char *text)
{
  char *doc_text;
  xmlDoc *doc;
  const char *tmp;
  xmlNode *node;
  if (text == NULL)
    return NULL;
  tmp = text;
  while (*tmp != 0 && isspace(*tmp))
    tmp++;
  if (*tmp == 0)
    {
      g_free (text);
      return NULL;
    }
  doc_text = g_strdup_printf ("<document>%s</document>", tmp);
  doc = xmlParseDoc (doc_text);
  g_free (doc_text);
  doc_text = NULL;
  if (doc == NULL)
    {
      g_warning ("couldn't parse '%s' as xml: making in to escaped text node",
		 text);
      node = xmlNewText (text);
    }
  else
    {
      node = xmlDocGetRootElement (doc);
      if (node == NULL || node->children == NULL)
	{
	  g_warning ("empty blurb: `%s'?", text);
	  node = NULL;
	}
      else
	{
	  g_assert (strcmp (node->name, "document") == 0);
	  node = codocXmlCopyNodeList (node->children);
	  xmlFreeDoc (doc);
	}
    }
  g_free (text);
  return node;
}


/*
 * Commands:
 *
 *     FUNCTION function_name
 *     FUNCTION_PARAM function_name param_index param_name param_type
 *     STRUCT struct_name
 *     STRUCT_MEMBER struct_name member_name member_type
 *     STRUCT_METHOD struct_name method_name return_type
 *     STRUCT_METHOD_PARAM struct_name method_name param_index name type
 *     ENUM enum_name
 *     ENUM_VALUE enum_name enum_value
 *     UNION union_name type1/name1/....
 *     GLOBAL name type
 *     SECTION hfilename title
 */
static gboolean
process_command (CODatabase    *database,  
		 const char    *id,
		 const char    *name,
		 const char    *parameter,
		 char          *blurb_text,
		 COContext     *context)
{
  xmlNode *xml_blurb = blurb_to_xml (blurb_text);
  if (strcasecmp (id, "FUNCTION_TYPEDEF") == 0)
    {
      co_database_add_function (database, TRUE, 
                                name, parameter, xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "FUNCTION") == 0)
    {
      co_database_add_function (database, FALSE, 
                                name, parameter, xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "FUNCTION_PARAM") == 0
   || strcasecmp (id, "FUNCTION_TYPEDEF_PARAM") == 0)
    {
      char *ends;
      int param_index;
      const char *param_name_start;
      char *param_name;
      gboolean is_typedef = strcasecmp("FUNCTION_TYPEDEF_PARAM", id) == 0;
      param_index = (int) strtol (parameter, &ends, 10);
      if (ends == parameter)
        {
          context_warning (context, "error getting param index (%s)", name);
          return FALSE;
        }
      SKIP_WHITESPACE (ends);
      param_name_start = ends;
      SKIP_NON_WHITESPACE (ends);
      param_name = alloca (ends + 1 - param_name_start);
      memcpy (param_name, param_name_start, ends - param_name_start);
      param_name[ends - param_name_start] = '\0';
      SKIP_WHITESPACE (ends);
      co_database_add_function_param (database, is_typedef, name, param_index,
                                      param_name, ends, xml_blurb, NULL, 0,
				      context);
      return TRUE;
    }
  if (strcasecmp (id, "STRUCT_STUB") == 0)
    {
      co_database_add_struct_stub (database, name, xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "STRUCT") == 0)
    {
      co_database_add_struct (database, name, xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "STRUCT_MEMBER") == 0
   || strcasecmp (id, "STRUCT_METHOD") == 0)
    {
      char *member_name;
      const char *member_name_start;
      char *flags;
      const char *flags_start;
      gboolean is_private;
      SKIP_WHITESPACE (parameter);
      member_name_start = parameter;
      SKIP_NON_WHITESPACE (parameter);
      member_name = alloca (parameter + 1 - member_name_start);
      memcpy (member_name, member_name_start, parameter - member_name_start);
      member_name[parameter - member_name_start] = '\0';
      SKIP_WHITESPACE (parameter);
      flags_start = parameter;
      SKIP_NON_WHITESPACE (parameter);
      flags = alloca (parameter + 1 - flags_start);
      memcpy (flags, flags_start, parameter - flags_start);
      flags[parameter - flags_start] = '\0';
      SKIP_WHITESPACE (parameter);
      is_private = (strcasecmp (flags, "private") == 0);
      if (strcasecmp (id, "STRUCT_METHOD") == 0)
        co_database_add_member_function (database, name, 
                                         member_name, parameter, 
                                         -1, NULL,
                                         xml_blurb, NULL, 0,
					 context);
      else
        co_database_add_struct_member (database, name, 
                                       member_name, parameter,
                                       xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "STRUCT_METHOD_PARAM") == 0)
    {
      const char *struct_name = name;
      char *method_name;
      int param_index;
      char *param_name;
      char *param_type;

      char *ends;
      const char *end;

      SKIP_WHITESPACE (parameter);
      end = parameter;
      SKIP_CHAR_TYPE (end, is_word_char);
      method_name = alloca (end + 1 - parameter);
      memcpy (method_name, parameter, end - parameter);
      method_name[end - parameter] = '\0';
      parameter = end;

      param_index = (int) strtol (parameter, &ends, 10);
      if (ends == parameter)
        {
          context_warning (context,"error parsing param index to METHOD_PARAM");
          return FALSE;
        }
      parameter = ends;
      SKIP_WHITESPACE (parameter);
      end = parameter;
      SKIP_CHAR_TYPE (end, is_word_char);
      param_name = alloca (end - parameter + 1);
      memcpy (param_name, parameter, end - parameter);
      param_name[end - parameter] = '\0';
      parameter = end;
      SKIP_WHITESPACE (parameter);
      param_type = alloca (strlen (parameter) + 1);
      strcpy (param_type, parameter);
      g_strchomp (param_type);

      co_database_add_struct_member_param (database, struct_name, 
                                           method_name, param_index,
                                           param_name, param_type,
                                           xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "ENUM") == 0)
    {
      co_database_add_enum (database, name, xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "ENUM_VALUE") == 0)
    {
      co_database_add_enum_value (database, name, parameter, xml_blurb,
                                  NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "UNION") == 0)
    {
      char **type_name_pairs;
      int num_els;
      type_name_pairs = g_strsplit (parameter, "/", 0);
      num_els = 0;
      while (type_name_pairs[num_els] != NULL)
        {
          g_strchomp (type_name_pairs[num_els]);
          g_strchug (type_name_pairs[num_els]);
          num_els++;
        }
      co_database_add_union (database, name,
                             num_els / 2, type_name_pairs,
                             xml_blurb, NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "GLOBAL") == 0)
    {
      co_database_add_global (database, name, parameter, xml_blurb, 
			      NULL, 0, context);
      return TRUE;
    }
  if (strcasecmp (id, "SECTION") == 0)
    {
      co_database_new_section (database, name, parameter, xml_blurb,
			       NULL, 0, context);
      return TRUE;
    }

  if (strcasecmp (id, "CPP_FLAGS") == 0)
    {
      if (database->cpp_flags != NULL)
        {
          g_warning ("CPP_FLAGS specified multiple times");
          g_free (database->cpp_flags);
        }
      database->cpp_flags = g_strjoin (" ", name, parameter, NULL);
      return TRUE;
    }
      
  if (strcasecmp (id, "SUBDIR") == 0)
    {
      co_database_add_subdir (database, name);
      return TRUE;
    }

  if (strcasecmp (id, "OMIT") == 0)
    {
      co_database_add_omission (database, name);
      return TRUE;
    }
      
  if (strcasecmp (id, "SGML_FILE") == 0)
    {
      if (database->output_doc_filename != NULL)
        g_free (database->output_doc_filename);
      database->output_doc_filename = g_strdup (name);
      g_message ("setting output-doc-filename to %s", database->output_doc_filename);
      return TRUE;
    }
      
  context_warning (context, "unrecognized command: %s", id);
  return FALSE;
}

/* note:  we g_free the xml_blurb eventually! */
static gboolean
process_section (CODatabase    *database,  
		 const char    *header,
		 char          *xml_blurb,
		 COContext     *context)
{
  const char *id_start;
  const char *id_end;
  const char *name_start;
  const char *name_end;
  const char *param;

  char *name;
  char *id;

  SKIP_WHITESPACE (header);
  id_start = header;
  id_end = id_start;
  SKIP_NON_WHITESPACE (id_end);
  name_start = id_end;
  SKIP_WHITESPACE (name_start);
  name_end = name_start;
  SKIP_NON_WHITESPACE (name_end);
  param = name_end;
  SKIP_WHITESPACE (param);

  id = alloca (id_end - id_start + 1);
  name = alloca (name_end - name_start + 1);
  memcpy (id, id_start, id_end - id_start);
  id[id_end - id_start] = '\0';
  memcpy (name, name_start, name_end - name_start);
  name[name_end - name_start] = '\0';

  return process_command (database, id, name, param, xml_blurb, context);
}

CODatabase *
co_database_load_old     (const char       *filename)
{
  CODatabase *database;
  FILE *fp;
  char buf[8192];
  GString *xml_buffer;
  char *last_header = NULL;
  COContext context;
  context.file = filename;
  context.lineno = 0;
  context.in_h_file = FALSE;

  fp = fopen (filename, "r");
  if (fp == NULL)
    {
      context_warning (&context, "%s not found", filename);
      return NULL;
    }

  database = co_database_new ();

  xml_buffer = g_string_new ("");
  while (fgets (buf, sizeof(buf), fp) != NULL)
    {
      context.lineno++;
      /* all the headers begin with @@@; but there's
       * not much to do until we have the xml_blurb
       * so, just save the header for later parsing.
       */
      if (memcmp(buf, "@@@", 3) == 0)
        {
          /* process the last section */
          if (last_header != NULL)
            {
              gboolean rv;
              rv = process_section (database, last_header, xml_buffer->str,
                                    &context);
              g_string_free (xml_buffer, FALSE);
              xml_buffer = g_string_new ("");
              if (! rv)
                {
                  context_warning (&context,
                                   "processing the last section (%s) failed",
                                   last_header);
                }
              g_free (last_header);
              last_header = NULL;
            }
          g_strchomp (buf);
          last_header = g_strdup (buf + 3);
          g_strchug (last_header);
          continue;
        }
      g_string_append (xml_buffer, buf);
    }

    /* process the last section */
    if (last_header != NULL)
      {
        gboolean rv;
        rv = process_section (database, last_header, xml_buffer->str,
                              &context);
        g_string_free (xml_buffer, FALSE);
        if (! rv)
          {
            context_warning (&context,
                             "processing the last section (%s) failed",
                             last_header);
          }
        g_free (last_header);
        last_header = NULL;
      }
  return database;
}

/*
 *        _     _   _                       _       _
 *   ___ | | __| | | |_ ___ _ __ ___  _ __ | | __ _| |_ ___
 *  / _ \| |/ _` | | __/ _ \ '_ ` _ \| '_ \| |/ _` | __/ _ \
 * | (_) | | (_| | | ||  __/ | | | | | |_) | | (_| | ||  __/
 *  \___/|_|\__,_|  \__\___|_| |_| |_| .__/|_|\__,_|\__\___|
 *                                   |_|
 *             _       _   _
 *  _ __  _ __(_)_ __ | |_(_)_ __   __ _
 * | '_ \| '__| | '_ \| __| | '_ \ / _` |
 * | |_) | |  | | | | | |_| | | | | (_| |
 * | .__/|_|  |_|_| |_|\__|_|_| |_|\__, |
 * |_|                             |___/
 */
#if 0
static void render_template_enum_value (const char           *key,
                                        COEnumerationValue   *value,
					COEnumerationIter    *iter)
{
  FILE *output = iter->output;
  char privacy_string[256];
  compute_privacy_string (privacy_string, value->flags);
  fprintf (output, "@@@ENUM_VALUE %s %s%s\n", iter->enumeration->name, key,
	   privacy_string);
  if (value->xml_blurb != NULL)
    fputs (value->xml_blurb, output);
}

static gboolean render_template_enumeration(COEnumeration  *enumeration,
                                            FILE           *output)
{
  COEnumerationIter iter;
  char privacy_string[256];
  compute_privacy_string (privacy_string, enumeration->flags);
  fprintf (output, "@@@ENUM %s%s\n", enumeration->name, privacy_string);
  if (enumeration->xml_blurb != NULL)
    fputs (enumeration->xml_blurb, output);
  iter.enumeration = enumeration;
  iter.output = output;
  iter.had_error = FALSE;
  g_hash_table_foreach (enumeration->values,
                        (GHFunc) render_template_enum_value,
			&iter);
  return TRUE;
}
static gboolean render_template_function   (COFunction     *function,
					    gboolean        is_typedef,
                                            FILE           *output)
{
  int i;
  const char *ret_type = function->return_type;
  const char *cmd_str;
  char privacy_string[256];
  compute_privacy_string (privacy_string, function->flags);
  if (ret_type == NULL)
    ret_type = "";
  cmd_str = is_typedef ? "FUNCTION_TYPEDEF" : "FUNCTION";
  fprintf (output, "@@@%s %s %s%s\n", 
      cmd_str, function->function_name, ret_type,
      privacy_string);
  if (function->xml_blurb != NULL)
    fputs (function->xml_blurb, output);
  for (i = 0; i < function->num_parameters; i++)
    {
      COParameter *param = function->parameters[i];
      if (param != NULL)
        {
	  const char *type = param->type;
	  if (type == NULL)
	    type = "";
	  fprintf (output, "@@@%s_PARAM %s %d %s %s%s\n",
	    cmd_str, function->function_name, i, param->name, type,
	    privacy_string);
	  if (param->xml_blurb != NULL)
            fputs (param->xml_blurb, output);
	}
    }
  return TRUE;
}

typedef struct _CIStructIter COStructIter;
struct _CIStructIter {
  COStructure   *structure;
  FILE          *output;
};

static gboolean  render_template_member   (const char      *member_name,
                                           COMember        *member,
					   COStructIter    *iter)
{
  char privacy_string[256];
  compute_privacy_string (privacy_string, member->flags);
  fprintf (iter->output, "@@@STRUCT_%s %s %s %s%s\n",
	    member->is_function ? "METHOD" : "MEMBER",
  	    iter->structure->name, member_name, 
	    member->type,
	    privacy_string);
  if (member->xml_blurb != NULL)
    fputs (member->xml_blurb, iter->output);

  if (member->is_function)
    {
      int i;
      for (i = 0; i < member->num_parameters; i++)
        {
	  COParameter *parameter = member->parameters[i];
	  g_return_val_if_fail (parameter != NULL, FALSE);

	  fprintf (iter->output, "@@@STRUCT_METHOD_PARAM %s %s %d %s %s\n",
	  	iter->structure->name, member_name,
		i, parameter->name, parameter->type);
          
	  if (parameter->xml_blurb != NULL)
	    fputs (parameter->xml_blurb, iter->output);
	}
    }
    return TRUE;
}

static gboolean render_template_structure  (COStructure    *structure,
                                            FILE           *output)
{
  COStructIter iter;
  int num_members;
  int i;
  char privacy_string[256];
  compute_privacy_string (privacy_string, structure->flags);
  iter.structure = structure;
  iter.output = output;
  fprintf (output, "@@@STRUCT %s%s\n", structure->name, privacy_string);
  if (structure->xml_blurb != NULL)
    fputs (structure->xml_blurb, output);
  num_members = g_hash_table_size (structure->members);
  for (i = 0; i < num_members; i++)
    {
      const char *name = structure->member_names[i];
      COMember *member = g_hash_table_lookup (structure->members, name);
      g_return_val_if_fail (member != NULL, FALSE);
      if (! render_template_member (name, member, &iter))
        return FALSE;
    }
  return TRUE;
}
static gboolean render_template_union      (COUnion        *un,
                                            FILE           *output)
{
  int i;
  char privacy_string[256];
  compute_privacy_string (privacy_string, un->flags);
  fprintf (output, "@@@UNION %s ", un->name);
  for (i = 0; i < un->num_objects; i++)
    {
      fprintf (output, "%s/%s%s", un->type_name_pairs[2 * i + 0],
                                  un->type_name_pairs[2 * i + 1],
				  i == un->num_objects - 1 ? "" : "/");
    }
  fprintf (output, "%s\n", privacy_string);
  if (un->xml_blurb != NULL)
    fputs (un->xml_blurb, output);
  return TRUE;
}
static gboolean render_template_global     (COGlobal       *global,
                                            FILE           *output)
{
  char privacy_string[256];
  compute_privacy_string (privacy_string, global->flags);
  fprintf (output, "@@@GLOBAL %s %s%s\n", global->name, global->type, 
	   privacy_string);
  if (global->xml_blurb != NULL)
    fputs (global->xml_blurb, output);
  return TRUE;
}

static gboolean render_template_struct_stub (COStructStub    *stub,
                                             CODatabase      *database,
				             FILE            *output)
{
  COStructure *structure;
  char privacy_string[256];
  compute_privacy_string (privacy_string, stub->flags);
  structure = g_hash_table_lookup (database->structures, stub->name);
  if (structure != NULL)
    {
      if (stub->xml_blurb != NULL)
        {
	  fprintf (output, "@@@STRUCT_STUB %s  "
	  "!!! WARNING THIS WILL NOT BE USED, "
	  "DELETE AND EDIT @@@STRUCTURE %s instead !!!\n%s",
	           stub->name, stub->name, stub->xml_blurb);
	}
      return TRUE;
    }

  fprintf (output, "@@@STRUCT_STUB %s%s\n", stub->name, privacy_string);
  if (stub->xml_blurb != NULL)
    fputs (stub->xml_blurb, output);
  return TRUE;
}

static gboolean   render_template_entry   (COEntry      *entry,
					   CODatabase   *database,
                                           FILE         *output)
{
  if (entry->enumeration != NULL)
    return render_template_enumeration (entry->enumeration, output);
  if (entry->function != NULL)
    return render_template_function (entry->function, FALSE, output);
  if (entry->function_typedef != NULL)
    return render_template_function (entry->function_typedef, TRUE, output);
  if (entry->structure != NULL)
    return render_template_structure (entry->structure, output);
  if (entry->the_union != NULL)
    return render_template_union (entry->the_union, output);
  if (entry->global != NULL)
    return render_template_global (entry->global, output);
  if (entry->struct_stub != NULL)
    return render_template_struct_stub (entry->struct_stub, database, output);

  g_warning ("render_template_entry: empty entry?");
  return FALSE;
}

static gboolean   render_template_section (COSection    *section,
					   CODatabase   *database,
                                           FILE         *output)
{
  GSList *list;
  if (section->title != NULL)
    fprintf (output, "@@@SECTION %s %s\n", section->h_filename, section->title);
  else
    fprintf (output, "@@@SECTION %s\n", section->h_filename);
  
  if (section->xml_blurb != NULL)
    fprintf (output, "%s", section->xml_blurb);

  for (list = section->first_entry; list != NULL; list = list->next)
    {
      COEntry *entry = list->data;
      if (! render_template_entry (entry, database, output))
        return FALSE;
    }
  return TRUE;
}

static void add_omission_line (gpointer         key,
                               gpointer         value,
			       gpointer         user_data)
{
  FILE *fp = user_data;
  (void) value;
  fprintf (fp, "@@@OMIT %s\n", (char*) key);
}

static gboolean   co_database_save     (CODatabase       *database,
                                        const char       *filename)
{
  FILE *fp;
  GSList *list;
  fp = fopen (filename, "w");
  if (fp == NULL)
    {
      g_warning ("error creating %s", filename);
      return FALSE;
    }
  for (list = database->subdirs; list != NULL; list = list->next)
    fprintf (fp, "@@@SUBDIR %s\n", (char*) (list->data));
  g_hash_table_foreach (database->omissions,
                        (GHFunc) add_omission_line,
			fp);
  if (database->xml_filename != NULL)
    fprintf (fp, "@@@SGML_FILE %s\n", database->xml_filename);
  for (list = database->first_section; list != NULL; list = list->next)
    {
      COSection *section = list->data;
      if (! render_template_section (section, database, fp))
        return FALSE;
    }
  fclose (fp);
  return TRUE;
}

gboolean          co_database_safe_save_old(CODatabase       *database,
                                            const char       *filename)
{
  char *tmp_filename;
  char *user;
  char *backup_filename;
  user = getenv ("USER");
  if (user == NULL)
    user = "unknown";
  tmp_filename = g_strdup_printf ("%s.tmp-%d-%s", filename, getpid (), user);
  if (! co_database_save (database, tmp_filename))
    {
      g_warning ("saving %s failed", filename);
      unlink (tmp_filename);
      return FALSE;
    }
   backup_filename = g_strdup_printf ("%s.backup", filename);
   unlink (backup_filename);
   if (rename (filename, backup_filename) < 0 && errno != ENOENT)
     {
       g_warning ("couldn't move %s to %s: %s", filename, backup_filename,
       	    strerror (errno));
       return FALSE;
     }
   if (rename (tmp_filename, filename) < 0)
     {
       g_warning ("couldn't move %s to %s: %s", tmp_filename, filename,
       	    strerror (errno));
       g_warning ("restoring backup: %s => %s", backup_filename, filename);
       if (rename (backup_filename, filename) < 0)
	 g_warning ("error restoring backup %s", filename);
       return FALSE;
     }
  return TRUE;
}
#endif


