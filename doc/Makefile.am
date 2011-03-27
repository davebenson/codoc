source_documentation = codoc-manual.xml

# XXX: need a rule to build plain text documentation!
built_documentation = codoc-manual.html

documentation = $(source_documentation) $(built_documentation)

docdir = $(datadir)/doc/codoc
doc_DATA = $(documentation)
	
EXTRA_DIST = $(documentation)

html: codoc-manual.html

codoc-manual.html: $(srcdir)/codoc-manual.xml
	echo XXX xmlto codoc-manual.html $(srcdir)/codoc-manual.xml

codoc-manual.txt: codoc-manual.html
	echo XXX lynx -dump codoc-manual.html > codoc-manual.txt
