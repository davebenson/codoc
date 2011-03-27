#include "libcodoc.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

static void usage()
{
  fprintf (stderr,
    "usage: %s [template|xml] [--xml file] [--template file]\n"
     "\n"
     "Regenerates the codoc template and xml files.\n"
     "\nBy default, this program examines codoc.template, and\n"
     "analyzes the .h files in the current directory.\n"
     "If the template does not exist, it is created,\n"
     "and if the template is out-of-date, it is updated (and a backup of\n"
     "the old copy is made).  Then if the codoc.xml file is out-of-date\n"
     "or nonexistant, it is written.\n"
     "\nOptions:\n"
     "\t--xml FILENAME          Set the filename for the .xml file.\n"
     "\t--template FILENAME      Set the filename for the template file.\n"
     "\t--force                  Rebuild the template and xml, regardless\n"
     "\t                         of timestamps.\n"
     "\t--mode=MODE              Specify the operation mode.\n"
     "\t--subdir SUBDIR          Also look for .h files in SUBDIR.\n"
     "\n"
     "CPP Options [these are passed to cpp]\n"
     "\t-Iinclude-dir,\n"
     "\t-Dmacro-to-define        These are standard cpp flags.\n"
     "\n"
     "Operation modes:\n"
    "\tauto                      The default, use timestamps to decide\n"
    "\t                          what to do.\n"
    "\tforce                     Regenerate templates and xml.\n"
    "\txml                      Regenerate xml only.\n"
    "\txml                       Regenerate xml only [default].\n"
    "\ttemplate                  Regenerate template only.\n"
     , g_get_prgname ());
  exit (1);
}

static void version()
{
  printf ("CODOC, version %s.\nCopyright Dave Benson, 2000.\n",
          VERSION);
  exit (0);
}

/* TODO: if a .h file disappears, this won't detect it. */
static gboolean older_than_h_files (const char *target)
{
  struct stat target_stat;
  struct stat source_stat;
  struct dirent *dir_entry;
  DIR *dir;

  if (stat (target, &target_stat) < 0)
    return TRUE;
  dir = opendir (".");
  while ((dir_entry = readdir(dir)) != NULL)
    {
      const char *extension;
      extension = strrchr (dir_entry->d_name, '.');
      if (extension == NULL)
        continue;
      if (stat (dir_entry->d_name, &source_stat) < 0)
        g_error ("source %s does not exist", dir_entry->d_name);
      if (target_stat.st_mtime < source_stat.st_mtime)
        return TRUE;
    }
  closedir (dir);
  return FALSE;
}

static gboolean is_out_of_date (const char* target, const char* source)
{
  struct stat target_stat;
  struct stat source_stat;

  if (stat (target, &target_stat) < 0)
    return TRUE;
  if (stat (source, &source_stat) < 0)
    g_error ("source %s does not exist", source);
  if (target_stat.st_mtime < source_stat.st_mtime)
    return TRUE;
  return FALSE;
}

static gboolean file_exists(const char* filename)
{
  struct stat dummy;
  return stat (filename, &dummy) == 0;
}

