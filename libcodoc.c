#include "libcodoc.h"
#include "hfile.h"
#include "xmlmemory.h"
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


/*
 * context_warning  and  context_message
 */
void
context_message      (COContext        *context,
		      const char       *format,
		      ...)
{
  va_list args;
  char *str;
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  if (context == NULL)
    g_message ("[no context available]: %s", str);
  else
    g_message ("%s:%d: %s", context->file, context->lineno, str);
  g_free(str);
}
void
context_warning       (COContext        *context,
		       const char       *format,
                       ...)
{
  va_list args;
  char *str;
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  if (context == NULL)
    g_warning ("[no context available]: %s", str);
  else
    g_warning ("%s:%d: %s", context->file, context->lineno, str);
  g_free(str);
}


 

/* note:  xml_blurb is passed to be incorporated into the
 *        tree directly.  only if a conflict occurs is copying
 *        necessary.  so...  always assume `xml_blurb' will
 *        be freed by the callee (that is, the function being called).
 */

static inline gboolean
is_word_char (char c)
{
  return (isalnum (c) || c == '_');
}

CODatabase *
co_database_new      ()
{
  CODatabase *database;
  database = g_new0(CODatabase, 1);
  database->functions = g_hash_table_new (g_str_hash, g_str_equal);
  database->function_typedefs = g_hash_table_new (g_str_hash, g_str_equal);
  database->structures = g_hash_table_new (g_str_hash, g_str_equal);
  database->struct_stubs = g_hash_table_new (g_str_hash, g_str_equal);
  database->enumerations = g_hash_table_new (g_str_hash, g_str_equal);
  database->sections = g_hash_table_new (g_str_hash, g_str_equal);
  database->globals = g_hash_table_new (g_str_hash, g_str_equal);
  database->unions = g_hash_table_new (g_str_hash, g_str_equal);
  database->omissions = g_hash_table_new (g_str_hash, g_str_equal);
  return database;
}

#define LEFT_PAREN_CHAR '('
#define RIGHT_PAREN_CHAR ')'
#define LEFT_BRACKET_CHAR '['
#define RIGHT_BRACKET_CHAR ']'

#define SKIP_CHAR_TYPE(ptr, cond) while (*(ptr) != '\0' && cond(*(ptr))) ptr++
#define SKIP_WHITESPACE(ptr)      SKIP_CHAR_TYPE(ptr, isspace)
#define SKIP_NON_WHITESPACE(ptr)  SKIP_CHAR_TYPE(ptr, !isspace)
#define SKIP_DIGITS(ptr)          SKIP_CHAR_TYPE(ptr, isdigit)

static char *
canon_type (const char *type)
{
  GString *rv = g_string_new ("");
  char *rv_str;
  gboolean last_was_space = TRUE;
  while (*type != 0 && *type != '*')
    {
      if (isspace (*type) && last_was_space)
	;
      else
	g_string_append_c (rv, *type);
      last_was_space = isspace (*type);
      type++;
    }
  if (*type == '*' && !last_was_space)
    g_string_append_c (rv, ' ');
  while (*type != 0)
    {
      if (!isspace (*type))
	g_string_append_c (rv, *type);
      type++;
    }
  rv_str = rv->str;
  g_string_free (rv, FALSE);
  return rv_str;
}

/*
 *        _ _                 _   _
 *   __ _| | | ___   ___ __ _| |_(_) ___  _ __
 *  / _` | | |/ _ \ / __/ _` | __| |/ _ \| '_ \
 * | (_| | | | (_) | (_| (_| | |_| | (_) | | | |
 *  \__,_|_|_|\___/ \___\__,_|\__|_|\___/|_| |_|
 * 
 */

static gboolean are_xml_lists_equal (xmlNode *a,
	                             xmlNode *b);
static gboolean
are_xml_equal (xmlNode *a,
	       xmlNode *b)
{
  if (a->type != b->type)
    return FALSE;
  switch (a->type)
    {
    case XML_ELEMENT_NODE:
      return strcmp (a->name, b->name) == 0
	 && are_xml_lists_equal (a->childs, b->childs);
    case XML_TEXT_NODE:
      {
	char *cona = xmlNodeGetContent (a);
	char *conb = xmlNodeGetContent (b);
	gboolean rv = strcmp (cona, conb) == 0;
#if 0	/*HACK*/
	xmlFree (cona);
	xmlFree (conb);
#endif	/*HACK*/
	return rv;
      }
    default:
      return FALSE;
    }
}
static gboolean
are_xml_lists_equal (xmlNode *a,
		     xmlNode *b)
{
  while (a != NULL && b != NULL)
    {
      if (!are_xml_equal (a, b))
	return FALSE;
      a = a->next;
      b = b->next;
    }
  return a == NULL && b == NULL;
}

/*
 *                    assign_xml_blurb
 *
 * Copy one xml blurb over another, discarding either
 * if it is NULL or whitespace; if both exist, a conflict
 * message replaces *dest_blurb.
 */
static void
assign_xml_blurb(xmlNode    **dest_blurb,
		 xmlNode    **conflict_blurb,
		 xmlNode     *src_blurb,
		 xmlNode     *src_conflict)
{
  xmlNode *at;
  if (src_blurb == NULL)
    ;
  else if (*dest_blurb == NULL)
    *dest_blurb = src_blurb;
  else if (are_xml_equal (*dest_blurb, src_blurb))
    xmlFreeNodeList (src_blurb);
  else if (*conflict_blurb == NULL)
      *conflict_blurb = src_blurb;
  else
    {
      for (at = *conflict_blurb; at != NULL; at = at->next)
	if (are_xml_equal (at, src_blurb))
	  {
	    xmlFreeNodeList (src_blurb);
	    break;
	  }
      if (at == NULL)
	xmlAddSibling (*conflict_blurb, src_blurb);
    }
  if (src_conflict != NULL)
    assign_xml_blurb (dest_blurb, conflict_blurb, src_conflict, NULL);
}

static COParameter *
co_parameter_new  (const char  *type,
		   char        *name,
		   xmlNode     *xml_blurb,
		   xmlNode     *conflict)
{
  COParameter *parameter;
  parameter = g_new0 (COParameter, 1);
  parameter->type = canon_type (type);
  parameter->name = name;
  parameter->blurb = xml_blurb;
  parameter->conflict = conflict;
  return parameter;
}

#define ASSIGN_XML_BLURB(object, xml_blurb, conflict, context)		\
  G_STMT_START{								\
    assign_xml_blurb(&(object)->blurb, &(object)->conflict,		\
		     (xml_blurb), (conflict));				\
  }G_STMT_END
#define COMBINE_FLAGS(object, flags, context)				\
  G_STMT_START{								\
      (object)->flags |= flags;						\
  }G_STMT_END
#define FREE_BLURBS(object)						\
  G_STMT_START{								\
    xmlFreeNodeList (object->blurb);					\
    xmlFreeNodeList (object->conflict);					\
  }G_STMT_END

void
co_database_new_section (CODatabase   *database,
			 const char   *h_filename,
			 const char   *title,
			 xmlNode      *xml_blurb,
			 xmlNode      *conflict,
			 COEntryFlags  flags,
			 COContext    *context)
{
  COSection *section;
  section = g_hash_table_lookup (database->sections, h_filename);
  (void) context;
  if (section == NULL)
    {
      section = g_new0 (COSection, 1);
      section->h_filename = g_strdup (h_filename);
      g_hash_table_insert (database->sections, section->h_filename, section);
      database->last_section = g_slist_append (database->last_section, section);
      /* add it to the section list */
      if (database->first_section == NULL)
        database->first_section = database->last_section;
      else
        database->last_section = database->last_section->next;
    }
  if (title != NULL)
    {
      if (section->title != NULL)
        g_free (section->title);
      section->title = g_strdup (title);
    }
  ASSIGN_XML_BLURB (section, xml_blurb, conflict, context);
  COMBINE_FLAGS (section, flags, context);
  database->default_section = section;
}
static COEntry *
get_entry (CODatabase    *database,
	   const char    *classification,
	   const char    *name,
	   COContext     *context)
{
  COEntry *entry;
  COSection *section = database->default_section;
  if (section == NULL)
    {
      context_warning (context,
                 "new %s: %s: no section to incorporate this object into",
                 classification, name);
      return NULL;
    }
  entry = g_new0 (COEntry, 1);
  if (section->first_entry == NULL)
    {
      section->first_entry = g_slist_prepend (NULL, entry);
      section->last_entry = section->first_entry;
    }
  else
    {
      g_slist_append (section->last_entry, entry);
      section->last_entry = section->last_entry->next;
    }
  return entry;
}

