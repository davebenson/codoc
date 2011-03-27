

SECTIONS         = $(shell cat sections)
SECTION_HTML     = $(patsubst %, %.html, $(SECTIONS))
EVAL_HTML	 = 
TXT_HTML         = 
SGML_HTML	 = 

HTML             = $(SECTION_HTML) $(TXT_HTML) $(EVAL_HTML)

VERSION          = 0.1

website: $(HTML) $(SGML_HTML)

default: $(HTML)

%.html: main-template %-body sections navbar-one navbar-template eval-template
	./eval-template project          CODOC                          \
			sf_project_name  codoc                          \
	                body             $*                             \
			release_version '$(VERSION)'                    \
			sf_group_id     9349                            \
			at              '@'                             \
			title           'main page'     < main-template \
			                                > $*.html


clean:
	rm -f $(HTML)

SF_HOST_SPEC=davebenson@codoc.sourceforge.net

update: website
	cd ../doc ; make html
	ssh $(SF_HOST_SPEC) 'rm -rf /home/groups/codoc/htdocs/manual'
	scp -r ../doc/codoc-manual $(SF_HOST_SPEC):/home/groups/codoc/htdocs/manual
	scp *.html $(SF_HOST_SPEC):/home/groups/codoc/htdocs
