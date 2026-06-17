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
install-exec-local:
if HAVE_PYTHON3_PROBES
	$(PYTHON3) -m pip install $(shell readlink -f $(builddir))/py3build/*.whl \
		 $(if $(DESTDIR),--root $(DESTDIR)) --prefix $(prefix) \
		 --no-deps --no-index --no-build-isolation \
		 --ignore-installed --verbose
endif

clean-local:
	rm -rf py3build build dist HelperSDT.egg-info

endif
