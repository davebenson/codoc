AUTOMAKE_MODE = gnits
bin_PROGRAMS = codoc
noinst_PROGRAMS = codoctest
codoctest_SOURCES = libcodoc.c codoctest.c hfile.c
codoc_SOURCES = codoc.c hfile.c hfile.h libcodoc.c libcodoc.h cooutput.c \
libcodoc-old-template.c libcodoc-xml.c
SUBDIRS = doc
INCLUDES = @GLIB_CFLAGS@ @XML_CFLAGS@
LDADD = @GLIB_LIBS@ @XML_LIBS@

EXTRA_DIST = build/pkgwrite 
DEBARCH = @ARCH@

deb:
	test -r codoc-@VERSION@.tar.gz || $(MAKE) dist
	$(srcdir)/build/pkgwrite --format=debian \
	  --tarball=codoc-@VERSION@.tar.gz \
	  --output=codoc-packages \
	  --pkgwriteinfo-file=pkgwriteinfo \
	  --arch=$(DEBARCH)

rpm:
	test -r codoc-@VERSION@.tar.gz || $(MAKE) dist
	$(srcdir)/build/pkgwrite --format=redhat \
	  --tarball=codoc-@VERSION@.tar.gz \
	  --output=codoc-packages \
	  --pkgwriteinfo-file=pkgwriteinfo \
	  --arch=$(DEBARCH)