void
co_database_add_function (CODatabase      *database,
			  gboolean         is_typedef,
			  const char      *function_name,
			  const char      *return_type,
			  xmlNode         *xml_blurb,
			  xmlNode         *conflict,
			  COEntryFlags     flags,
			  COContext       *context)
{
  COFunction *function;
  GHashTable *table;
  char *chomped;
  table = is_typedef ? database->function_typedefs : database->functions;
  function = g_hash_table_lookup (table, function_name);

  if (return_type != NULL)
    {
      chomped = alloca (strlen(return_type) + 1);
      strcpy (chomped, return_type);
      g_strchomp (chomped);
      return_type = chomped;
    }

  if (function == NULL)
    {
      char *name;
      COEntry *entry;
      function = g_new0(COFunction, 1);
      name = g_strdup (function_name);
      function->name = name;
      function->return_type = g_strdup (return_type);
      g_hash_table_insert (table, name, function);

      /* add this to the current section list. */
      entry = get_entry (database, "function", function_name, context);
      g_return_if_fail (entry != NULL);
      if (is_typedef)
        entry->function_typedef = function;
      else
        entry->function = function;
    }
  else if (return_type != NULL)
    {
      char *r = canon_type (return_type);
      if (function->return_type == NULL)
        function->return_type = r;
      else if (strcmp(function->return_type, r) != 0)
        {
          context_warning (context, "return value of %s changed from %s to %s",
            function_name, function->return_type, return_type);
          g_free (function->return_type);
          function->return_type = r;
        }
      else
	{
	  g_free (r);
	}
    }
  ASSIGN_XML_BLURB (function, xml_blurb, conflict, context);
  COMBINE_FLAGS (function, flags, context);
}

void
co_database_add_function_param (CODatabase      *database,
				gboolean         is_typedef,
				const char      *function_name,
				int              param_index,
				const char      *parameter_name,
				const char      *parameter_type,
				xmlNode         *xml_blurb,
				xmlNode         *conflict,
				COEntryFlags     flags,
				COContext       *context)
{
  COFunction *function;
  COParameter *param;
  co_database_add_function (database, is_typedef, function_name, NULL, NULL,
			    NULL, 0, context);
  function = g_hash_table_lookup (is_typedef ? database->function_typedefs
				             : database->functions,
				  function_name);
  g_assert (function != NULL);
  if (param_index >= function->num_parameters)
    {
      int new_num = param_index + 1;
      function->parameters = g_renew (COParameter*,
                                      function->parameters,
                                      new_num);
      if (function->num_parameters < 0)
	function->num_parameters = 0;
      while (function->num_parameters < new_num)
        function->parameters[function->num_parameters++] = NULL;
    }
  if (function->parameters[param_index] == NULL)
    {
      param = g_new0(COParameter, 1);
      function->parameters[param_index] = param;
      param->type = canon_type (parameter_type);
      param->name = g_strdup (parameter_name);
    }
  else
    {
      char *param_type = canon_type (parameter_type);
      param = function->parameters[param_index];

      if (strcmp (param_type, param->type) != 0)
        {
          context_warning (context, "type of parameter %d to %s: %s "
                        "switched from `%s' to `%s'",
                param_index, function_name, parameter_name,
                param->type, param_type);
          g_free (param->type);
          param->type = param_type;
        }
      else
	{
	  g_free (param_type);
	}
      if (strcmp (parameter_name, param->name) != 0)
        {
          context_warning (context,
                  "name of parameter %d to %s switched from `%s' to `%s'",
                  param_index, function_name, param->name, parameter_name);
          g_free (param->name);
          param->name = g_strdup (parameter_name);
        }
    }
  g_assert (function->parameters[param_index] != NULL);
  g_assert (function->parameters[param_index] == param);
  ASSIGN_XML_BLURB (param, xml_blurb, conflict, context);
  COMBINE_FLAGS (param, flags, context);
}

void
co_database_add_struct_stub (CODatabase      *database,
			     const char      *struct_name,
			     xmlNode         *xml_blurb,
			     xmlNode         *conflict,
			     COEntryFlags     flags,
			     COContext       *context)
{
  COStructure *structure;
  char *name;
  COEntry *entry;
  COStructStub *struct_stub;
  structure = g_hash_table_lookup (database->structures, struct_name);
#if 0		/* skip blank? why was this needed? */
  if (xml_blurb != NULL)
    {
      const char *tmp;
      tmp = xml_blurb;
      SKIP_WHITESPACE (tmp);
      if (*tmp == '\0')
        {
          g_free (xml_blurb);
          xml_blurb = NULL;
        }
    }
#endif
  if (structure != NULL && xml_blurb == NULL)
    return;
  
  if (g_hash_table_lookup (database->struct_stubs, struct_name) != NULL)
    return;
  struct_stub = g_new0(COStructStub, 1);
  name = g_strdup (struct_name);
  struct_stub->name = name;
  g_hash_table_insert (database->struct_stubs, name, struct_stub);

  /* add this to the current section list. */
  entry = get_entry (database, "struct_stub", name, context);
  g_return_if_fail (entry != NULL);
  entry->struct_stub = struct_stub;
  ASSIGN_XML_BLURB (struct_stub, xml_blurb, conflict, context);
  COMBINE_FLAGS (struct_stub, flags, context);
}

void
co_database_add_struct (CODatabase      *database,
			const char      *struct_name,
			xmlNode         *xml_blurb,
			xmlNode         *conflict,
			COEntryFlags     flags,
			COContext       *context)
{
  COStructure *structure;
  structure = g_hash_table_lookup (database->structures, struct_name);
  if (structure == NULL)
    {
      char *name;
      COEntry *entry;
      structure = g_new0(COStructure, 1);
      structure->members = g_hash_table_new (g_str_hash, g_str_equal);
      name = g_strdup (struct_name);
      structure->name = name;
      g_hash_table_insert (database->structures, name, structure);

      /* add this to the current section list. */
      entry = get_entry (database, "structure", struct_name, context);
      g_return_if_fail (entry != NULL);
      entry->structure = structure;
    }
  ASSIGN_XML_BLURB (structure, xml_blurb, conflict, context);
  COMBINE_FLAGS (structure, flags, context);
}

void
co_database_add_struct_member  (CODatabase      *database,
				const char      *struct_name,
				const char      *member_name,
				const char      *member_type,
				xmlNode         *xml_blurb,
				xmlNode         *conflict,
				COEntryFlags     flags,
				COContext       *context)
{
  COStructure *structure;
  COMember *member;
  co_database_add_struct (database, struct_name, NULL, NULL, 0, context);
  structure = g_hash_table_lookup (database->structures, struct_name);

  member = g_hash_table_lookup (structure->members, member_name);
  if (member == NULL)
    {
      int old_num_members;
      member = g_new0 (COMember, 1);
      member->name = g_strdup (member_name);
      member->type = canon_type (member_type);

      old_num_members = g_hash_table_size (structure->members);
      g_hash_table_insert (structure->members, member->name, member);
      structure->member_names = g_renew(char*, structure->member_names,
                                        old_num_members + 1);
      structure->member_names[old_num_members] = member->name;
    }
  else
    {
      char *tmp = canon_type (member_type);
      if (strcmp (member->type, tmp) != 0)
        {
          context_warning (context,
                           "member %s::%s changed type from `%s' to `%s'",
                           struct_name, member_name, member->type, member_type);
          g_free (member->type);
          member->type = tmp;
        }
      else
        g_free (tmp);
    }
  g_assert (member != NULL);
  ASSIGN_XML_BLURB (member, xml_blurb, conflict, context);
  COMBINE_FLAGS (member, flags, context);
}

static void free_parameter_array (COParameter   **parameters,
                                  int             num_parameters);

