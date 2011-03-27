#ifndef __CO_DOC_H_
#define __CO_DOC_H_

#include <glib.h>
#include <tree.h>

typedef struct _COContext COContext;
typedef struct _COParameter COParameter;
typedef struct _COFunction COFunction;
typedef struct _COEnumerationValue COEnumerationValue;
typedef struct _COEnumeration COEnumeration;
typedef struct _COMember COMember;
typedef struct _COUnion COUnion;
typedef struct _COGlobal COGlobal;
typedef struct _COStructure COStructure;
typedef struct _COStructStub COStructStub;
typedef struct _CODatabase CODatabase;
typedef struct _COEntry COEntry;
typedef struct _COSection COSection;
typedef struct _COPrivacyInfo COPrivacyInfo;

struct _COPrivacyInfo
{
  gboolean       is_private;
};

/* Context for error messages. */
struct _COContext {
  const char    *file;
  int            lineno;
  gboolean       in_h_file;
  COPrivacyInfo  privacy_info;
};

void context_message(COContext       *context,
                     const char      *format,
                     ...);
void context_warning(COContext       *context,
                     const char      *format,
                     ...);

typedef enum
{
  CO_ENTRY_FLAGS_PRIVATE                = (1 << 0),
  CO_ENTRY_FLAGS_GOT_IN_TEMPLATE        = (1 << 1),
  CO_ENTRY_FLAGS_GOT_IN_HFILE           = (1 << 2)
} COEntryFlags;

/* note: 'blurb' is always a *list* of nodes */

struct _COParameter
{
  char          *type;
  char          *name;
  xmlNode       *blurb;
  xmlNode       *conflict;
  COEntryFlags   flags;
};

struct _COFunction {
  char          *name;
  char          *return_type;
  int            num_parameters;
  COParameter  **parameters;
  xmlNode       *blurb;
  xmlNode       *conflict;
  COEntryFlags   flags;
};

struct _COMember {
  char          *type;
  char          *name;
  xmlNode       *blurb;
  xmlNode       *conflict;

  gboolean       is_function;
  int            num_parameters;
  COParameter  **parameters;
  COEntryFlags   flags;
};

struct _COStructure {
  char          *name;
  GHashTable    *members;
  xmlNode       *blurb;
  xmlNode       *conflict;

  /* these are in the order they appear, for static initializers. */
  char         **member_names;
  COEntryFlags   flags;
};

struct _COUnion {
  char          *name;
  int            num_objects;
  char         **type_name_pairs;
  xmlNode       *blurb;
  xmlNode       *conflict;
  COEntryFlags   flags;
};

struct _COEnumerationValue {
  char          *name;
  xmlNode       *blurb;
  xmlNode       *conflict;
  COEntryFlags   flags;
};

struct _COEnumeration {
  char          *name;
  xmlNode       *blurb;
  xmlNode       *conflict;
  GHashTable    *values;
  COEntryFlags   flags;
};

struct _COGlobal {
  char          *type;
  char          *name;
  xmlNode       *blurb;
  xmlNode       *conflict;
  COEntryFlags   flags;
};

struct _COStructStub {
  char          *name;
  xmlNode       *blurb;
  xmlNode       *conflict;
  COEntryFlags   flags;
};

struct _COEntry {
  COEnumeration *enumeration;
  COFunction    *function;
  COFunction    *function_typedef;
  COStructure   *structure;
  COGlobal      *global;
  COUnion       *the_union;     /* sigh, inconsistent b/c union is reserved */
  COStructStub  *struct_stub;
};

struct _COSection {
  char          *h_filename;
  char          *title;
  GSList        *first_entry;
  GSList        *last_entry;
  xmlNode       *blurb;
  xmlNode       *conflict;
  COEntryFlags   flags;
};


struct _CODatabase {
  GHashTable    *functions;
  GHashTable    *function_typedefs;
  GHashTable    *structures;
  GHashTable    *struct_stubs;
  GHashTable    *enumerations;
  GHashTable    *globals;
  GHashTable    *unions;
  GHashTable    *sections;
  GHashTable    *omissions;

  GSList        *first_section;
  GSList        *last_section;

  GSList        *subdirs;
  char          *cpp_flags;

  char          *output_doc_filename;

  COSection     *default_section;
};

CODatabase*   co_database_new            ();
CODatabase*   co_database_load_xml       (const char       *filename);
CODatabase*   co_database_load_old       (const char       *filename);
gboolean      co_database_safe_save      (CODatabase       *database,
                                          const char       *filename);
gboolean      co_database_merge          (CODatabase       *database,
                                          const char       *cpp_flags);
void          co_database_destroy        (CODatabase       *database);
gboolean      co_database_render         (CODatabase       *database);

void          co_database_set_cpp_flags  (CODatabase       *database,
                                          const char       *cpp_flags);
void          co_database_add_subdir     (CODatabase       *database,
                                          const char       *subdirs);


gboolean      codoc_util_is_c_type       (const char     **pstr);


/* --- methods for building a database: note xml_blurb's ownership
 *     is transfered to the called function! */
void co_database_add_omission            (CODatabase      *database,
                                          const char      *omission);
void co_database_add_global              (CODatabase      *database,
                                          const char      *name,
                                          const char      *type,
                                          xmlNode         *blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_union               (CODatabase      *database,
                                          const char      *union_name,
                                          int              num_pairs,
                                          char           **type_name_pairs,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_struct_stub         (CODatabase      *database,
                                          const char      *struct_name,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_enum                (CODatabase      *database,
                                          const char      *enum_name,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_enum_value          (CODatabase      *database,
                                          const char      *enum_name,
                                          const char      *value_name,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_struct              (CODatabase      *database,
                                          const char      *struct_name,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_struct_member       (CODatabase      *database,
                                          const char      *struct_name,
                                          const char      *member_name,
                                          const char      *member_type,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_member_function     (CODatabase      *database,
                                          const char      *struct_name,
                                          const char      *name,
                                          const char      *return_type,
                                          int              num_parameters,
                                          COParameter    **parameters,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_struct_member_param (CODatabase      *database,
                                          const char      *struct_name, 
                                          const char      *method_name,
                                          int              param_index,
                                          const char      *param_name,
                                          const char      *param_type,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_new_section             (CODatabase      *database,
                                          const char      *h_filename,
                                          const char      *title,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_function            (CODatabase      *database,
                                          gboolean         is_typedef,
                                          const char      *function_name,
                                          const char      *return_type,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);
void co_database_add_function_param      (CODatabase      *database,
                                          gboolean         is_typedef,
                                          const char      *function_name,
                                          int              param_index,
                                          const char      *parameter_name,
                                          const char      *parameter_type,
                                          xmlNode         *xml_blurb,
                                          xmlNode         *conflict,
                                          COEntryFlags     flags,
                                          COContext       *context);

#endif
