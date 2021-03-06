##
## Prepare for SWIGging
## 

include $(top_srcdir)/config/automake/vars.am
include $(top_srcdir)/config/automake/scripts.am

# add the SWIG symbol to avoid conflicts with perl definitions.
# $AMANDA_WARNING_CFLAGS is omitted because SWIG-generated files tend
# to trigger warnings
AM_CFLAGS = -DSWIG

# Appropriate INCLUDES depends on which components (server or client) are 
# being built
INCLUDES =  \
            -I$(top_builddir)/common-src \
            -I$(top_srcdir)/common-src \
            -I$(top_srcdir)/perl/amglue \
            -I$(top_srcdir)/gnulib \
            $(PERL_INC)

if WANT_SERVER
INCLUDES += -I$(top_srcdir)/device-src \
	    -I$(top_srcdir)/server-src
endif

if WANT_CLIENT
INCLUDES += -I$(top_srcdir)/client-src
endif

if WANT_RESTORE
INCLUDES += -I$(top_srcdir)/restore-src
endif

if WANT_RECOVER
INCLUDES += -I$(top_srcdir)/recover-src
endif

# (amplot has a conditional, but doesn't have any header files)

##
## libamglue -- helper functions for gluing amanda to perl
##

amlib_LTLIBRARIES = libamglue.la
libamglue_la_SOURCES = \
	amglue/ghashtable.c \
	amglue/bigint.c \
	amglue/amglue.h
libamglue_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
EXTRA_DIST += $(libamglue_la_SOURCES)

##
## HTML Generation
##

# This isn't a very robust implementation, as it's only meant to make the HTML
# available online as part of the automated compilation process.  It assumes
# that all perl modules were built (not just server or client), and that the
# build directory does not contain any "junk".

# HTML_SOURCE_FILES should contain the .pm files from which the HTML should
# be geneated.  This is just the concatenation of foo_DATA for all $(foodir)
# into which perl modules are installed.
HTML_SOURCE_FILES=
html: make_html amperl.css $(HTML_SOURCE_FILES)
	rm -rf html-tmp/ && mkdir html-tmp/
	cp amperl.css html-tmp/
	$(PERL) make_html html-tmp/ $(HTML_SOURCE_FILES)
	cd html-tmp/ && tar -zcf $(abs_builddir)/html.tar.gz .
	rm -rf html-tmp/
##
## SWIG-generated packages
##

# Tell libtool to not bother versioning the libs we build, since perl won't
# pay attention to such things, anyway.
PERL_EXT_LDFLAGS = -avoid-version

# And add any flags the user has requested; on FreeBSD, this pulls in the 
# threading library
PERL_EXT_LDFLAGS += $(PERLEXTLIBS)

# list our SWIG libraries. 
AMGLUE_SWG = amglue/amglue.swg \
	amglue/amglue.swg \
	amglue/constants.swg \
	amglue/dumpspecs.swg \
	amglue/exports.swg \
	amglue/glib.swg \
	amglue/integers.swg
EXTRA_DIST += $(AMGLUE_SWG)

# SWIG OPTIONS:
# -perl5 -proxy: generate perl code, with proxy (shadow) classes
# -DSWIG_TYPE_TABLE: the name for the global type table; customized
#  to minimize type collisions with other SWIG-generated modules
# -I$(srcdir) use include files relative to this directory
SWIG_OPTIONS=-perl5 -proxy -DSWIG_TYPE_TABLE=SWIG_TYPES_Amanda -I$(srcdir)

# Suffix rules.  These are parallel-build compatible, but may cause
# strange behavior if there are other .c files in this directory.  The
# 'rm -f $@' is needed because SWIG sometimes fails to remove its output
# file in the event of a syntax error.  .i files are pre-processor output;
# they are only used during development.
%.c : %.swg $(AMGLUE_SWG)
	$(mkdir_p) `dirname $@`
	$(SWIG) $(SWIG_OPTIONS) -o $@ $(top_srcdir)/perl/$< || { rm -f $@; false; }
%.pm : %.c
	@echo "$@ was produced as a side-effect of creating $<"
%.i : %.swg $(AMGLUE_SWG)
	$(SWIG) $(SWIG_OPTIONS) -E $(top_srcdir)/perl/$< >$@

Amandadir=$(amperldir)/Amanda
Amanda_DATA =
HTML_SOURCE_FILES += $(Amanda_DATA)

if WANT_SERVER
# PACKAGE: Amanda::Device
libDevicedir = $(amperldir)/auto/Amanda/Device
libDevice_LTLIBRARIES = libDevice.la
libDevice_la_SOURCES = Amanda/Device.c $(AMGLUE_SWG)
libDevice_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libDevice_la_LIBADD = libamglue.la \
	$(top_builddir)/device-src/libamdevice.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Device.pm
EXTRA_DIST += Amanda/Device.swg Amanda/Device.pm
MAINTAINERCLEANFILES += Amanda/Device.c Amanda/Device.pm