void
co_database_add_member_function (CODatabase      *database,
				 const char      *struct_name,
				 const char      *name,
				 const char      *return_type,
				 int              num_parameters,
				 COParameter    **parameters,
				 xmlNode         *xml_blurb,
				 xmlNode         *conflict,
				 COEntryFlags     flags,
				 COContext       *context)
{
  COStructure *structure;
  COMember *member;
  co_database_add_struct (database, struct_name, NULL, NULL, 0, context);
  structure = g_hash_table_lookup (database->structures, struct_name);
  g_assert (structure != NULL);
  member = g_hash_table_lookup (structure->members, name);
  if (member == NULL)
    {
      int old_num_members;
      member = g_new0 (COMember, 1);
      member->name = g_strdup (name);
      if (return_type != NULL)
        member->type = canon_type (return_type);
      member->num_parameters = num_parameters;
      member->parameters = parameters;
      member->is_function = TRUE;

      old_num_members = g_hash_table_size (structure->members);
      g_hash_table_insert (structure->members, member->name, member);
      structure->member_names = g_renew(char*, structure->member_names,
                                        old_num_members + 1);
      structure->member_names[old_num_members] = member->name;
    }
  else
    {
      if (return_type != NULL)
        {
          char *tmp = canon_type (return_type);
	  if (member->type == NULL)
	    {
	      member->type = tmp;
	    }
	  else if (strcmp (member->type, tmp) != 0)
            {
              context_warning (context,
                      "method %s::%s changed return type from `%s' to `%s'",
                      struct_name, name, member->type, tmp);
              g_free (member->type);
              member->type = tmp;
            }
          else
            g_free (tmp);
        }

      if (! member->is_function)
        {
          context_warning (context,
                     "member %s::%s switched from non-function to function: "
                     "hand-remove from your template.", struct_name, name);
          exit (1);
        }

      if (num_parameters != -1)
        {
          if (member->num_parameters != num_parameters)
            {
              context_warning (context, "method %s::%s: "
                         "number of parameters changed from %d to %d",
                       struct_name, name,
                       member->num_parameters, num_parameters);
            }
          else
            {
              int i;
              for (i = 0; i < num_parameters; i++)
                if (member->parameters[i] != NULL
                 && strcmp (member->parameters[i]->type,
                            parameters[i]->type) != 0)
                  {
                    context_warning (context, "method %s::%s, parameter %d: "
                               "switched type from %s to %s",
                               struct_name, name, i,
                               member->parameters[i]->type,
                               parameters[i]->type);
                  }
            }
            {
              int i;
              for (i = 0; i < num_parameters && i < member->num_parameters; i++)
                {
                  xmlNode *blurb;
                  if (member->parameters[i] == NULL)
                    continue;
                  blurb = member->parameters[i]->blurb;
                  member->parameters[i]->blurb = NULL;
		  /* what about 'conflict'? */
                  ASSIGN_XML_BLURB (parameters[i], blurb, conflict, context);
                }
            }
          free_parameter_array (member->parameters, member->num_parameters);
          member->parameters = parameters;
          member->num_parameters = num_parameters;
        }
    }
  g_assert (member != NULL);
  ASSIGN_XML_BLURB (member, xml_blurb, conflict, context);
  COMBINE_FLAGS (member, flags, context);
}

void
co_database_add_struct_member_param (CODatabase    *database,
				     const char    *struct_name, 
				     const char    *method_name,
				     int            param_index,
				     const char    *param_name,
				     const char    *param_type,
				     xmlNode       *xml_blurb,
				     xmlNode         *conflict,
				     COEntryFlags     flags,
				     COContext     *context)
{
  COStructure *structure;
  COMember *member;
  COParameter *param;
  co_database_add_member_function (database, struct_name,
				   method_name, NULL,
				   -1, NULL, NULL, NULL, 0, context);
  structure = g_hash_table_lookup (database->structures, struct_name);
  member = g_hash_table_lookup (structure->members, method_name);
  g_assert (member != NULL);
  if (param_index >= member->num_parameters)
    {
      int old_num_params = member->num_parameters;
      if (old_num_params < 0)
        old_num_params = 0;
      member->parameters = g_renew (COParameter*,
                                    member->parameters,
                                    param_index + 1);
      while (old_num_params <= param_index)
        member->parameters[old_num_params++] = NULL;
      member->num_parameters = param_index + 1;
    }
  param = member->parameters[param_index];
  if (param != NULL)
    {
      char *new_type = canon_type (param_type);
      if (strcmp (param->type, new_type) != 0)
        {
          context_warning (context, "%s::%s: param %d changed from %s to %s",
            struct_name, method_name, param_index, param->type, new_type);
        }
      g_free (param->type);
      param->type = new_type;

      if (param_name != NULL)
        {
          g_free (param->name);
          param->name = g_strdup (param_name);
        }
      ASSIGN_XML_BLURB (param, xml_blurb, conflict, context);
      COMBINE_FLAGS (param, flags, context);
    }
  else
    {
      param = co_parameter_new (param_type,
                                g_strdup (param_name),
                                xml_blurb,
				conflict);
      member->parameters[param_index] = param;
    }
}

void
co_database_add_enum (CODatabase      *database,
		      const char      *enum_name,
		      xmlNode         *xml_blurb,
		      xmlNode         *conflict,
		      COEntryFlags     flags,
		      COContext       *context)
{
  COEnumeration *enumeration;
  enumeration = g_hash_table_lookup (database->enumerations, enum_name);
  if (enumeration == NULL)
    {
      char *name;
      COEntry *entry;
      enumeration = g_new0(COEnumeration, 1);
      name = g_strdup (enum_name);
      enumeration->name = name;
      enumeration->values = g_hash_table_new (g_str_hash, g_str_equal);
      g_hash_table_insert (database->enumerations, name, enumeration);

      /* add this to the current section list. */
      entry = get_entry (database, "enumeration", enum_name, context);
      g_return_if_fail (entry != NULL);
      entry->enumeration = enumeration;
    }
  ASSIGN_XML_BLURB (enumeration, xml_blurb, conflict, context);
  COMBINE_FLAGS (enumeration, flags, context);
}

void
co_database_add_enum_value     (CODatabase      *database,
				const char      *enum_name,
				const char      *value_name,
				xmlNode         *xml_blurb,
				xmlNode         *conflict,
				COEntryFlags     flags,
				COContext       *context)
{
  COEnumeration *enumeration;
  COEnumerationValue *value;
  co_database_add_enum (database, enum_name, NULL, NULL, 0, context);
  enumeration = g_hash_table_lookup (database->enumerations, enum_name);
  g_assert (enumeration != NULL);
  value = g_hash_table_lookup (enumeration->values, value_name);
  if (value == NULL)
    {
      value = g_new0 (COEnumerationValue, 1);
      value->name = g_strdup (value_name);
      g_hash_table_insert (enumeration->values, value->name, value);
    }
  ASSIGN_XML_BLURB (value, xml_blurb, conflict, context);
  COMBINE_FLAGS (value, flags, context);
}

/* the type_name_pairs will be freed by the union object. */
void
co_database_add_union          (CODatabase      *database,
				const char      *union_name,
				int              num_pairs,
				char           **type_name_pairs,
				xmlNode         *xml_blurb,
				xmlNode         *conflict,
				COEntryFlags     flags,
				COContext       *context)
{
  COUnion *un;
  un = g_hash_table_lookup (database->unions, union_name);
  if (un == NULL)
    {
      COEntry *entry;
      un = g_new0 (COUnion, 1);
      un->name = g_strdup (union_name);
      un->num_objects = num_pairs;
      un->type_name_pairs = type_name_pairs;
      entry = get_entry (database, "union", union_name, context);
      entry->the_union = un;
      g_hash_table_insert (database->unions, un->name, un);
      {
	int i;
	for (i = 0; i < num_pairs; i++)
	  {
	    char *tmp = canon_type (type_name_pairs[2*i]);
	    g_free (type_name_pairs[2*i]);
	    type_name_pairs[2*i] = tmp;
	  }
      }
    }
  else
    {
      if (num_pairs != -1)
        {
          int i;
          for (i = 0; i < un->num_objects * 2; i++)
            g_free (un->type_name_pairs[i]);
          g_free (un->type_name_pairs);
          un->num_objects = num_pairs;
          un->type_name_pairs = type_name_pairs;
        }
    }
  ASSIGN_XML_BLURB (un, xml_blurb, conflict, context);
  COMBINE_FLAGS (un, flags, context);
}

