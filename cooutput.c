#include "libcodoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define LEFT_PAREN_CHAR      '('
#define RIGHT_PAREN_CHAR     ')'

static void
compute_privacy_string(char *str_out, int flags)
{
  if (flags & CO_ENTRY_FLAGS_PRIVATE)
    strcpy (str_out, " <private>");
  else
    *str_out = '\0';
}

typedef struct _CIEnumerationIter COEnumerationIter;
struct _CIEnumerationIter {
  COEnumeration *enumeration;
  gboolean       had_error;
};

/* from libcodoc-xml.c (hack) */
void codocXmlAddChildList (xmlNode *node, xmlNode *list);

static xmlNode *
protect_blurb (xmlNode *node)
{
  xmlNode *rv = xmlNewNode (NULL, "para");
  if (node != NULL)
    codocXmlAddChildList (rv, node);
  return rv;
}

static gboolean
needs_space_after (const char *s)
{
  if (s == NULL)
    return FALSE;
  if (*s == 0)
    return FALSE;
  s = strchr (s, 0) - 1;
  return (*s != '*');
}

/* Render xml. */
static void enum_to_var_list_entry (gpointer       key,
                                    gpointer       value,
				    gpointer       user_data)
{
  xmlNode *parent = user_data;
  COEnumerationValue *enum_value = value;
  xmlNode *varlistentry = xmlNewNode (NULL, "varlistentry");
  xmlNode *listitem = xmlNewNode (NULL, "listitem");
  xmlNewChild (varlistentry, NULL, "term", enum_value->name);
  xmlAddChild (varlistentry, listitem);

  xmlAddChild (listitem, protect_blurb (enum_value->blurb));
  xmlAddChild (parent, varlistentry);
}

static void
addTitle (xmlNode *node, const char *type, const char *name)
{
  char *tmp = g_strdup_printf ("%s %s", type, name);
  xmlNewChild (node, NULL, "title", tmp);
  g_free (tmp);
}

static xmlNode *
enumeration_to_docbook_xml (COEnumeration  *enumeration)
{
  xmlNode *rv = xmlNewNode (NULL, "sect1");
  xmlSetProp (rv, "id", enumeration->name);
  addTitle (rv, "enum", enumeration->name);
  xmlAddChild (rv, protect_blurb (enumeration->blurb));
  if (g_hash_table_size (enumeration->values) > 0)
    {
      xmlNode *para = xmlNewNode (NULL, "para");
      xmlNode *list = xmlNewNode (NULL, "variablelist");
      g_hash_table_foreach (enumeration->values, enum_to_var_list_entry, list);
      xmlAddChild (para, xmlNewText ("Allowed values:\n"));
      xmlAddChild (para, list);
      xmlAddChild (rv, para);
    }
  return rv;
}
static xmlNode *
function_to_docbook_xml       (COFunction     *function,
			       gboolean       is_typedef)
{
  int i;
  xmlNode *funcsynopsis, *funcprototype, *funcdef, *variablelist;

  xmlNode *rv = xmlNewNode (NULL, "sect1");
  xmlSetProp (rv, "id", function->name);
  addTitle (rv, is_typedef ? "typedef" : "function", function->name);

  funcsynopsis = xmlNewNode (NULL, "funcsynopsis");
  funcprototype = xmlNewNode (NULL, "funcprototype");
  xmlAddChild (funcsynopsis, funcprototype);
  funcdef = xmlNewNode (NULL, "funcdef");
  xmlAddChild (funcprototype, funcdef);
  if (is_typedef)
    xmlAddChild (funcdef, xmlNewText ("typedef "));
  xmlAddChild (funcdef, xmlNewText (function->return_type));
  if (needs_space_after (function->return_type))
    xmlAddChild (funcdef, xmlNewText (" "));		/* inefficient? */
  if (is_typedef)
    {
      char *tmp = g_strdup_printf ("(*%s)", function->name);
      xmlNewChild (funcdef, NULL, "function", tmp);
      g_free (tmp);
    }
  else
    {
      xmlNewChild (funcdef, NULL, "function", function->name);
    }
  for (i = 0; i < function->num_parameters; i++)
    {
      COParameter *param = function->parameters[i];
      xmlNode *paramdef = xmlNewNode (NULL, "paramdef");
      xmlNode *text = xmlNewText (param->type);
      xmlTextConcat (text, " ", 1);
      xmlAddChild (paramdef, text);
      xmlNewChild (paramdef, NULL, "parameter", param->name);
      xmlAddChild (funcprototype, paramdef);
    }
  if (i == 0)
    xmlAddChild (funcprototype, xmlNewNode (NULL, "void"));

  {
    xmlNode * para = xmlNewNode (NULL, "para");
    xmlAddChild (para, xmlNewText ("Synopsis:"));
    xmlAddChild (para, funcsynopsis);
    xmlAddChild (rv, para);
  }

  xmlAddChild (rv, protect_blurb (function->blurb));
  xmlNewChild (rv, NULL, "para", "Parameters:");

  if (function->num_parameters > 0)
    {
      variablelist = xmlNewNode (NULL, "variablelist");
      xmlAddChild (rv, variablelist);

      for (i = 0; i < function->num_parameters; i++)
	{
	  COParameter *param = function->parameters[i];
	  const char *blurb;
	  xmlNode *varlistentry, *listitem;
	  if (param == NULL)
	    continue;
	  varlistentry = xmlNewNode (NULL, "varlistentry");
	  xmlNewChild (varlistentry, NULL, "term", param->name);
	  listitem = xmlNewNode (NULL, "listitem");
	  xmlAddChild (listitem, protect_blurb (param->blurb));
	  xmlAddChild (varlistentry, listitem);
	  xmlAddChild (variablelist, varlistentry);
	}
    }
  else
    {
      xmlNewChild (rv, NULL, "para", "None.");
    }
  return rv;
}

