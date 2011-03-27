#include <stdlib.h>
#include <errno.h>
#include "libcodoc.h"
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define XML_NODE_GET_CHILDREN(node)		((node)->children)

#define CONTEXT	NULL

void
codocXmlAddChildList (xmlNode *node, xmlNode *list)
{
  while (list != NULL)
    {
      xmlAddChild (node, xmlCopyNode (list, TRUE));
      list = list->next;
    }
}

xmlNode *
codocXmlCopyNodeList (xmlNode *list)
{
  xmlNode *rv = NULL;
  if (list != NULL)
    {
      rv = xmlCopyNode (list, TRUE);
      list = list->next;
      while (list != NULL)
        {
	  xmlAddSibling (rv, xmlCopyNode (list, TRUE));
	  list = list->next;
	}
    }
  return rv;
}

/* === Loading from an XML template === */
static char *
get_named_string (xmlNode *node, const char *key)
{
  xmlNode *at;
  char *txt;
  gchar *rv;
  xmlNode *got = NULL;
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, key) == 0)
      {
	if (got != NULL)
	  {
	    g_warning ("got two <%s> keys: aborting", key);
	    return NULL;
	  }
	got = at;
      }
  if (got == NULL)
    return NULL;
  txt = xmlNodeGetContent (got);
  rv = g_strdup (txt);
  xmlFree (txt);
  return rv;
}

static xmlNode *
get_named_node_list (xmlNode *node, const char *key)
{
  xmlNode *at;
  xmlNode *got = NULL;
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, key) == 0)
      {
	if (got != NULL)
	  {
	    g_warning ("got two <%s> keys: aborting", key);
	    return NULL;
	  }
	got = at;
      }
  if (got == NULL)
    return NULL;
  return codocXmlCopyNodeList (got->children);
}
static COEntryFlags
get_entry_flags (xmlNode *node)
{
  COEntryFlags rv = 0;
  xmlNode *at;
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, "private") == 0)
      rv |= CO_ENTRY_FLAGS_PRIVATE;
  return rv;
}

static gboolean
process_enumeration_node (CODatabase *database,
			  xmlNode    *node)
{
  xmlNode *at;
  char *name = get_named_string (node, "name");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  COEntryFlags flags = get_entry_flags (node);
  if (name == NULL)
    return FALSE;
  co_database_add_enum (database, name, blurb, conflict, flags, CONTEXT);
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, "value") == 0)
      {
	char *val_name = get_named_string (at, "name");
	xmlNode *val_blurb = get_named_node_list (at, "blurb");
	xmlNode *val_conflict = get_named_node_list (at, "conflict");
	COEntryFlags val_flags = get_entry_flags (at);
	co_database_add_enum_value (database, name, val_name,
				    val_blurb, val_conflict, val_flags,
				    CONTEXT);
	g_free (val_name);
      }
  return TRUE;
}

static gboolean
process_function_node (CODatabase *database,
		       xmlNode    *node,
		       gboolean    is_typedef)
{
  xmlNode *at;
  char *name = get_named_string (node, "name");
  char *type = get_named_string (node, is_typedef ? "function-typedef" : "function");
  int param_index = 0;
  xmlNode *para;
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  COEntryFlags flags = get_entry_flags (node);
  co_database_add_function (database, is_typedef, name, type, blurb, conflict,
			    flags, CONTEXT);
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, "parameter") == 0)
      {
	char *param_name = get_named_string (at, "name");
	char *param_type = get_named_string (at, "type");
	char *param_index_str = get_named_string (at, "index");
	xmlNode *param_blurb = get_named_node_list (at, "blurb");
	xmlNode *param_conflict = get_named_node_list (at, "conflict");
	COEntryFlags param_flags = get_entry_flags (at);
	if (param_index_str != NULL)
	  param_index = atoi (param_index_str);
	co_database_add_function_param (database, is_typedef, name,
					param_index, param_name, param_type,
					param_blurb, param_conflict,
					param_flags, CONTEXT);
	g_free (param_name);
	g_free (param_type);
	g_free (param_index_str);
	param_index++;
      }
  g_free (name);
  g_free (type);
  return TRUE;
}

static gboolean
process_real_function_node    (CODatabase *database,
			       xmlNode    *node)
{
  return process_function_node (database, node, FALSE);
}

static gboolean
process_function_typedef_node (CODatabase *database,
			       xmlNode    *node)
{
  return process_function_node (database, node, TRUE);
}