void
co_database_add_global         (CODatabase      *database,
				const char      *name,
				const char      *type,
				xmlNode         *xml_blurb,
				xmlNode         *conflict,
				COEntryFlags     flags,
				COContext       *context)
{
  COGlobal *global;
  global = g_hash_table_lookup (database->globals, name);
  if (global == NULL)
    {
      COEntry *entry;
      global = g_new0 (COGlobal, 1);
      global->name = g_strdup (name);
      global->type = g_strdup (type);
      g_strchomp (global->type);
      entry = get_entry (database, "global", name, context);
      entry->global = global;
      g_hash_table_insert (database->globals, global->name, global);
    }
  else
    {
      char *tmp;
      tmp = g_strdup (type);
      g_strchomp (tmp);
      if (strcmp (tmp, global->type) != 0)
        {
          context_warning (context, "global %s:  switched type from %s to %s",
                global->name, global->type, tmp);
        }
      g_free (global->type);
      global->type = tmp;
    }
  ASSIGN_XML_BLURB (global, xml_blurb, conflict, context);
  COMBINE_FLAGS (global, flags, context);
}
/*
 *                   __ _                       _   _
 *   ___ ___  _ __  / _(_) __ _ _   _ _ __ __ _| |_(_) ___  _ __
 *  / __/ _ \| '_ \| |_| |/ _` | | | | '__/ _` | __| |/ _ \| '_ \
 * | (_| (_) | | | |  _| | (_| | |_| | | | (_| | |_| | (_) | | | |
 *  \___\___/|_| |_|_| |_|\__, |\__,_|_|  \__,_|\__|_|\___/|_| |_|
 *                        |___/
 */

void
co_database_set_cpp_flags   (CODatabase       *database,
			     const char       *cpp_flags)
{
  g_return_if_fail (cpp_flags != NULL);
  if (database->cpp_flags != NULL)
    g_free (database->cpp_flags);
  database->cpp_flags = g_strdup (cpp_flags);
}

static void
add_unique (GSList **plist, const char *addend)
{
  GSList *list;
  for (list = *plist; list != NULL; list = list->next)
    {
      const char *tmp = list->data;
      if (strcmp (tmp, addend) == 0)
        return;
    }
  *plist = g_slist_append (*plist, g_strdup (addend));
}

void
co_database_add_subdir      (CODatabase       *database,
			     const char       *subdir)
{
  add_unique (&database->subdirs, subdir);
}
void
co_database_add_omission    (CODatabase       *database,
			     const char       *omission)
{
  if (g_hash_table_lookup (database->omissions, omission) != NULL)
    return;
  g_hash_table_insert (database->omissions, g_strdup (omission),
                       GINT_TO_POINTER (1));
}


static char *
copy_between (const char *start,
	      const char *end)
{
  char *rv;
  rv = g_new (char, end - start + 1);
  memcpy (rv, start, end - start);
  rv[end - start] = '\0';
  return rv;
}

static gboolean
is_word_ws (const char **pstr,
	    const char *str)
{
  const char *at = *pstr;
  int len;
  SKIP_WHITESPACE (at);
  len = strlen (str);
  if (strncmp (str, at, len) != 0)
    return FALSE;
  at += len;
  if (*at == '\0')
    {
      *pstr = at;
      return TRUE;
    }
  if (is_word_char (*at))
    return FALSE;
  SKIP_WHITESPACE (at);
  *pstr = at;
  return TRUE;
}

/*
 *  _      __ _ _                             _
 * | |__  / _(_) | ___   _ __   __ _ _ __ ___(_)_ __   __ _
 * | '_ \| |_| | |/ _ \ | '_ \ / _` | '__/ __| | '_ \ / _` |
 * | | | |  _| | |  __/ | |_) | (_| | |  \__ \ | | | | (_| |
 * |_| |_|_| |_|_|\___| | .__/ \__,_|_|  |___/_|_| |_|\__, |
 *                      |_|                           |___/
 */

/* this function advances *pstr to the end of a c-type
 * (at least, it's plausibly a c-type) *pstr ends up being,
 * e.g. a member name or a local variable, if we parsed c files ;)
 */
gboolean
codoc_util_is_c_type (const char **pstr)
{
  const char *start = *pstr;
  const char *at;
  const char *tmp;
  SKIP_WHITESPACE (start);
  at = start;
  if (is_word_ws (&at, "typedef")
  ||  is_word_ws (&at, "static"))
    return FALSE;

  (void) is_word_ws (&at, "const");
  if (is_word_ws (&at, "struct")
   || is_word_ws (&at, "enum")
   || is_word_ws (&at, "union"))
    {
      /* Parse just one word: the object's name. */
      if (! is_word_char (*at))
        return FALSE;
      SKIP_CHAR_TYPE (at, is_word_char);
      SKIP_WHITESPACE (at);
      *pstr = at;
    }
  else if (is_word_ws (&at, "unsigned"))
    {
      /* deal with the usual multi word combos */
      (void) ( is_word_ws (&at, "long")
         ||    is_word_ws (&at, "short"));
      (void) is_word_ws (&at, "int");
    }
  else if (is_word_ws (&at, "long"))
    {
      /* deal with the usual multi word combos */
      (void) is_word_ws (&at, "unsigned");
      (void) ( is_word_ws (&at, "long")
         ||    is_word_ws (&at, "short"));
      (void) is_word_ws (&at, "int");
    }
  else if (is_word_ws (&at, "short"))
    {
      /* deal with the usual multi word combos */
      (void) is_word_ws (&at, "unsigned");
      (void) (is_word_ws (&at, "long")
        ||    is_word_ws (&at, "short"));
      (void) is_word_ws (&at, "int");
    }
  else
    {
      /* typedef'd name */
      tmp = at;
      SKIP_CHAR_TYPE (at, is_word_char);
      if (tmp == at)
        return FALSE;
    }


  /* look for a series of * and const */
  for (;;)
    {
      SKIP_WHITESPACE (at);
      if (*at == '*')
        {
          at++;
          continue;
        }
      if (is_word_ws (&at, "const"))
        continue;

      /* not recognized... */
      break;
    }

  *pstr = at;
  return TRUE;
}
static gboolean
skip_bracketed_indices (const char **ptr)
{
  const char *at = *ptr;
  SKIP_WHITESPACE (at);
  if (*at != LEFT_BRACKET_CHAR)
    return FALSE;
  while (*at == LEFT_BRACKET_CHAR)
    {
      at++;
      SKIP_WHITESPACE (at);
      while (isdigit(*at) && *at != '\0')
        at++;
      SKIP_WHITESPACE (at);
      if (*at != RIGHT_BRACKET_CHAR)
        {
          g_warning ("missing ]");
          at++;
        }
      SKIP_WHITESPACE (at);
    }
  *ptr = at;
  return TRUE;
}

static gboolean
skip_bitfield_delimiter (const char **ptr)
{
  const char *at = *ptr;
  if (*at == ':')
    {
      /* bit-field (XXX: maybe should check it is an integer type? nah...)
       * also, we force the poor user to use an actual integer here.
       * (for no real reason too...)
       */
      at++;     /* skip colon */
      SKIP_WHITESPACE (at);
      SKIP_DIGITS (at);
      SKIP_WHITESPACE (at);
      *ptr = at;
      return TRUE;
    }
  return FALSE;
}

static gboolean
parse_arg_triple (const char    **rv_at, 
		  char          **rv_type,
		  char          **rv_name,
		  char          **rv_posttype,
		  int             param_index)
{
  const char *at;
  const char *end;
  at = *rv_at;
  SKIP_WHITESPACE (at);
  end = at;
  if (codoc_util_is_c_type (&end))
    {
      *rv_type = copy_between (at, end);
      g_strchomp (*rv_type);
      at = end;
      SKIP_WHITESPACE (at);
      end = at;
      SKIP_CHAR_TYPE (end, is_word_char);
      if (at == end)
        {
          if (param_index < 0)
            return FALSE;
          *rv_name = g_strdup_printf ("unnamed%d", param_index);
        }
      else
        {
          *rv_name = copy_between (at, end);
          at = end;
          SKIP_WHITESPACE (at);
        }
      end = at;
      skip_bracketed_indices (&end);
      skip_bitfield_delimiter (&end);

      if (end == at)
        *rv_posttype = NULL;
      else
        *rv_posttype = copy_between (at, end);
      SKIP_WHITESPACE (at);
      *rv_at = at;
      return TRUE;
    }
  else
    {
      g_warning("not a type %s", at);
    }
  return FALSE;
}