static void
method_to_docbook_xml_list (COMember   *member,
                            xmlNode    *parent)
{
  int i;
  xmlNode *funcsynopsis, *funcprototype, *funcdef, *variablelist, *para;
  g_assert (member->is_function);
  
  para = xmlNewNode (NULL, "para");
  funcsynopsis = xmlNewNode (NULL, "funcsynopsis");
  xmlAddChild (para, funcsynopsis);
  funcprototype = xmlNewNode (NULL, "funcprototype");
  xmlAddChild (funcsynopsis, funcprototype);
  funcdef = xmlNewNode (NULL, "funcdef");
  xmlAddChild (funcprototype, funcdef);
  xmlAddChild (funcdef, xmlNewText (member->type));

  {
    char *tmp = g_strdup_printf ("(*%s)", member->name);
    xmlNewChild (funcdef, NULL, "function", tmp);
    g_free (tmp);
  }
  for (i = 0; i < member->num_parameters; i++)
    {
      COParameter *param = member->parameters[i];
      xmlNode *paramdef = xmlNewNode (NULL, "paramdef");
      xmlNode *text = xmlNewText (param->type);
      xmlTextConcat (text, " ", 1);
      xmlAddChild (paramdef, text);
      xmlNewChild (paramdef, NULL, "parameter", param->name);
      xmlAddChild (funcprototype, paramdef);
    }
  xmlAddChild (parent, para);
  xmlAddChild (parent, protect_blurb (member->blurb));
  if (member->num_parameters)
    {
      para = xmlNewChild (parent, NULL, "para", "Parameters:");
      variablelist = xmlNewNode (NULL, "variablelist");
      xmlAddChild (para, variablelist);

      for (i = 0; i < member->num_parameters; i++)
        {
          COParameter *param = member->parameters[i];
          const char *blurb;
          xmlNode *varlistentry, *listitem;
          if (param == NULL)
            continue;
          varlistentry = xmlNewNode (NULL, "varlistentry");
          xmlNewChild (varlistentry, NULL, "term", param->name);
          listitem = xmlNewNode (NULL, "listitem");
          xmlAddChild (listitem, protect_blurb (param->blurb));
          xmlAddChild (varlistentry, listitem);
          xmlAddChild (variablelist, varlistentry);
        }
    }
  else
    {
      para = xmlNewChild (parent, NULL, "para", "Parameters: none");
    }
}

static xmlNode *
member_to_docbook_xml (COMember   *member)
{
  g_assert (!member->is_function);
  return protect_blurb (member->blurb);
}

