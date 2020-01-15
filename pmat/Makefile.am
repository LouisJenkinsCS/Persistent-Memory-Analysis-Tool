# Derived from Intel PMDK's pmat Tool

include $(top_srcdir)/Makefile.tool.am

EXTRA_DIST = docs/pmat-manual.xml

#----------------------------------------------------------------------------
# Headers
#----------------------------------------------------------------------------

pkginclude_HEADERS = pmat.h

noinst_HEADERS = pmat_include.h

#----------------------------------------------------------------------------
# pmat-<platform>
#----------------------------------------------------------------------------

noinst_PROGRAMS  = pmat-@VGCONF_ARCH_PRI@-@VGCONF_OS@
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += pmat-@VGCONF_ARCH_SEC@-@VGCONF_OS@
endif

PMAT_SOURCES_COMMON = \
	pmat_main.c \
	pmat_common.c

pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_SOURCES      = \
	$(PMAT_SOURCES_COMMON)
pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) -O2
pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_PRI_CAPS@)
pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_PRI_CAPS@)
pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_PRI@ \
	$(LINK) \
	$(pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS) \
	$(pmat_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS)

if VGCONF_HAVE_PLATFORM_SEC
pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_SOURCES      = \
	$(PMAT_SOURCES_COMMON)
pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@) -O2
pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_SEC_CAPS@)
pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_SEC_CAPS@)
pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_SEC@ \
	$(LINK) \
	$(pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS) \
	$(pmat_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS)
endif

# pmat_main.c contains the helper function for pmat that get called
# all the time. To maximise performance compile with -fomit-frame-pointer
# Primary beneficiary is x86.
pmat_main.o: CFLAGS += -fomit-frame-pointer