/*
 *                           _
 *  _ __ ___   ___ _ __ ___ | |__   ___ _ __
 * | '_ ` _ \ / _ \ '_ ` _ \| '_ \ / _ \ '__|
 * | | | | | |  __/ | | | | | |_) |  __/ |
 * |_| |_| |_|\___|_| |_| |_|_.__/ \___|_|
 * 
 *   __                  _   _
 *  / _|_   _ _ __   ___| |_(_) ___  _ __  ___
 * | |_| | | | '_ \ / __| __| |/ _ \| '_ \/ __|
 * |  _| |_| | | | | (__| |_| | (_) | | | \__ \
 * |_|  \__,_|_| |_|\___|\__|_|\___/|_| |_|___/
 * 
 */
static gboolean
is_member_function    (const char        *text)
{
  const char *at = text;
  if (! codoc_util_is_c_type (&at))
    return FALSE;
  SKIP_WHITESPACE (at);
  if (*at != LEFT_PAREN_CHAR)
    return FALSE;
  at++;
  SKIP_WHITESPACE (at);
  if (*at != '*')
    return FALSE;
  at++;
  SKIP_WHITESPACE (at);
  if (! is_word_char (*at))
    return FALSE;
  SKIP_CHAR_TYPE (at, is_word_char);
  SKIP_WHITESPACE (at);
  if (*at != RIGHT_PAREN_CHAR)
    return FALSE;
  return TRUE;
}

static gboolean
parse_member_function (const char        *text,
		       const char        *struct_name,
		       char             **return_type,
		       char             **function_name,
		       int               *num_parameters,
		       COParameter     ***parameters)
{
  const char *at = text;
  const char *end;
  GPtrArray *param_array;
  SKIP_WHITESPACE (at);
  end = at;
  if (! codoc_util_is_c_type (&end))
    {
      g_warning ("parse_member_function: error parsing return type");
      return FALSE;
    }
  *return_type = copy_between (at, end);
  g_strchomp (*return_type);

  at = end;
  SKIP_WHITESPACE (at);
  g_return_val_if_fail (*at == LEFT_PAREN_CHAR, FALSE);
  at++;
  SKIP_WHITESPACE (at);
  g_return_val_if_fail (*at == '*', FALSE);
  at++;
  SKIP_WHITESPACE (at);
  g_return_val_if_fail (is_word_char (*at), FALSE);
  end = at;
  SKIP_CHAR_TYPE (end, is_word_char);
  *function_name = copy_between (at, end);
  at = end;
  SKIP_WHITESPACE (at);
  g_return_val_if_fail (*at == RIGHT_PAREN_CHAR, FALSE);
  at++;
  SKIP_WHITESPACE (at);
  if (*at != LEFT_PAREN_CHAR)
    {
      g_warning ("error parsing method %s::%s, "
                 "missing open-paren", struct_name, *function_name);
      return FALSE;
    }
  at++;
  SKIP_WHITESPACE (at);

  /* Check for, and special case, (void) */
  if (strncmp (at, "void", 4) == 0)
    {
      const char *tmp = at + 4;
      SKIP_WHITESPACE (tmp);
      if (*tmp == RIGHT_PAREN_CHAR)
        {
          /* found it... */
          *num_parameters = 0;
          *parameters = NULL;
          return TRUE;
        }
    }

  param_array = g_ptr_array_new ();
  for (;;)
    {
      char *type;
      char *name;
      char *type_suffix;
      COParameter *parameter;
      SKIP_WHITESPACE (at);

      if (*at == RIGHT_PAREN_CHAR)
        break;
      if (*at == ',')
        {
          at++;
          SKIP_WHITESPACE (at);
        }
      if (! parse_arg_triple (&at, &type, &name, &type_suffix,
                              param_array->len))
        {
          g_warning ("error parsing argument to %s::%s",
                     struct_name, *function_name);
          return FALSE;
        }
      parameter = co_parameter_new (type, name, NULL, NULL);
      if (type_suffix != NULL)
        g_free (type_suffix);
      g_free (type);
      g_ptr_array_add (param_array, parameter);
    }

  *parameters = (COParameter**) param_array->pdata;
  *num_parameters = param_array->len;
  g_ptr_array_free (param_array, FALSE);
  
  return TRUE;
}

static void
co_context_init_from_hfile (COContext     *context,
			    HFile         *hfile)
{
  context->file = hfile->any.filename;
  context->lineno = hfile->any.lineno;
  context->in_h_file = TRUE;
  context->privacy_info.is_private = 0;
}

/*
 * Functions:
 *     (const)? type_name \** function_name(param_type_1\** name_1,
 *                                          param_type_2\** name_2);
 * also must handle (void) as an argument list.
 */
static gboolean
hfile_is_function (HFile      *hfile,
		   HFile      *next,
		   gboolean    is_typedef)
{
  const char *at;
  const char *start;
  if (hfile->type != HFILE_TYPE_TEXT
   || next == NULL
   || next->type != HFILE_TYPE_SEMICOLON)
    return FALSE;
  at = HFILE_TEXT (hfile)->text;
  SKIP_WHITESPACE (at);
  if (is_typedef)
    {
      if (strncmp (at, "typedef", 7) != 0)
        return FALSE;
      at += 7;
      if (! isspace (*at))
        return FALSE;
      SKIP_WHITESPACE (at);
    }
  if (! codoc_util_is_c_type (&at))
    return FALSE;
  SKIP_WHITESPACE (at);
  start = at;
  if (is_typedef)
    {
      if (*at != LEFT_PAREN_CHAR)
        return FALSE;
      at++;
      SKIP_WHITESPACE (at);
      if (*at != '*')
        return FALSE;
      at++;
      SKIP_WHITESPACE (at);
      SKIP_CHAR_TYPE (at, is_word_char);
      if (start == at)
        return FALSE;
      SKIP_WHITESPACE (at);
      if (*at != RIGHT_PAREN_CHAR)
        return FALSE;
      at++;
      SKIP_WHITESPACE (at);
    }
  else
    {
      SKIP_CHAR_TYPE (at, is_word_char);
      if (start == at)
        return FALSE;
      SKIP_WHITESPACE (at);
    }
  if (*at != LEFT_PAREN_CHAR)
    return FALSE;
  return TRUE;
}