static xmlNode *
structure_to_docbook_xml      (COStructure    *structure)
{
  int i;
  int num_members = g_hash_table_size (structure->members);
  xmlNode *sect;
  guint count_methods = 0;
  guint count_members = 0;

  sect = xmlNewNode (NULL, "sect1");
  xmlSetProp (sect, "id", structure->name);
  addTitle (sect, "struct", structure->name);

  xmlAddChild (sect, protect_blurb (structure->blurb));

  for (i = 0; i < num_members; i++)
    {
      const char *member_name = structure->member_names[i];
      COMember *member = g_hash_table_lookup (structure->members, member_name);
      if (member == NULL)
        continue;
      if (member->is_function)
        ++count_methods;
      else
        ++count_members;
    }

  if (count_methods)
    {
      xmlNode *para = xmlNewNode (NULL, "para");
      xmlNode *variablelist = xmlNewNode (NULL, "variablelist");
      if (count_methods > 1)
	xmlAddChild (para, xmlNewText ("Methods:"));
      else
	xmlAddChild (para, xmlNewText ("Method:"));
      xmlAddChild (para, variablelist);
      for (i = 0; i < num_members; i++)
	{
	  const char *member_name = structure->member_names[i];
	  xmlNode *varlistentry, *listitem;
	  COMember *member = g_hash_table_lookup (structure->members,
						  member_name);
	  int param;
	  if (member == NULL || ! member->is_function)
	    continue;
	  varlistentry = xmlNewNode (NULL, "varlistentry");
	  xmlNewChild (varlistentry, NULL, "term", member->name);
	  listitem = xmlNewNode (NULL, "listitem");
          method_to_docbook_xml_list (member, listitem);
	  xmlAddChild (varlistentry, listitem);
	  xmlAddChild (variablelist, varlistentry);
	}
      xmlAddChild (sect, para);
    }
  if (count_members)
    {
      xmlNode *para = xmlNewNode (NULL, "para");
      xmlNode *variablelist = xmlNewNode (NULL, "variablelist");
      if (count_members > 1)
	xmlAddChild (para, xmlNewText ("Members:"));
      else
	xmlAddChild (para, xmlNewText ("Member:"));
      xmlAddChild (para, variablelist);
      for (i = 0; i < num_members; i++)
	{
	  const char *member_name = structure->member_names[i];
	  xmlNode *varlistentry, *listitem, *term;
	  COMember *member = g_hash_table_lookup (structure->members,
						  member_name);
	  int param;
	  if (member == NULL || member->is_function)
	    continue;
	  varlistentry = xmlNewNode (NULL, "varlistentry");
	  term = xmlNewNode (NULL, "term");
	  xmlNewChild (term, NULL, "type", member->type);
	  if (needs_space_after (member->type))
	    xmlAddChild (term, xmlNewText (" "));
	  xmlNewChild (term, NULL, "structfield", member->name);
	  xmlAddChild (varlistentry, term);
	  listitem = xmlNewNode (NULL, "listitem");
	  xmlAddChild (listitem, member_to_docbook_xml (member));
	  xmlAddChild (varlistentry, listitem);
	  xmlAddChild (variablelist, varlistentry);
	}
      xmlAddChild (sect, para);
    }
  return sect;
}

static xmlNode *
struct_stub_to_docbook_xml    (COStructStub   *stub)
{
  xmlNode *rv = xmlNewNode (NULL, "para");
  xmlAddChild (rv, xmlNewText ("struct "));
  xmlNewChild (rv, NULL, "structname", stub->name);
  xmlAddChild (rv, protect_blurb (stub->blurb));
  return rv;
}


static xmlNode *
union_to_docbook_xml          (COUnion        *un)
{
  int i;
  xmlNode *rv = xmlNewNode (NULL, "sect1");
  xmlNode *para = xmlNewNode (NULL, "para");
  xmlNode *orderedlist = xmlNewNode (NULL, "orderedlist");
  xmlAddChild (para, xmlNewText ("This is a union of the following types:"));
  xmlAddChild (para, orderedlist);
  xmlAddChild (rv, para);
  for (i = 0; i < un->num_objects; i++)
    {
      xmlNode *listitem = xmlNewNode (NULL, "listitem");
      xmlNode *para = xmlNewNode (NULL, "para");
      xmlAddChild (listitem, para);
      xmlNewChild (para, NULL, "type", un->type_name_pairs[2*i+0]);
      if (needs_space_after (un->type_name_pairs[2*i+0]))
	xmlAddChild (para, xmlNewText (" "));
      xmlNewChild (para, NULL, "structfield", un->type_name_pairs[2*i+1]);
      xmlAddChild (orderedlist, listitem);
    }
  xmlAddChild (rv, protect_blurb (un->blurb));
  return rv;
}

static xmlNode *
global_to_docbook_xml         (COGlobal       *global)
{
  xmlNode *rv = xmlNewNode (NULL, "listitem");
  xmlNode *para = xmlNewNode (NULL, "para");
  xmlAddChild (rv, para);
  xmlNewChild (para, NULL, "type", global->type);
  if (needs_space_after (global->type))
    xmlAddChild (para, xmlNewText (" "));
  xmlNewChild (para, NULL, "structfield", global->name);	/*XXX*/
  xmlAddChild (rv, protect_blurb (global->blurb));
  return rv;
}