static gboolean
parse_method_node (CODatabase *database,
		   const char *struct_name,
		   xmlNode    *node)
{
  char *name = get_named_string (node, "name");
  char *return_type = get_named_string (node, "return-type");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  xmlNode *at;
  COEntryFlags flags = get_entry_flags (node);
  int param_index = 0;
  if (name == NULL)
    return FALSE;
  co_database_add_member_function (database, struct_name, name, return_type,
				   -1, NULL, blurb, conflict, flags, CONTEXT);
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, "parameter") == 0)
      {
	char *param_name = get_named_string (at, "name");
	char *param_type = get_named_string (at, "type");
	char *param_index_str = get_named_string (at, "index");
	xmlNode *param_blurb = get_named_node_list (at, "blurb");
	xmlNode *param_conflict = get_named_node_list (at, "conflict");
	COEntryFlags param_flags = get_entry_flags (at);
	if (param_index_str != NULL)
	  param_index = atoi (param_index_str);
	co_database_add_struct_member_param (database, struct_name,
					     name, param_index, param_name,
					     param_type, param_blurb,
					     param_conflict, param_flags,
					     CONTEXT);
	g_free (param_name);
	g_free (param_type);
	g_free (param_index_str);
	param_index++;
      }
  g_free (name);
  g_free (return_type);
  return TRUE;
}

static gboolean
parse_member_node (CODatabase *database,
		   const char *struct_name,
		   xmlNode    *node)
{
  char *name = get_named_string (node, "name");
  char *type = get_named_string (node, "type");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  xmlNode *at;
  COEntryFlags flags = get_entry_flags (node);
  int param_index = 0;
  if (name == NULL)
    {
      g_warning ("no name in member for %s", struct_name);
      return FALSE;
    }
  if (type == NULL)
    {
      g_warning ("%s::%s has no type?", struct_name, name);
      return FALSE;
    }
  co_database_add_struct_member (database, struct_name, name, type,
			         blurb, conflict, flags, CONTEXT);
  g_free (name);
  g_free (type);
  return TRUE;
}

static gboolean
process_structure_node   (CODatabase *database,
			  xmlNode    *node)
{
  char *name = get_named_string (node, "name");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  xmlNode *at;
  COEntryFlags flags = get_entry_flags (node);
  gboolean rv = TRUE;
  if (name == NULL)
    {
      g_warning ("no name field for structure");
      return FALSE;
    }
  co_database_add_struct (database, name, blurb, conflict, flags, CONTEXT);
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, "method") == 0)
      {
	if (!parse_method_node (database, name, at))
	  {
	    g_warning ("error parsing method for %s", name);
	    rv = FALSE;
	  }
      }
    else if (strcmp (at->name, "member") == 0)
      {
	if (!parse_member_node (database, name, at))
	  {
	    g_warning ("error parsing member for %s", name);
	    rv = FALSE;
	  }
      }
  g_free (name);
  return rv;
}

static gboolean
process_global_node      (CODatabase *database,
			  xmlNode    *node)
{
  char *name = get_named_string (node, "name");
  char *type = get_named_string (node, "type");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  COEntryFlags flags = get_entry_flags (node);
  if (name == NULL || type == NULL)
    return FALSE;
  co_database_add_global (database, name, type, blurb, conflict, flags,
			  CONTEXT);
  g_free (name);
  g_free (type);
  return TRUE;
}

static gboolean
process_union_node       (CODatabase *database,
			  xmlNode    *node)
{
  GPtrArray *pairs = g_ptr_array_new ();
  char *name = get_named_string (node, "name");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  xmlNode *at;
  gboolean rv = TRUE;
  COEntryFlags flags = get_entry_flags (node);
  if (name == NULL)
    return FALSE;
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    if (strcmp (at->name, "parameter") == 0)
      {
	char *param_type = get_named_string (at, "type");
	char *param_name = get_named_string (at, "name");
	if (param_type == NULL || param_name == NULL)
	  {
	    rv = FALSE;
	    continue;
          }
	g_ptr_array_add (pairs, param_type);
	g_ptr_array_add (pairs, param_name);
      }
  co_database_add_union (database, name, pairs->len / 2, (char **) pairs->pdata,
			 blurb, conflict, flags, CONTEXT);
  g_free (name);
  g_ptr_array_free (pairs, FALSE);
  return rv;
}