static gboolean
hfile_parse_function  (CODatabase *database,
		       HFile      *hfile,
		       gboolean    is_typedef)
{
  const char *at;
  const char *tmp_end;
  char *return_type;
  char *function_name;
  int param_index;
  COContext context;

  co_context_init_from_hfile (&context, hfile);

  at = HFILE_TEXT (hfile)->text;
  SKIP_WHITESPACE (at);
  if (is_typedef)
    {
      if (strncmp (at, "typedef", 7) != 0)
        return FALSE;
      at += 7;
      SKIP_WHITESPACE (at);
    }

  tmp_end = at;
  if (! codoc_util_is_c_type (&tmp_end))
    {
      context_warning (&context, "error parsing ret type for function");
      return FALSE;
    }
  return_type = alloca (tmp_end - at + 1);
  memcpy (return_type, at, tmp_end - at);
  return_type[tmp_end - at] = '\0';
  g_strchomp (return_type);
  at = tmp_end;
  SKIP_WHITESPACE (at);

  if (is_typedef)
    {
      if (*at != LEFT_PAREN_CHAR)
        return FALSE;
      at++;
      SKIP_WHITESPACE (at);
      if (*at != '*')
        return FALSE;
      at++;
      SKIP_WHITESPACE (at);

      tmp_end = at;
      SKIP_CHAR_TYPE (tmp_end, is_word_char);
      if (tmp_end == at)
        {
          context_warning (&context, "error getting function name");
          return FALSE;
        }
      function_name = alloca (tmp_end - at + 1);
      memcpy (function_name, at, tmp_end - at);
      function_name[tmp_end - at] = '\0';
      at = tmp_end;

      if (*at != RIGHT_PAREN_CHAR)
        return FALSE;
      at++;
    }
  else
    {
      tmp_end = at;
      SKIP_CHAR_TYPE (tmp_end, is_word_char);
      if (tmp_end == at)
        {
          context_warning (&context, "error getting function name");
          return FALSE;
        }
      function_name = alloca (tmp_end - at + 1);
      memcpy (function_name, at, tmp_end - at);
      function_name[tmp_end - at] = '\0';
      at = tmp_end;
    }

  SKIP_WHITESPACE (at);
  if (*at != LEFT_PAREN_CHAR)
    {
      context_warning (&context, "missing left paren after fct name");
      return FALSE;
    }
  at++;

  co_database_add_function (database, is_typedef,
                            function_name, return_type, NULL, NULL, 0,
                            &context);
  SKIP_WHITESPACE (at);
  /* parse `function(void)' */
  {
    const char *tmp = at;
    if (is_word_ws (&tmp, "void") && *tmp == RIGHT_PAREN_CHAR)
      return TRUE;
  }
      
  /* parse command-separated arguments */
  param_index = 0;
  for (;;)
    {
      char *type;
      char *name;
      char *posttype;
      SKIP_WHITESPACE (at);
      if (*at == RIGHT_PAREN_CHAR)
        break;
      if (strncmp (at, "...", 3) == 0)
        {
          co_database_add_function_param (database, is_typedef, function_name,
                                          param_index, "...",
                                          "...", NULL, NULL, 0, &context);
          break;
        }
          
          
      if (! parse_arg_triple (&at, &type, &name, &posttype, param_index))
        {
          context_warning (&context,
                           "error parsing type triplet to %s()",
                           function_name);
          return FALSE;
        }

      co_database_add_function_param (database, is_typedef, function_name,
                                      param_index, name,
                                      type, NULL, NULL, 0, &context);

      param_index++;
      SKIP_WHITESPACE (at);
      if (*at == ',')
        at++;
    }
  return TRUE;
}

/*
 * Stub structures:
 *     typedef struct _A A;
 */
static gboolean
hfile_is_struct_stub (HFile      *hfile,
		      HFile      *next)
{
  const char *text;
  if (hfile->type != HFILE_TYPE_TEXT
   || next == NULL
   || next->type != HFILE_TYPE_SEMICOLON)
    return FALSE;
  
  text = HFILE_TEXT (hfile)->text;
  SKIP_WHITESPACE (text);
  if (strncmp (text, "typedef", 7) == 0)
    return FALSE;
  text += 7;
  if (! isspace (*text))
    return FALSE;
  SKIP_WHITESPACE (text);

  if (strncmp (text, "struct", 6) == 0)
    return FALSE;
  text += 6;
  if (! isspace (*text))
    return FALSE;
  SKIP_WHITESPACE (text);

  if (*text != '_')
    return FALSE;

  return TRUE;
}

static GSList *
hfile_parse_struct_stub (CODatabase *database,
			 GSList     *start)
{
  const char *text;
  const char *end;
  char *name;
  HFile *hfile = start->data;
  COContext context;
  if (hfile->type != HFILE_TYPE_TEXT
   || start->next == NULL)
    return FALSE;
  
  text = HFILE_TEXT (hfile)->text;
  SKIP_WHITESPACE (text);
  if (strncmp (text, "typedef", 7) == 0)
    return FALSE;
  text += 7;
  if (! isspace (*text))
    return FALSE;
  SKIP_WHITESPACE (text);

  if (strncmp (text, "struct", 6) == 0)
    return FALSE;
  text += 6;
  if (! isspace (*text))
    return FALSE;
  SKIP_WHITESPACE (text);

  if (*text != '_')
    return FALSE;

  text++;
  end = text;
  SKIP_CHAR_TYPE (end, is_word_char);
  if (end == text)
    return FALSE;
  name = copy_between (text, end);
  co_context_init_from_hfile (&context, hfile);
  co_database_add_struct_stub (database, name, NULL, NULL, 0, &context);
  g_free (name);
  return start->next;
}

/*
 * Structures:
 *     struct _StructName {
 *        type_1    member_name_1 type_suffix_1
 *     };
 *   type_suffix is: (\[\d*\])*
 *   must also cope with typedef struct [_A] { ... } A;
 */
static gboolean
hfile_is_structure (HFile      *hfile)
{
  const char *at;
  if (hfile->type != HFILE_TYPE_TEXT)
    return FALSE;
  at = HFILE_TEXT (hfile)->text;
  SKIP_WHITESPACE (at);
  if (strncmp (at, "typedef", 7) == 0)
    {
      at += 7;
      SKIP_WHITESPACE (at);
    }
  if (strncmp (at, "struct", 6) == 0)
    {
      at += 6;
      if (*at == '\0' || isspace(*at))
        return TRUE;
    }
  return FALSE;
}

/* Returns the last node comprising the structure,
 * or NULL in a parse error occurs. */
static GSList *
hfile_parse_structure (CODatabase *database,
		       GSList     *start)
{
  const char *text;
  HFileList *body;
  GSList *at;
  char *struct_name;
  COContext context;
  co_context_init_from_hfile (&context, HFILE (start->data));
  text = HFILE_TEXT (start->data)->text;
  is_word_ws(&text, "typedef");
  if (! is_word_ws(&text, "struct"))
    {
      context_warning (&context, "missing word `struct' in definition");
      return NULL;
    }
  if (*text != '_')
    {
      context_warning (&context, "er, sorry, struct _X please");
      return NULL;
    }
  struct_name = hfile_get_word (text + 1);

  if (((HFile*)start->next->data)->type == HFILE_TYPE_SEMICOLON)
    {
      return start->next;
    }

  g_return_val_if_fail (start->next != NULL, NULL);
  /* hfile_dump ( (HFile*) start->next->data, 0, stderr); */
  
  body = HFILE_LIST (start->next->data);
  g_return_val_if_fail (body != NULL, NULL);
  g_return_val_if_fail (body->has_braces, NULL);

  /* add the structure to the database if needed. */
  co_database_add_struct (database, struct_name, NULL, NULL, 0, &context);

  /* Parse the body. */
  at = body->list;
  while (at != NULL)
    {
      HFile *this_hfile = at->data;
      HFile *next_hfile;
      if (at->next == NULL)
        break;
      next_hfile = at->next->data;
      if (this_hfile->type == HFILE_TYPE_TEXT
       && next_hfile->type == HFILE_TYPE_SEMICOLON)
        {
          /* ok, this is the kind of token we can handle. */
          const char *text;
          text = HFILE_TEXT (this_hfile)->text;
          co_context_init_from_hfile (&context, this_hfile);
          if (is_member_function (text))
            {
              char *return_type;
              char *function_name;
              int   num_parameters;
              COParameter **parameters;
              if (! parse_member_function (text,
                                           struct_name,
                                           &return_type,
                                           &function_name,
                                           &num_parameters,
                                           &parameters))
                {
                  context_warning (&context, "struct %s member function: "
                             "couldn't parse: `%s'", struct_name, text);
                }
              else
                {
                  co_database_add_member_function (database, struct_name,
                                                   function_name, return_type,
                                                   num_parameters,
                                                   parameters,
                                                   NULL, NULL, 0, &context);
                }
            }
          else
            {
              char *type;
              char *name;
              char *type_suffix;
              if (! parse_arg_triple (&text, &type, &name, &type_suffix, -1))
                {
                  context_warning (&context,
                                   "struct %s member: couldn't parse: `%s'",
                                   struct_name, text);
                }
              else
                {
                  /* add the member to the structure if needed, warning
                   * about changes. */
                  co_database_add_struct_member (database, struct_name,
                                                 name, type, NULL, NULL, 0,
						 &context);
                }
            }
          at = at->next->next;
        }
      else
        {
          /* well, we just need to skip until the semicolon, 
           * TODO: a warning is needed here */
          at = at->next->next;
          while (at != NULL && * ((int*) (at->data)) != HFILE_TYPE_SEMICOLON)
            at = at->next;
        }
    }

  /* now return the appropriate list to the caller, since everything
   * checked out. */
  while (start != NULL)
    {
      HFile *hfile = start->data;
      if (hfile->type == HFILE_TYPE_SEMICOLON)
        break;
      start = start->next;
    }

  return start;
}