static xmlNode *
entry_to_docbook_xml   (COEntry      *entry)
{
  if (entry->enumeration != NULL)
    return enumeration_to_docbook_xml (entry->enumeration);
  if (entry->function != NULL)
    return function_to_docbook_xml (entry->function, FALSE);
  if (entry->function_typedef != NULL)
    return function_to_docbook_xml (entry->function_typedef, TRUE);
  if (entry->struct_stub != NULL)
    return struct_stub_to_docbook_xml (entry->struct_stub);
  if (entry->structure != NULL)
    return structure_to_docbook_xml (entry->structure);
  if (entry->the_union != NULL)
    return union_to_docbook_xml (entry->the_union);
  if (entry->global != NULL)
    return global_to_docbook_xml (entry->global);
  g_warning ("render_template_structure: empty entry?");
  return NULL;
}

static xmlNode *
section_to_docbook_xml (COSection    *section)
{
  xmlNode *rv = xmlNewNode (NULL, "chapter");
  xmlNode *title = xmlNewNode (NULL, "title");
  GSList *list;
  if (section->title != NULL)
    {
      xmlAddChild (title, xmlNewText (section->title));
      xmlAddChild (title, xmlNewText (" ("));
    }
  xmlNewChild (title, NULL, "filename", section->h_filename);
  if (section->title != NULL)
    {
      xmlAddChild (title, xmlNewText (")"));
    }
  xmlAddChild (rv, title);
  if (section->blurb)
    {
      xmlNode *abs = xmlNewNode (NULL, "abstract");
      xmlAddChild (abs, protect_blurb (section->blurb));
      xmlAddChild (rv, abs);
    }

  unsigned n_globals = 0;
  for (list = section->first_entry; list != NULL; list = list->next)
    if (((COEntry*)list->data)->global == NULL)
      {
        COEntry *entry = list->data;
        xmlNode *result = entry_to_docbook_xml (entry);
        if (result != NULL)
          xmlAddChild (rv, result);
        else
          {
            g_warning ("error converting entry to docbook");
          }
      }
    else
      n_globals++;
  if (n_globals != 0)
    {
      xmlNode *sect = xmlNewNode (NULL, "sect1");
      xmlNewChild (sect, NULL, "title", "Globals");
      xmlNode *para = xmlNewNode (NULL, "para");
      xmlNode *ilist = xmlNewNode (NULL, "itemizedlist");
      xmlAddChild (sect, para);
      xmlAddChild (para, ilist);
      for (list = section->first_entry; list != NULL; list = list->next)
        if (((COEntry*)list->data)->global != NULL)
          xmlAddChild (ilist, entry_to_docbook_xml (list->data));
      xmlAddChild (rv, sect);
    }
  return rv;
}

static xmlNode *
database_to_docbook_xml (CODatabase *database)
{
  xmlNode *root = xmlNewNode (NULL, "book");
  GSList *list;
  for (list = database->first_section; list != NULL; list = list->next)
    {
      COSection *section = list->data;
      if (section->first_entry)
        {
          xmlNode *chapter = section_to_docbook_xml (section);
          if (chapter == NULL)
            g_warning ("section %s failed?", section->h_filename);
          else
            xmlAddChild (root, chapter);
        }
    }
  return root;
}

gboolean          co_database_render   (CODatabase       *database)
{
  GSList *list;
  const char *filename = database->output_doc_filename;
  xmlDoc *doc;
  xmlNode *root;
  if (filename == NULL)
    filename = "codoc.xml";
  doc = xmlNewDoc ("1.0");
#if 0		/*TODO: need something like this! */
  xmlAddChild ((xmlNode*) doc,
	       xmlNewPI ("-//Norman Walsh//DTD DocBk XML V3.1.7//EN",
	        "../dtd/3.1.7/docbookx.dtd"));
#endif
  root = database_to_docbook_xml (database);
  if (root == NULL)
    {
      g_warning ("error creating documentation...");
      return FALSE;
    }
  xmlDocSetRootElement (doc, root);

  xmlCreateIntSubset (doc, "book",
                      "-//OASIS//DTD DocBook XML V4.2//EN",
                      "http://www.oasis-open.org/docbook/xml/4.2/docbookx.dtd");
  xmlSaveFormatFile (filename, doc, TRUE);
  xmlFreeDoc (doc);
  return TRUE;
}