static gboolean
process_struct_stub_node (CODatabase *database,
			  xmlNode    *node)
{
  char *name = get_named_string (node, "name");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  COEntryFlags flags = get_entry_flags (node);
  if (name == NULL)
    return FALSE;
  co_database_add_struct_stub (database, name, blurb, conflict, flags, CONTEXT);
  g_free (name);
  return TRUE;
}

struct {
  const char *name;
  gboolean (*handle_node)(CODatabase *database,
			  xmlNode    *node);
} node_types[] = {
  { "enumeration", process_enumeration_node },
  { "function", process_real_function_node },
  { "function-typedef", process_function_typedef_node },
  { "structure", process_structure_node },
  { "global", process_global_node },
  { "union", process_union_node },
  { "struct-stub", process_struct_stub_node },

  /* other non-handled bits */
  { "title", NULL },
  { "h-filename", NULL },
  { "blurb", NULL },
  { "conflict", NULL },
  { NULL, NULL }
};
static gboolean
process_section_node (CODatabase *database,
		      xmlNode    *node)
{
  xmlNode *at;
  char *title = get_named_string (node, "title");
  char *h_filename = get_named_string (node, "h-filename");
  xmlNode *blurb = get_named_node_list (node, "blurb");
  xmlNode *conflict = get_named_node_list (node, "conflict");
  COEntryFlags flags = get_entry_flags (node);
  gboolean rv = TRUE;
  if (h_filename == NULL)
    {
      g_warning ("no h-filename in section?");
      return FALSE;
    }
  co_database_new_section (database, h_filename, title,
			   blurb, conflict, flags, CONTEXT);
  for (at = XML_NODE_GET_CHILDREN (node); at != NULL; at = at->next)
    {
      guint i;
      for (i = 0; node_types[i].name != NULL; i++)
	if (strcmp (node_types[i].name, at->name) == 0)
	  {
	    if (node_types[i].handle_node == NULL)
	      break;
	    else if (!(*node_types[i].handle_node) (database, at))
	      {
		g_warning ("handler failed for node of type %s", at->name);
		rv = FALSE;
	      }
	    else
	      break;
	  }
      if (node_types[i].name == NULL)
	g_warning ("no handler for node of type %s", at->name);
    }
  return rv;
}

CODatabase *
co_database_load_xml (const char *filename)
{
  xmlDoc *doc = xmlParseFile (filename);
  CODatabase *database;
  xmlNode *root;
  xmlNode *child;
  if (doc == NULL)
    {
      g_warning ("couldn't load xml from %s", filename);
      return NULL;
    }
  root = xmlDocGetRootElement (doc);
  if (strcmp (root->name, "template") != 0)
    {
      g_warning ("root element of %s was not <template>", filename);
      return NULL;
    }
  database = co_database_new ();
  for (child = XML_NODE_GET_CHILDREN (root); child != NULL; child = child->next)
    {
      if (strcmp (child->name, "section") == 0)
	{
	  if (!process_section_node (database, child))
	    {
	      g_warning ("error processing section in %s", filename);
	    }
	}
      else if (strcmp (child->name, "omission") == 0)
	{
	  char *o = xmlNodeGetContent (child);
	  co_database_add_omission (database, o);
	  xmlFree (o);
	}
      else if (strcmp (child->name, "subdirectory") == 0)
	{
	  char *s = xmlNodeGetContent (child);
	  co_database_add_subdir (database, s);
	  xmlFree (s);
	}
      else
	{
	  g_warning ("encountered <%s> where <section> required", child->name);
	  co_database_destroy (database);
	  return NULL;
	}
    }
  xmlFreeDoc (doc);
  return database;
}
/* === Saving to an XML template === */

static void
maybe_add_blurb_and_conflict_nodes (xmlNode    *parent,
				    xmlNode    *blurb,
				    xmlNode    *conflict)
{
  if (blurb != NULL)
    {
      xmlNode *b = xmlNewNode (NULL, "blurb");
      codocXmlAddChildList (b, blurb);
      xmlAddChild (parent, b);
    }
  if (conflict != NULL)
    {
      xmlNode *conflict = xmlNewNode (NULL, "conflict");
      codocXmlAddChildList (conflict, conflict);
      xmlAddChild (parent, conflict);
    }
}

static void
maybe_add_flag_nodes (xmlNode *node,
		      COEntryFlags flags)
{
  if (flags & CO_ENTRY_FLAGS_PRIVATE)
    xmlAddChild (node, xmlNewNode (NULL, "private"));
}

