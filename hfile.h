#ifndef __HFILE_H_
#define __HFILE_H_

#define HFILE_TYPE_LIST      5353400
#define HFILE_TYPE_TEXT      5353401
#define HFILE_TYPE_SEMICOLON 5353402
#define HFILE_TYPE_COMMENT   5353403

#define _HFILE_TYPE_MIN      HFILE_TYPE_LIST
#define _HFILE_TYPE_MAX      HFILE_TYPE_COMMENT

typedef struct _HFileList HFileList;
typedef struct _HFileSemicolon HFileSemicolon;
typedef struct _HFileText HFileText;
typedef struct _HFileComment HFileComment;
typedef struct _HFileAny HFileAny;
typedef union _HFile HFile;
#define HFILE_GEN_CAST(object, type, type_enum) \
	((type*) (hfile_cast(object, type_enum, #type, __FILE__, __LINE__)))
#define HFILE(ptr)            hfile_base_cast(ptr, __FILE__, __LINE__)
#define HFILE_ANY(ptr)        (HFileAny*) hfile_base_cast(ptr,__FILE__,__LINE__)
#define HFILE_LIST(ptr)       HFILE_GEN_CAST(ptr, HFileList, HFILE_TYPE_LIST)
#define HFILE_SEMICOLON(ptr)  HFILE_GEN_CAST(ptr, HFileSemicolon, HFILE_TYPE_SEMICOLON)
#define HFILE_TEXT(ptr)       HFILE_GEN_CAST(ptr, HFileText, HFILE_TYPE_TEXT)
#define HFILE_COMMENT(ptr)    HFILE_GEN_CAST(ptr, HFileComment, HFILE_TYPE_COMMENT)

#include <glib.h>
#include <stdio.h>

HFile   *hfile_base_cast(gpointer ptr, const char *file, int line);
gpointer hfile_cast(gpointer object, int type, const char* type_name,
                           const char *file, int line);

                                                   
struct _HFileList {
	int             type;
	int             lineno;
	char           *filename;

	gboolean	has_braces;
	GSList         *list;
};
struct _HFileSemicolon {
	int             type;
	int             lineno;
	char           *filename;
};
struct _HFileText {
	int             type;
	int             lineno;
	char           *filename;

	char           *text;
};
/* these are created but not interpreted at the moment. */
struct _HFileComment {
	int		type;
	int             lineno;
	char           *filename;

	char           *comment;
};
struct _HFileAny {
 	int             type;
	int             lineno;
	char           *filename;
};
union _HFile {
	int type;
	HFileAny any;
	HFileList list;
	HFileSemicolon semicolon;
	HFileText text;
	HFileComment comment;
};

HFileList*     hfile_parse   (const char      *filename,
                              const char      *cpp_options);
void           hfile_destroy (HFile           *to_destroy);

void           hfile_dump    (HFile           *hfile,
                              int              indent,
			      FILE            *output);

/* private */
char *hfile_get_word (const char *text);
#endif 