# PACKAGE: Amanda::Logfile
libLogfiledir = $(amperldir)/auto/Amanda/Logfile
libLogfile_LTLIBRARIES = libLogfile.la
libLogfile_la_SOURCES = Amanda/Logfile.c $(AMGLUE_SWG)
libLogfile_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libLogfile_la_LIBADD = libamglue.la \
	$(top_builddir)/server-src/libamserver.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Logfile.pm
EXTRA_DIST += Amanda/Logfile.swg Amanda/Logfile.pm
MAINTAINERCLEANFILES += Amanda/Logfile.c Amanda/Logfile.pm

# PACKAGE: Amanda::Cmdline
libCmdlinedir = $(amperldir)/auto/Amanda/Cmdline
libCmdline_LTLIBRARIES = libCmdline.la
libCmdline_la_SOURCES = Amanda/Cmdline.c $(AMGLUE_SWG)
libCmdline_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libCmdline_la_LIBADD = libamglue.la \
	$(top_builddir)/server-src/libamserver.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Cmdline.pm
EXTRA_DIST += Amanda/Cmdline.swg Amanda/Cmdline.pm
MAINTAINERCLEANFILES += Amanda/Cmdline.c Amanda/Cmdline.pm

# PACKAGE: Amanda::Tapefile
libTapefiledir = $(amperldir)/auto/Amanda/Tapefile
libTapefile_LTLIBRARIES = libTapefile.la
libTapefile_la_SOURCES = Amanda/Tapefile.c $(AMGLUE_SWG)
libTapefile_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libTapefile_la_LIBADD = libamglue.la \
	$(top_builddir)/server-src/libamserver.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Tapefile.pm
EXTRA_DIST += Amanda/Tapefile.swg Amanda/Tapefile.pm
MAINTAINERCLEANFILES += Amanda/Tapefile.c Amanda/Tapefile.pm

# PACKAGE: Amanda::Changer
Amanda_DATA += Amanda/Changer.pm
EXTRA_DIST += Amanda/Changer.pm

endif

# PACKAGE: Amanda::Debug
libDebugdir = $(amperldir)/auto/Amanda/Debug
libDebug_LTLIBRARIES = libDebug.la
libDebug_la_SOURCES = Amanda/Debug.c $(AMGLUE_SWG)
libDebug_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libDebug_la_LIBADD = libamglue.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Debug.pm
EXTRA_DIST += Amanda/Debug.swg Amanda/Debug.pm
MAINTAINERCLEANFILES += Amanda/Debug.c Amanda/Debug.pm

# PACKAGE: Amanda::Config
libConfigdir = $(amperldir)/auto/Amanda/Config
libConfig_LTLIBRARIES = libConfig.la
libConfig_la_SOURCES = Amanda/Config.c $(AMGLUE_SWG)
libConfig_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libConfig_la_LIBADD = libamglue.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Config.pm
EXTRA_DIST += Amanda/Config.swg Amanda/Config.pm
MAINTAINERCLEANFILES += Amanda/Config.c Amanda/Config.pm

# PACKAGE: Amanda::Util
libUtildir = $(amperldir)/auto/Amanda/Util
libUtil_LTLIBRARIES = libUtil.la
libUtil_la_SOURCES = Amanda/Util.c $(AMGLUE_SWG)
libUtil_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libUtil_la_LIBADD = libamglue.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Util.pm
EXTRA_DIST += Amanda/Util.swg Amanda/Util.pm
MAINTAINERCLEANFILES += Amanda/Util.c Amanda/Util.pm

# PACKAGE: Amanda::Types
libTypesdir = $(amperldir)/auto/Amanda/Types
libTypes_LTLIBRARIES = libTypes.la
libTypes_la_SOURCES = Amanda/Types.c $(AMGLUE_SWG)
libTypes_la_LDFLAGS = $(PERL_EXT_LDFLAGS)
libTypes_la_LIBADD = libamglue.la \
	$(top_builddir)/common-src/libamanda.la
Amanda_DATA += Amanda/Types.pm
EXTRA_DIST += Amanda/Types.swg Amanda/Types.pm
MAINTAINERCLEANFILES += Amanda/Types.c Amanda/Types.pm

# PACKAGE: Amanda::Paths
Amanda_DATA += Amanda/Paths.pm
EXTRA_DIST += Amanda/Paths.pm.in
DISTCLEANFILES += Amanda/Paths.pm

# perl doesn't use the cyg prefix and it doesn't search in the bin
# directory, dll must be copied to their modules directories.
if WANT_CYGWIN_COPY_PERL_DLL
amperldirauto="$(DESTDIR)$(amperldir)/auto/Amanda"
install-data-hook:
	@for cygdll in $(amperldirauto)/bin/cyg*.dll; do \
	    destfname=`echo $$cygdll|sed 's!/bin/cyg\([^.]*\).dll!/\1/\1.dll!'`; \
	    cp "$$cygdll" "$$destfname"; \
	done
endif