#define MAYBE_ADD_TYPICAL_XML_NODES(node, thing)		\
      G_STMT_START{						\
	maybe_add_blurb_and_conflict_nodes (node,		\
					    (thing)->blurb,	\
					    (thing)->conflict);	\
	maybe_add_flag_nodes (node, (thing)->flags);		\
      }G_STMT_END

static void
add_enum_value_to_xml (const char         *name,
		       COEnumerationValue *value,
		       xmlNode            *node)
{
  xmlNode *add = xmlNewNode (NULL, "value");
  xmlNewChild (add, NULL, "name", name);
  MAYBE_ADD_TYPICAL_XML_NODES (add, value);
  xmlAddChild (node, add);
}

static xmlNode *
co_enumeration_to_template_xml (COEnumeration *enumeration)
{
  xmlNode *rv = xmlNewNode (NULL, "enumeration");
  xmlNewChild (rv, NULL, "name", enumeration->name);
  MAYBE_ADD_TYPICAL_XML_NODES (rv, enumeration);
  g_hash_table_foreach (enumeration->values, (GHFunc) add_enum_value_to_xml, rv);
  return rv;
}

static xmlNode *
co_parameter_to_template_xml (COParameter *parameter)
{
  xmlNode *rv = xmlNewNode (NULL, "parameter");
  xmlNewChild (rv, NULL, "type", parameter->type);
  if (parameter->name != NULL)
    xmlNewChild (rv, NULL, "name", parameter->name);
  MAYBE_ADD_TYPICAL_XML_NODES (rv, parameter);
  return rv;
}


static xmlNode *
co_function_to_template_xml (COFunction *function, gboolean is_typedef)
{
  int i;
  xmlNode *rv = xmlNewNode (NULL, is_typedef ? "function-typedef" : "function");
  xmlNewChild (rv, NULL, "name", function->name);
  xmlNewChild (rv, NULL, "return-type", function->return_type);
  MAYBE_ADD_TYPICAL_XML_NODES (rv, function);
  for (i = 0; i < function->num_parameters; i++)
    xmlAddChild (rv, co_parameter_to_template_xml (function->parameters[i]));
  return rv;
}

static xmlNode *
co_structure_to_template_xml (COStructure *structure)
{
  xmlNode *rv = xmlNewNode (NULL, "structure");
  guint num_members = g_hash_table_size (structure->members);
  guint i;
  xmlNewChild (rv, NULL, "name", structure->name);
  MAYBE_ADD_TYPICAL_XML_NODES (rv, structure);
  for (i = 0; i < num_members; i++)
    {
      const char *name = structure->member_names[i];
      COMember *member = g_hash_table_lookup (structure->members, name);
      xmlNode *member_node;
      g_assert (member != NULL);
      if (member->is_function)
	member_node = xmlNewNode (NULL, "method");
      else
	member_node = xmlNewNode (NULL, "member");
      xmlNewChild (member_node, NULL, "name", name);
      MAYBE_ADD_TYPICAL_XML_NODES (member_node, member);
      if (member->is_function)
	{
	  gint p;
	  xmlNewChild (member_node, NULL, "return-type", member->type);
	  for (p = 0; p < member->num_parameters; p++)
	    {
	      COParameter *param = member->parameters[p];
	      xmlAddChild (member_node, co_parameter_to_template_xml (param));
	    }
	}
      else
	{
	  xmlNewChild (member_node, NULL, "type", member->type);
	}
      xmlAddChild (rv, member_node);
    }
  return rv;
}

static xmlNode *
co_global_to_template_xml (COGlobal *global)
{
  xmlNode *rv = xmlNewNode (NULL, "global");
  xmlNewChild (rv, NULL, "type", global->type);
  if (global->name != NULL)
    xmlNewChild (rv, NULL, "name", global->name);
  MAYBE_ADD_TYPICAL_XML_NODES (rv, global);
  return rv;
}

static xmlNode *
co_union_to_template_xml (COUnion *the_union)
{
  gint i;
  xmlNode *rv = xmlNewNode (NULL, "union");
  xmlNewChild (rv, NULL, "name", the_union->name);
  MAYBE_ADD_TYPICAL_XML_NODES (rv, the_union);
  for (i = 0; i < the_union->num_objects; i++)
    {
      xmlNode *entry = xmlNewNode (NULL, "parameter");
      xmlAddChild (rv, entry);
      xmlNewChild (entry, NULL, "type", the_union->type_name_pairs[2*i+0]);
      xmlNewChild (entry, NULL, "name", the_union->type_name_pairs[2*i+1]);
    }
  return rv;
}

