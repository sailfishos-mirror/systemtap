# Makefile.am --- automake input file for systemtap dyninst tools

AUTOMAKE_OPTIONS = subdir-objects

AM_CFLAGS = -Wall -Wunused -W -Wformat # -Wformat=2
AM_CXXFLAGS = -Wall -Wunused -W -Wformat # -Wformat=2
if Werror
AM_CFLAGS += -Werror
AM_CXXFLAGS += -Werror
endif
AM_CPPFLAGS = -D_GNU_SOURCE
AM_CPPFLAGS += -I$(srcdir)/../includes
AM_CPPFLAGS += -I$(builddir)/../includes/sys
AM_CPPFLAGS += -DBINDIR='"$(bindir)"' -DSYSCONFDIR='"$(sysconfdir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"' -DLOCALEDIR='"$(localedir)"'

AM_CFLAGS += @PIECFLAGS@
AM_CXXFLAGS += @PIECXXFLAGS@
AM_LDFLAGS = @PIELDFLAGS@ @BOOST_SYSTEM_LIB@

if HAVE_SELINUX
AM_CPPFLAGS += $(selinux_CFLAGS)
AM_LDFLAGS += $(selinux_LIBS)
endif

bin_PROGRAMS =
noinst_PROGRAMS =

if HAVE_DYNINST

bin_PROGRAMS += stapdyn
noinst_PROGRAMS += dynsdt

man_MANS = stapdyn.8

stapdyn_SOURCES = stapdyn.cxx mutator.cxx mutatee.cxx \
				  dynprobe.cxx dynutil.cxx ../staputil.cxx
stapdyn_CXXFLAGS = $(AM_CXXFLAGS) @DYNINST_CXXFLAGS@ $(debuginfod_CFLAGS)
stapdyn_LDFLAGS = $(AM_LDFLAGS) @DYNINST_LDFLAGS@ $(debuginfod_LDFLAGS)
stapdyn_LDADD = -ldl -ldyninstAPI -lsymtabAPI -lpthread $(debuginfod_LIBS)
stapdyn_CFLAGS = $(debuginfod_CFLAGS)

dynsdt_SOURCES = dynsdt.cxx dynutil.cxx ../staputil.cxx
dynsdt_CPPFLAGS = $(AM_CPPFLAGS)
dynsdt_CXXFLAGS = $(AM_CXXFLAGS) @DYNINST_CXXFLAGS@ $(debuginfod_CFLAGS)
dynsdt_LDFLAGS = $(AM_LDFLAGS) @DYNINST_LDFLAGS@ $(debuginfod_LDFLAGS)
dynsdt_LDADD = -ldl -lelf -ldyninstAPI -lsymtabAPI -lpthread $(debuginfod_LIBS)
dynsdt_CFLAGS = $(debuginfod_CFLAGS)

endif # HAVE_DYNINST

BUILT_SOURCES =

# Arrange for the top-level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)