int
main (int argc, char *argv[])
{
  int i;
  gboolean do_template = FALSE;
  gboolean do_xml = FALSE;
  gboolean auto_dependencies = TRUE;
  const char *read_template_file = NULL;
  const char *write_template_file = NULL;
  const char *output_doc_filename = NULL;
  CODatabase *database = NULL;
  gboolean template_exists;
  GString *cpp_options;
  GSList *subdirs = NULL;
  const char *cpp_flags;

  g_set_prgname(argv[0]);
  cpp_options = g_string_new ("");

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
        {
	  const char *mode = NULL;
	  if (strcmp (argv[i], "--help") == 0)
	    usage();
	  if (strcmp (argv[i], "--version") == 0)
	    version();
	  else if (strcmp (argv[i], "--force") == 0)
	    {
	      do_template = TRUE;
	      do_xml = TRUE;
	      continue;
            }
	  else if (strcmp (argv[i], "--xml") == 0)
	    {
	      output_doc_filename = argv[++i];
	      if (output_doc_filename == NULL)
	        g_error("--xml needs argument");
	      continue;
	    }
	  else if (argv[i][1] == 'I' || argv[i][1] == 'D')
	    {
	      g_string_sprintfa (cpp_options, "%s ", argv[i]);
	      continue;
	    }
	  else if (strcmp (argv[i], "--subdir") == 0)
	    {
	      char *subdir = argv[++i];
	      if (subdir == NULL)
	        g_error ("--subdir needs argument");
	      subdirs = g_slist_append (subdirs, subdir);
	      continue;
	    }
	  else if (strcmp (argv[i], "--template") == 0)
	    {
	      read_template_file = argv[++i];
	      write_template_file = read_template_file;
	      if (read_template_file == NULL)
	        g_error("--template needs argument");
	      continue;
	    }
	  else if (strcmp (argv[i], "--mode") == 0)
	    {
	      mode = argv[++i];
	      if (mode == NULL)
	        g_error ("--mode needs argument");
	    }
	  else if (strncmp (argv[i], "--mode=", 7) == 0)
	    {
	      mode = argv[i] + 7;
	    }
	  else
	    g_error("unrecognized option: %s", argv[i]);

	  if (mode != NULL)
	    {
	      if (strcmp (mode, "auto") == 0)
	        {
		  /* this is the default: nothing to do */
		}
	      else if (strcmp (mode, "xml") == 0)
	        {
		  auto_dependencies = FALSE;
		  do_xml = TRUE;
		}
	      else if (strcmp (mode, "template") == 0)
	        {
		  auto_dependencies = FALSE;
		  do_template = TRUE;
		}
	      else if (strcmp (mode, "force") == 0)
	        {
		  auto_dependencies = FALSE;
		  do_xml = TRUE;
		  do_template = TRUE;
		}
	      else
	        { 
		  g_error ("unrecognized mode: %s, "
		           "must be auto,xml,template or force", mode);
		}
	    }
	}
    }

  if (read_template_file == NULL && write_template_file == NULL)
    {
      gboolean template_exists = file_exists ("codoc.template");
      gboolean xml_template_exists = file_exists ("codoc.template.xml");
      if (template_exists && xml_template_exists)
	{
	  g_warning ("you should get rid of codoc.template: it is replaced "
		     "by codoc.template.xml");
	  read_template_file = "codoc.template.xml";
	  write_template_file = "codoc.template.xml";
	}
      else if (template_exists)
	{
	  g_warning ("You have just a deprecated codoc.template");
	  g_warning ("I will read from it and output a codoc.template.xml");
	  read_template_file = "codoc.template";
	  write_template_file = "codoc.template.xml";
	}
      else if (xml_template_exists)
	{
	  read_template_file = "codoc.template.xml";
	  write_template_file = "codoc.template.xml";
	}
      else
	{
	  g_message ("will create a new codoc.template.xml");
	  write_template_file = "codoc.template.xml";
	}
    }
  if (cpp_options->len == 0)
    cpp_flags = NULL;
  else
    cpp_flags = cpp_options->str;

  if (auto_dependencies)
    {
      /* check whether we need to regenerate the template. */
      if (older_than_h_files (read_template_file))
	do_template = TRUE;

      /* check whether we need to regenerate the xml. */
      if (do_template
       || is_out_of_date (output_doc_filename, write_template_file))
	do_xml = TRUE;
    }
  if (write_template_file != NULL && read_template_file != NULL
   && strcmp (write_template_file, read_template_file) != 0)
    {
      do_template = TRUE;
    }

  if (read_template_file != NULL)
    {
      char *ext = strrchr (read_template_file, '.');
      if (strcmp (ext, ".template") == 0)
	database = co_database_load_old (read_template_file);
      else
	database = co_database_load_xml (read_template_file);
      if (database == NULL)
        g_error("error loading template %s", read_template_file);
    }
  if (database == NULL)
    database = co_database_new ();

  if (cpp_flags != NULL)
    co_database_set_cpp_flags (database, cpp_flags);
  
  {
    GSList *at;
    for (at = subdirs; at != NULL; at = at->next)
      co_database_add_subdir (database, (char*) (at->data));
    g_slist_free (subdirs);
  }
  
  if (do_template)
    {
      if (! co_database_merge (database, cpp_flags))
        {
	  g_warning ("error merging database with .h files");
	  exit (1);
	}
      if (! co_database_safe_save (database, write_template_file))
        {
	  g_warning ("error saving template %s", write_template_file);
	  exit (1);
	}
    }

  if (do_xml)
    {
      if (output_doc_filename != NULL)
        {
          g_free (database->output_doc_filename);
	  database->output_doc_filename = g_strdup (output_doc_filename);
	}
      if (!co_database_render (database))
        g_error ("error writing the xml file");
    }

  return 0;
}