/*
 * Enumerations:
 *     typedef enum [_EnumName] {
 *        enum_value_1 (= \d+)?,
 *        enum_value_2,
 *        ...
 *     } EnumName;
 */
static gboolean
hfile_is_enumeration (HFile      *hfile)
{
  const char *at;
  if (hfile->type != HFILE_TYPE_TEXT)
    return FALSE;
  at = HFILE_TEXT (hfile)->text;
  SKIP_WHITESPACE (at);
  if (strncmp (at, "typedef", 4) == 0)
    {
      at += 7;
      SKIP_WHITESPACE (at);
    }
  if (strncmp (at, "enum", 4) == 0)
    {
      at += 4;
      if (*at == '\0' || isspace(*at))
        return TRUE;
    }
  return FALSE;
}

static void
append_text_blobs (HFile *hfile, GString *str)
{
  if (hfile->type == HFILE_TYPE_TEXT)
    g_string_append (str, HFILE_TEXT (hfile)->text);
}
static char *
condense_text (GSList  *hfile_list)
{
  GString *rv_string;
  char *rv;
  rv_string = g_string_new ("");
  g_slist_foreach (hfile_list, (GFunc) append_text_blobs, rv_string);
  rv = rv_string->str;
  g_string_free (rv_string, FALSE);
  return rv;
}

static GSList *
hfile_parse_enumeration  (CODatabase *database,
			  GSList     *start)
{
  const char *text;
  char       *enum_name;
  HFileList  *value_list;
  char      **values;
  char      **at;
  COContext context;

  co_context_init_from_hfile (&context, HFILE (start->data));
  text = HFILE_TEXT (start->data)->text;
  is_word_ws(&text, "typedef");
  if (! is_word_ws(&text, "enum"))
    {
      context_warning (&context, "missing word `enum' in definition");
      return NULL;
    }
  g_return_val_if_fail (start->next != NULL, NULL);
  if (*text == '_')
    {
      enum_name = hfile_get_word (text + 1);
    }
  else
    {
      HFileText *next_blob;
      g_return_val_if_fail (start->next->next != NULL, NULL);
      next_blob = HFILE_TEXT (start->next->next->data);
      enum_name = hfile_get_word (next_blob->text);
    }
  g_return_val_if_fail (enum_name != NULL, NULL);
  value_list = HFILE_LIST (start->next->data);
  g_return_val_if_fail (value_list != NULL, NULL);
  g_return_val_if_fail (value_list->has_braces, NULL);
  text = condense_text (value_list->list);
  values = g_strsplit (text, ",", 0);
  for (at = values; *at != NULL; at++)
    {
      char *value_name;
      value_name = hfile_get_word (*at);
      if (value_name == NULL)
        continue;
      co_database_add_enum_value (database, enum_name, value_name, NULL,
                                  NULL, 0, &context);
    }
  g_strfreev (values);

  while (start != NULL)
    {
      HFile *hfile = start->data;
      if (hfile->type == HFILE_TYPE_SEMICOLON)
        break;
      start = start->next;
    }
  g_return_val_if_fail (start != NULL, NULL);
  return start;
}

/*
 * globals:
 *
 *         type name type_suffix;
 */
static gboolean
hfile_is_global        (HFile      *hfile,
			HFile      *next)
{
  const char *text;
  if (hfile->type != HFILE_TYPE_TEXT)
    return FALSE;
  if (next == NULL)
    return FALSE;
  if (next->type != HFILE_TYPE_SEMICOLON)
    return FALSE;
  text = HFILE_TEXT (hfile)->text;
  if (strchr (text, LEFT_PAREN_CHAR) != NULL)
    return FALSE;
  if (! codoc_util_is_c_type (&text))
    return FALSE;
  return TRUE;
}

static GSList *
hfile_parse_global     (CODatabase *database,
			GSList     *start)
{
  const char *text;
  char *type;
  char *name;
  char *type_suffix;
  COContext context;
  co_context_init_from_hfile (&context, HFILE (start->data));
  text = HFILE_TEXT (start->data)->text;

  if (! parse_arg_triple (&text, &type, &name, &type_suffix, -1))
    {
      context_warning (&context, "parse_arg_triple failed for global");
      return NULL;
    }

  co_database_add_global (database, name, type, NULL, NULL, 0, &context);
  g_free (type);
  g_free (name);
  g_free (type_suffix);

  return start->next;
}

static gboolean
hfile_is_union (HFile       *hfile,
		HFile       *next)
{
  const char *at;
  if (hfile->type != HFILE_TYPE_TEXT)
    return FALSE;
  if (next->type != HFILE_TYPE_LIST)
    return FALSE;
  at = HFILE_TEXT (hfile)->text;
  SKIP_WHITESPACE (at);
  if (strncmp (at, "typedef", 7) == 0)
    {
      at += 7;
      SKIP_WHITESPACE (at);
    }
  if (strncmp (at, "union", 5) == 0)
    {
      at += 5;
      if (*at == '\0' || isspace(*at))
        return TRUE;
    }
  return FALSE;
}
static GSList *
hfile_parse_union      (CODatabase *database,
			GSList     *start)
{
  const char *at;
  const char *end;
  char *union_name = NULL;
  GPtrArray *member_array;
  COContext context;
  HFileText *hfile_text = HFILE_TEXT (start->data);
  HFileList *list;
  GSList *union_elements;
  at = hfile_text->text;

  co_context_init_from_hfile (&context, HFILE (start->data));
  g_return_val_if_fail (start->next != NULL, NULL);
  g_return_val_if_fail (start->next->next != NULL, NULL);

  SKIP_WHITESPACE (at);
  if (strncmp (at, "typedef", 7) == 0)
    {
      at += 7;
      SKIP_WHITESPACE (at);
    }
  if (strncmp (at, "union", 5) != 0)
    return FALSE;
  at += 5;
  SKIP_WHITESPACE (at);

  /* if we find _, then assume we're at: _UnionName */
  if (*at == '_')
    {
      at++;
      end = at;
      SKIP_CHAR_TYPE (end, is_word_char);
      union_name = copy_between (at, end);
    }

  member_array = g_ptr_array_new ();
  list = HFILE_LIST (start->next->data);
  for (union_elements = list->list;
       union_elements != NULL;
       union_elements = g_slist_next (union_elements))
    {
      HFile *cur = union_elements->data;
      HFile *next;
      if (union_elements->next == NULL)
        break;
      next  = union_elements->next->data;
      if (cur->type == HFILE_TYPE_TEXT && next->type == HFILE_TYPE_SEMICOLON)
        {
          const char *text = cur->text.text;
          char *type;
          char *name;
          char *type_suffix;

          if (! parse_arg_triple (&text, &type, &name, &type_suffix, -1) )
            {
              context_warning (&context,
                               "hmmm.. error parsing type/name from %s", text);
            }
          else
            {
              g_ptr_array_add (member_array, type);
              g_ptr_array_add (member_array, name);
              g_free (type_suffix);
            }
        }
      while (union_elements != NULL
        &&   * (int*) (union_elements->data) != HFILE_TYPE_SEMICOLON)
        union_elements = union_elements->next;
    }
  if ( * (int*) start->next->next->data == HFILE_TYPE_TEXT)
    {
      HFile *hfile = start->next->next->data;
      const char *label = HFILE_TEXT (hfile)->text;
      char *u_name_2;
      const char *end_label;
      SKIP_WHITESPACE (label);
      end_label = label;
      SKIP_CHAR_TYPE (end_label, is_word_char);
      if (end_label > label)
        {
          u_name_2 = copy_between (label, end_label);
          if (union_name == NULL)
            union_name = u_name_2;
          else
            {
              if (strcmp (u_name_2, union_name) != 0)
                {
                  co_context_init_from_hfile (&context, hfile);
                  context_warning (&context,
                                   "union name/typedef mismatch - using %s",
                                   u_name_2);
                }
              g_free (union_name);
              union_name = u_name_2;
            }
        }
    }

  if (union_name != NULL)
    {
      char **type_name_pairs;
      int num_pairs;
      type_name_pairs = (char**) member_array->pdata;
      num_pairs = member_array->len / 2;
      g_ptr_array_free (member_array, FALSE);
      co_database_add_union (database, union_name,
                             num_pairs, type_name_pairs,
                             NULL, NULL, 0, &context);
      g_free (union_name);
    }
  else
    {
      context_message (&context, "cannot document anonymous union");
    }

  while (start != NULL
   &&    * (int*) (start->data) != HFILE_TYPE_SEMICOLON)
    start = start->next;
  return start;
}