static xmlNode *
co_struct_stub_to_template_xml (COStructStub *struct_stub)
{
  xmlNode *rv = xmlNewNode (NULL, "struct-stub");
  xmlNewChild (rv, NULL, "name", struct_stub->name);
  MAYBE_ADD_TYPICAL_XML_NODES (rv, struct_stub);
  return rv;
}

static xmlNode *
co_section_to_template_xml (COSection *section)
{
  xmlNode *rv = xmlNewNode (NULL, "section");
  GSList *at;
  xmlNewChild (rv, NULL, "h-filename", section->h_filename);
  if (section->title != NULL)
    xmlNewChild (rv, NULL, "title", section->title);
  else
    xmlNewChild (rv, NULL, "title", section->h_filename);
  maybe_add_blurb_and_conflict_nodes (rv, section->blurb, section->conflict);
  for (at = section->first_entry; at != NULL; at = at->next)
    {
      COEntry *entry = at->data;
      xmlNode *tmp = NULL;
      if (entry->enumeration != NULL)
	tmp = co_enumeration_to_template_xml (entry->enumeration);
      else if (entry->function != NULL)
	tmp = co_function_to_template_xml (entry->function, FALSE);
      else if (entry->function_typedef != NULL)
	tmp = co_function_to_template_xml (entry->function_typedef, TRUE);
      else if (entry->structure != NULL)
	tmp = co_structure_to_template_xml (entry->structure);
      else if (entry->global != NULL)
	tmp = co_global_to_template_xml (entry->global);
      else if (entry->the_union != NULL)
	tmp = co_union_to_template_xml (entry->the_union);
      else if (entry->struct_stub != NULL)
	tmp = co_struct_stub_to_template_xml (entry->struct_stub);
      if (tmp != NULL)
	xmlAddChild (rv, tmp);
    }
  return rv;
}

static void
add_omission (const char *omit, gpointer one, xmlNode *root)
{
  xmlNewChild (root, NULL, "omission", omit);
}

static void
add_subdir (const char *subdir, xmlNode *root)
{
  xmlNewChild (root, NULL, "subdirectory", subdir);
}

static xmlDoc *
co_database_to_template_doc (CODatabase *database)
{
#define XML_VERSION "1.0"
  xmlDoc *doc = xmlNewDoc (XML_VERSION);
  xmlNode *root = xmlNewNode (NULL, "template");
  GSList *at;
  xmlDocSetRootElement (doc, root);
  g_hash_table_foreach (database->omissions, (GHFunc) add_omission, root);
  g_slist_foreach (database->subdirs, (GFunc) add_subdir, root);
  for (at = database->first_section; at != NULL; at = at->next)
    {
      xmlNode *x = co_section_to_template_xml (at->data);
      g_return_val_if_fail (x != NULL, NULL);
      xmlAddChild (root, x);
    }
  return doc;
}

gboolean
co_database_safe_save (CODatabase       *database,
		       const char       *filename)
{
  char *tmp_filename;
  char *user;
  char *backup_filename;
  xmlDoc *doc;
  user = getenv ("USER");
  if (user == NULL)
    user = "unknown";
  tmp_filename = g_strdup_printf ("%s.tmp-%d-%s", filename, getpid (), user);
  doc = co_database_to_template_doc (database);
  if (doc == NULL || !xmlSaveFile (tmp_filename, doc))
    {
      g_warning ("saving %s failed", filename);
      unlink (tmp_filename);
      if (doc != NULL)
        xmlFreeDoc (doc);
      return FALSE;
    }
  xmlFreeDoc (doc);
  backup_filename = g_strdup_printf ("%s.backup", filename);
  unlink (backup_filename);
  if (rename (filename, backup_filename) < 0 && errno != ENOENT)
    {
      g_warning ("couldn't move %s to %s: %s", filename, backup_filename,
       	    g_strerror (errno));
      return FALSE;
    }
  if (rename (tmp_filename, filename) < 0)
    {
      g_warning ("couldn't move %s to %s: %s", tmp_filename, filename,
       	    g_strerror (errno));
      g_warning ("restoring backup: %s => %s", backup_filename, filename);
      if (rename (backup_filename, filename) < 0)
	 g_warning ("error restoring backup %s", filename);
      return FALSE;
    }
  return TRUE;
}
