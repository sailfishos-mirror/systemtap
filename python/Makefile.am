# Makefile.am --- automake input file

AUTOMAKE_OPTIONS = subdir-objects

# Note that we have to use absolute directores here since we change
# directories back to the source directory when building the python
# extension.
AM_CPPFLAGS = -I$(abs_srcdir)/../includes
AM_CPPFLAGS += -I$(abs_builddir)/../includes/sys

# Any script in the following variable will get byte-compiled at
# install time.
pkglibexecpython_PYTHON =

pkglibexecpython_DATA =

pkglibexecpythondir = $(pkglibexecdir)/python

if HAVE_PYTHON_PROBES

# Why aren't we putting stap-resolve-module-function.py into
# pkglibexecpython_PYTHON? stap-resolve-module-function.py is a
# python script, but it will be run by the translator as either a
# python version 3 script. So, it doesn't need to
# be executable (since we'll be calling "python3
# stap-resolve-module-function.py ARGS."
pkglibexecpython_DATA += stap-resolve-module-function.py

all-local:
if HAVE_PYTHON3_PROBES
	(cd $(srcdir); CFLAGS="$(CFLAGS) $(AM_CPPFLAGS)" $(PYTHON3) -m pip wheel . \
		 --wheel-dir $(shell readlink -f $(builddir))/py3build \
		 --no-deps --no-index --no-build-isolation \
		 --verbose)
endif


# Note that we're installing from the wheel we just built.
#
# --no-compile: pip byte-compiles with the paths it writes to, so with
# DESTDIR set the .pyc files record $(DESTDIR)-prefixed source names, which
# are wrong on the installed system (and defeat reproducible builds).  The
# bytecode is regenerated on first import instead.  Older pip versions lack
# the option, so probe for it rather than assume.
#
# PEP 610 direct_url.json records the file:// URL of the wheel in the build
# tree, which is likewise meaningless once installed, so drop it.  Drop its
# line from RECORD too: that line holds a hash of the file, and so of the
# build directory, which would keep RECORD varying between builds and would
# describe a file that is not installed.
PIP_NO_COMPILE = $(shell $(PYTHON3) -m pip install --help 2>&1 | grep -q -- --no-compile && echo --no-compile)

install-exec-local:
if HAVE_PYTHON3_PROBES
	$(PYTHON3) -m pip install $(shell readlink -f $(builddir))/py3build/*.whl \
		 $(if $(DESTDIR),--root $(DESTDIR)) --prefix $(prefix) \
		 --no-deps --no-index --no-build-isolation $(PIP_NO_COMPILE) \
		 --ignore-installed --verbose
	rm -f $(DESTDIR)$(prefix)/lib*/python*/site-packages/*.dist-info/direct_url.json
	sed -i '/direct_url\.json,/d' $(DESTDIR)$(prefix)/lib*/python*/site-packages/*.dist-info/RECORD
endif

clean-local:
	rm -rf py3build build dist HelperSDT.egg-info

endif