/*
 * TODO:
 *   nested structures, anon structures
 *   #define's   [but which?]
 *   typedef void (*FunctionPointerTypedefs)();
 *   "classes" -- that is, the Class structure of g[st]k say.
 */
static GSList  *
co_database_merge_element (CODatabase       *database,
			   GSList           *list)
{
  HFile *hfile = list->data;
  HFile *next = list->next != NULL ? list->next->data : NULL;
  if (hfile_is_function (hfile, next, TRUE))
    return hfile_parse_function (database, hfile, TRUE) ? list : NULL;;
  if (hfile_is_function (hfile, next, FALSE))
    return hfile_parse_function (database, hfile, FALSE) ? list : NULL;;
  if (hfile_is_structure (hfile))
    return hfile_parse_structure (database, list);
  if (hfile_is_struct_stub (hfile, next))
    return hfile_parse_struct_stub (database, list);
  if (hfile_is_enumeration (hfile))
    return hfile_parse_enumeration (database, list);
  if (hfile_is_global (hfile, next))
    return hfile_parse_global (database, list);
  if (hfile_is_union (hfile, next))
    return hfile_parse_union (database, list);

  /* hmm.. */
  return list;
}

static gboolean
co_database_merge_one  (CODatabase       *database,
			const char       *h_filename,
			const char       *cpp_flags)
{
  HFileList *hfile_list;
  GSList *tmp;
  gboolean rv;
  COContext context;
  hfile_list = hfile_parse (h_filename, cpp_flags);
  context.file = h_filename;
  context.lineno = 0;
  if (hfile_list == NULL)
    {
      context_warning (&context, "error parsing %s", h_filename);
      return FALSE;
    }
  co_database_new_section (database, h_filename, NULL, NULL, NULL, 0, &context);

  rv = TRUE;
  tmp = hfile_list->list;
  while (tmp != NULL)
    {
      co_context_init_from_hfile (&context, HFILE (tmp->data));
      tmp = co_database_merge_element (database, tmp);
      if (tmp == NULL)
        {
          /* TODO: error contexts */
          context_warning (&context, "error merging subelement...");
        }
      else
        tmp = tmp->next;
    }
  hfile_destroy ((HFile*) hfile_list);
  return rv;
}

gboolean
co_database_merge    (CODatabase       *database,
		      const char       *cpp_flags)
{
  DIR *dir;
  struct dirent *entry;
  GSList *list;
  gboolean rv = TRUE;

  if (cpp_flags == NULL)
    {
      cpp_flags = database->cpp_flags;
      if (cpp_flags == NULL)
        cpp_flags = "";
    }

  for (list = database->subdirs; list != NULL; list = list->next)
    if (strcmp ((char*) list->data, ".") == 0)
      break;
  if (list == NULL)
    database->subdirs = g_slist_append (database->subdirs, g_strdup ("."));
     
  for (list = database->subdirs; list != NULL; list = list->next)
    {
      const char *dirname = list->data;
      gboolean is_curdir = strcmp (dirname, ".") == 0;
      dir = opendir (dirname);
      if (dir == NULL)
        {
          g_warning ("error scanning %s", dirname);
          rv = FALSE;
          continue;
        }

      while ((entry = readdir(dir)) != NULL)
        {
          const char *name = entry->d_name;
          const char *ext = strrchr (name, '.');
          char *tmp = NULL;
          if (ext == NULL)
            continue;
          if (! is_curdir)
            {
              tmp = g_strjoin ("/", dirname, name, NULL);
              name = tmp;
            }
          if (strcmp (ext, ".h") == 0)
            if (g_hash_table_lookup (database->omissions, name) == NULL)
              if (! co_database_merge_one (database, name, cpp_flags))
                rv = FALSE;
          g_free (tmp);
        }
      closedir (dir);
    }

  return rv;
}

/*
 *  _ _ _                    _   _
 * | (_) |__   ___ _ __ __ _| |_(_) ___  _ __
 * | | | '_ \ / _ \ '__/ _` | __| |/ _ \| '_ \
 * | | | |_) |  __/ | | (_| | |_| | (_) | | | |
 * |_|_|_.__/ \___|_|  \__,_|\__|_|\___/|_| |_|
 * 
 */

static void
co_parameter_destroy(COParameter *parameter)
{
  g_free (parameter->type);
  g_free (parameter->name);
  FREE_BLURBS (parameter);
  g_free (parameter);
}

static void
free_parameter_array (COParameter   **parameters,
		      int             num_parameters)
{
  int i;
  for (i = 0; i < num_parameters; i++)
    if (parameters[i] != NULL)
      co_parameter_destroy (parameters[i]);
  g_free (parameters);
}

static void
free_function_hash_style(gpointer unused,
			 COFunction *function)
{
  (void) unused;
  g_free (function->name);
  FREE_BLURBS (function);
  free_parameter_array (function->parameters, function->num_parameters);
  g_free (function);
}

static void
free_one_param (gpointer unused,
		COParameter *param)
{
  (void) unused;
  g_free (param->type);
  g_free (param->name);
  FREE_BLURBS (param);
  g_free (param);
}
static void
free_structure_hash_style(gpointer unused, 
			  COStructure *structure)
{
  (void) unused;
  g_free (structure->name);
  FREE_BLURBS (structure);
  g_hash_table_foreach (structure->members, (GHFunc) free_one_param, NULL);
  g_hash_table_destroy (structure->members);
  g_free (structure->member_names);
  g_free (structure);
}
static void
free_one_enum_value(gpointer unused,
		    COEnumerationValue *value)
{
  (void) unused;
  g_free (value->name);
  FREE_BLURBS (value);
  g_free (value);
}
static void
free_enumeration_hash_style(gpointer unused,
			    COEnumeration *enumer)
{
  (void) unused;
  g_free (enumer->name);
  FREE_BLURBS (enumer);
  g_hash_table_foreach (enumer->values, (GHFunc) free_one_enum_value, NULL);
  g_hash_table_destroy (enumer->values);
  g_free (enumer);
}
static void
free_union_hash_style (gpointer unused,
		       COUnion *un)
{
  int i;
  (void) unused;
  for (i = 0; i < un->num_objects; i++)
    {
      g_free (un->type_name_pairs [2*i+0]);
      g_free (un->type_name_pairs [2*i+1]);
    }
  FREE_BLURBS (un);
  g_free (un->type_name_pairs);
  g_free (un);
}
static void
free_global_hash_style (gpointer unused,
			COGlobal *global)
{
  (void) unused;
  g_free (global->name);
  g_free (global->type);
  g_free (global);
}

void
co_database_destroy  (CODatabase       *database)
{
  g_hash_table_foreach (database->functions,
                        (GHFunc) free_function_hash_style,
                        NULL);
  g_hash_table_foreach (database->structures,
                        (GHFunc) free_structure_hash_style,
                        NULL);
  g_hash_table_foreach (database->enumerations,
                        (GHFunc) free_enumeration_hash_style,
                        NULL);
  g_hash_table_foreach (database->unions,
                        (GHFunc) free_union_hash_style,
                        NULL);
  g_hash_table_foreach (database->globals,
                        (GHFunc) free_global_hash_style,
                        NULL);
  g_hash_table_destroy (database->functions);
  g_hash_table_destroy (database->structures);
  g_hash_table_destroy (database->enumerations);
  g_hash_table_destroy (database->unions);
  g_hash_table_destroy (database->globals);
  g_free (database->output_doc_filename);
  g_free (database);
}
