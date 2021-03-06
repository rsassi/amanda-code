# Makefile for amanda man-pages

transform =	s,x,x,;

AMPLOT_MAN_PAGES =  amplot.8

COMMON_MAN_PAGES =  amanda.8 \
		    amanda.conf.5 \
		    amanda-client.conf.5

SERVER_MAN_PAGES =  amadmin.8 \
		    amcheck.8 \
		    amcheckdb.8 \
		    amcheckdump.8 \
		    amcleanup.8 \
		    amdd.8 \
		    amdevcheck.8 \
		    amdump.8 \
		    amflush.8 \
		    amgetconf.8 \
		    amlabel.8 \
		    ammt.8 \
		    amoverview.8 \
		    amreport.8 \
		    amrmtape.8 \
		    amstatus.8 \
		    amtape.8 \
		    amtapetype.8 \
		    amtoc.8 \
		    amverify.8 \
		    amverifyrun.8 \
		    amserverconfig.8 \
		    amaddclient.8 \
		    amcheckdump.8 \
		    amcrypt.8 \
		    amaespipe.8 \
		    amgpgcrypt.8 \
		    amcryptsimple.8  \
		    amcrypt-ossl.8 \
		    amcrypt-ossl-asym.8

RECOVER_MAN_PAGES =  amrecover.8

RESTORE_MAN_PAGES =  amrestore.8 \
		     amfetchdump.8

ALL_MAN_PAGES = $(AMPLOT_MAN_PAGES) \
	   $(COMMON_MAN_PAGES) \
	   $(COMMON_MAN_PAGES) \
	   $(SERVER_MAN_PAGES) \
	   $(RECOVER_MAN_PAGES) \
	   $(RESTORE_MAN_PAGES)

WANTED_MAN_PAGES = $(COMMON_MAN_PAGES)
if WANT_AMPLOT
  WANTED_MAN_PAGES += $(AMPLOT_MAN_PAGES)
endif
if WANT_SERVER
  WANTED_MAN_PAGES += $(SERVER_MAN_PAGES)
endif
if WANT_RECOVER
  WANTED_MAN_PAGES += $(RECOVER_MAN_PAGES)
endif
if WANT_RESTORE
  WANTED_MAN_PAGES += $(RESTORE_MAN_PAGES)
endif

SRCMANPAGEDIR  = $(srcdir)/xml-source

MAN_XML = $(ALL_MAN_PAGES:%=xml-source/%.xml)

EXTRA_XML = xslt/expand-sambadoc.xsl \
            xslt/man.xsl.in \
            xslt/html.xsl.in \
            xslt/settings.xsl \
	    entities/global.entities \
	    entities/xinclude.dtd

EXTRA_DIST = $(ALL_MAN_PAGES) $(MAN_XML) $(EXTRA_HTML) $(EXTRA_XML)

GEN_XML = $(ALL_MAN_PAGES:%=xml-source/%.proc.xml)

MOSTLYCLEANFILES = $(GEN_XML)
MAINTAINERCLEANFILES = $(ALL_MAN_PAGES)

# some of the XSL needs to know what docbook-xsl version we've selected
%.xsl: %.xsl.in $(top_builddir)/config.status
	$(top_builddir)/config.status --file=$@:$<

if   ENABLE_MANPAGE_BUILD
# phase one of the documentation build expands some macros.
xml-source/%.proc.xml: $(SRCMANPAGEDIR)/%.xml $(srcdir)/xslt/expand-sambadoc.xsl
	$(XSLTPROC) $(XSLTPROC_FLAGS) --path $(srcdir)/xslt/ --xinclude --stringparam latex.imagebasedir "$*/" --stringparam noreference 1 --output $@ $(srcdir)/xslt/expand-sambadoc.xsl $<

# phase two generates a manpage from the resulting "expanded" docbook XML
%: xml-source/%.proc.xml $(srcdir)/xslt/man.xsl
	$(XSLTPROC) $(XSLTPROC_FLAGS) --path $(srcdir)/xslt/ --output $@ xslt/man.xsl $<

%.html: xml-source/%.proc.xml $(srcdir)/xslt/html.xsl
	$(XSLTPROC) $(XSLTPROC_FLAGS) --path $(srcdir)/xslt/ --output $@ xslt/html.xsl $<

else	!ENABLE_MANPAGE_BUILD

%: $(SRCMANPAGEDIR)/%.xml
	@echo WARNING: Not building needed $@ because building manpages is disabled.
	@echo 'DUMMY' > $@

# (no rule to build HTML without ENABLE_MANPAGE_BUILD)

endif

##
## HTML Generation
##

EXTRA_DIST += \
	index.php \
	amanda.css

ALL_MAN_HTMLS = $(ALL_MAN_PAGES:%=%.html)

# toplevel rule to build HTML manpages and tar them up
html: amanda.css index.php $(ALL_MAN_HTMLS)
	tar -zcf html.tar.gz $(ALL_MAN_HTMLS) amanda.css index.php

# double-check that we don't ship dummy manpages
dist-hook:
	for f in $(WANTED_MAN_PAGES); do \
		if sed -e 1q $(distdir)/$$f | $(GREP) '^DUMMY$$' >/dev/null 2>/dev/null; then \
			echo "$(distdir)/$$f is a dummy manpage; cannot create distribution"; \
			exit 1; \
		fi; \
	done

man_MANS = $(WANTED_MAN_PAGES)
