# Makefile.am --- automake input file

AUTOMAKE_OPTIONS = subdir-objects
NOTEBOOK_DIR = "$(DESTDIR)$(pkgdatadir)/interactive-notebook"
install-data-local:
	(cd "$(srcdir)"; for f in *.{sh,ipynb,md,txt,json}; \
		do $(INSTALL_DATA) -D -m 666 $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/$$f; done)
	(cd "$(srcdir)/isystemtap"; for f in *.{py,json}; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/isystemtap/$$f; done)
	(cd "$(srcdir)/codemirror"; \
		for f in jupyterlab-stap-highlighting/__init__.py lib/plugin.js lib/stap.js package.json; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/codemirror/$$f; done)
	(cd "$(srcdir)/tests"; for f in *; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/interactive-notebook/tests/$$f; done)
	@echo "STAP_VERSION    = '$(VERSION)'"               >  "constants.py"
	@echo "STAP_PKGDATADIR = '$(pkgdatadir)'"            >> "constants.py"
	@echo "STAP_PATH       = '$(DESTDIR)$(bindir)/stap'" >> "constants.py"
	$(INSTALL_DATA) -D constants.py $(NOTEBOOK_DIR)/isystemtap/constants.py
	chmod 777 $(DESTDIR)$(pkgdatadir)/interactive-notebook/{isystemtap,codemirror,tests,}

clean-local: uninstall-local

uninstall-local:
	rm -rf $(NOTEBOOK_DIR)