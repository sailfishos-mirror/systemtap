// Setup routines for creating fully populated DWFLs. Used in pass 2 and 3.
// Copyright (C) 2009-2018, 2026 Red Hat, Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "setupdwfl.h"

#include "dwarf_wrappers.h"
#include "dwflpp.h"
#include "session.h"
#include "staputil.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <mutex>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#include <iterator>
#include <cstring>
#include <cstdint>

extern "C" {
#include <fnmatch.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <byteswap.h>
#include <endian.h>
#include <inttypes.h>
#include <elfutils/libdwelf.h>
#if defined(HAVE_LIBZ)
#include <zlib.h>
#endif
}

#if defined(HAVE_LIBDEBUGINFOD)
#include <elfutils/debuginfod.h>
#endif

// Linux ELF note types found in vmlinux .notes (see include/linux/build-salt.h,
// include/linux/elfnote-lto.h).  Fedora stores the uts release in BUILD_SALT.
#ifndef LINUX_ELFNOTE_BUILD_SALT
#define LINUX_ELFNOTE_BUILD_SALT 0x100
#endif
#ifndef LINUX_ELFNOTE_LTO_INFO
#define LINUX_ELFNOTE_LTO_INFO 0x101
#endif

// XXX: also consider adding $HOME/.debug/ for perf build-id-cache
static const char *debuginfo_path_arr = "+:.debug:/usr/lib/debug:/var/cache/abrt-di/usr/lib/debug:build";
static const char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
static char *debuginfo_path = (char *)(debuginfo_env_arr ?: debuginfo_path_arr);

static const char *debuginfod_progress = getenv("DEBUGINFOD_PROGRESS");

// NB: kernel_build_tree doesn't enter into this, as it's for
// kernel-side modules only.
// XXX: also consider adding $HOME/.debug/ for perf build-id-cache
static const char *debuginfo_usr_path_arr = "+:.debug:/usr/lib/debug:/var/cache/abrt-di/usr/lib/debug";
static char *debuginfo_usr_path = (char *)(debuginfo_env_arr
					   ?: debuginfo_usr_path_arr);

// A pointer to the current systemtap session for use only by a few
// dwfl calls. DO NOT rely on this, as it is cleared after use.
// This is a kludge.
static systemtap_session* current_session_for_find_debuginfo;

// Serializes all setup_dwfl_* entry points: file-static search state,
// current_session_for_find_debuginfo, and libelf's one-time elf_version
// init via dwfl_begin.  Recursive for the download-retry re-enter.
// Dwfl reporting must stay single-threaded; after dwfl_report_end,
// concurrent read-only module use is gated on HAVE_ELFUTILS_THREAD_SAFETY.
static std::recursive_mutex setup_dwfl_mutex;

// Build-id bytes for the kernel, filled by setup_dwfl_kernel() before
// dwfl_linux_kernel_report_offline so stap_linux_kernel_find_elf can hand
// them to dwfl when /boot/vmlinuz is a non-ELF boot image (aarch64 zboot,
// s390x zipl).  Cleared again before setup_dwfl_kernel returns.
static std::vector<unsigned char> pending_kernel_build_id_bits;

static bool
parse_build_id_hex (const std::string &hex, std::vector<unsigned char> &bits)
{
  bits.clear ();
  if (hex.size () < 2 || (hex.size () % 2) != 0)
    return false;
  bits.reserve (hex.size () / 2);
  for (size_t i = 0; i + 1 < hex.size (); i += 2)
    {
      char tmp[3] = { hex[i], hex[i + 1], '\0' };
      char *end = NULL;
      unsigned long byte = strtoul (tmp, &end, 16);
      if (end != tmp + 2 || byte > 0xff)
        {
          bits.clear ();
          return false;
        }
      bits.push_back ((unsigned char) byte);
    }
  return !bits.empty ();
}

/* Wrap dwfl_linux_kernel_find_elf so a build-id-only kernel module (see
   ensure_kernel_build_id_module) can still be opened via debuginfod.
   Note: dwfl_linux_kernel_report_offline uses dwfl_report_elf directly for
   an on-disk vmlinuz, so this wrapper is reached via dwfl_module_getelf
   after we report a build-id-only placeholder — not during the offline
   path's first open attempt.  */
static int
stap_linux_kernel_find_elf (Dwfl_Module *mod, void **userdata,
                            const char *modname, Dwarf_Addr base,
                            char **file_name, Elf **elfp)
{
  if (mod != NULL && modname != NULL && !strcmp (modname, "kernel")
      && !pending_kernel_build_id_bits.empty ())
    {
      const unsigned char *existing = NULL;
      GElf_Addr vaddr = 0;
      if (dwfl_module_build_id (mod, &existing, &vaddr) <= 0)
        (void) dwfl_module_report_build_id (mod,
                                            pending_kernel_build_id_bits.data (),
                                            pending_kernel_build_id_bits.size (),
                                            0);
      if (dwfl_module_build_id (mod, &existing, &vaddr) > 0)
        {
          int fd = dwfl_build_id_find_elf (mod, userdata, modname, base,
                                           file_name, elfp);
          if (fd >= 0)
            return fd;
        }
    }
  return dwfl_linux_kernel_find_elf (mod, userdata, modname, base,
                                     file_name, elfp);
}

struct find_kernel_mod_arg
{
  Dwfl_Module *mod;
};

static int
find_kernel_mod_cb (Dwfl_Module *mod, void **, const char *name,
                    Dwarf_Addr, void *arg)
{
  if (name != NULL && !strcmp (name, "kernel"))
    {
      ((find_kernel_mod_arg *) arg)->mod = mod;
      return DWARF_CB_ABORT;
    }
  return DWARF_CB_OK;
}

/* Try well-known .build-id cache locations for a full vmlinux ELF.  */
static bool
kernel_build_id_local_path (const std::vector<unsigned char> &bits,
                            std::string &path_out)
{
  if (bits.size () < 2)
    return false;
  static const char *const dirs[] = {
    "/usr/lib/debug/.build-id/",
    "/var/cache/abrt-di/usr/lib/debug/.build-id/",
    NULL
  };
  std::string id;
  id.reserve (bits.size () * 2);
  for (size_t i = 0; i < bits.size (); i++)
    {
      char byte[3];
      sprintf (byte, "%02x", bits[i]);
      id.append (byte);
    }
  std::string leaf = id.substr (0, 2) + "/" + id.substr (2);
  for (int d = 0; dirs[d] != NULL; d++)
    {
      std::string p = std::string (dirs[d]) + leaf;
      if (access (p.c_str (), R_OK) == 0)
        {
          path_out = p;
          return true;
        }
      std::string pd = p + ".debug";
      if (access (pd.c_str (), R_OK) == 0)
        {
          path_out = pd;
          return true;
        }
    }
  return false;
}

/* After dwfl_linux_kernel_report_offline: on aarch64/s390x the installed
   vmlinuz is not ELF, so the offline reporter never creates a kernel
   module.  Fetch the matching full vmlinux (ELF+DWARF) via local
   .build-id cache or debuginfod, then dwfl_report_offline so libdwfl
   sees proper address ranges and file names — not a [0,0] build-id
   placeholder (which leaves mainfile null and breaks relocation).  */
static Dwfl_Module *
ensure_kernel_build_id_module (Dwfl *dwfl, int verbose)
{
  if (pending_kernel_build_id_bits.empty ())
    return NULL;

  find_kernel_mod_arg a = { NULL };
  (void) dwfl_getmodules (dwfl, &find_kernel_mod_cb, &a, 0);

  // Already reported (e.g. ELF vmlinuz on x86).  Optionally attach the
  // staged build-id if the module somehow lacks one.
  if (a.mod != NULL)
    {
      const unsigned char *existing = NULL;
      GElf_Addr vaddr = 0;
      if (dwfl_module_build_id (a.mod, &existing, &vaddr) <= 0)
        {
          if (dwfl_module_report_build_id (a.mod,
                                           pending_kernel_build_id_bits.data (),
                                           pending_kernel_build_id_bits.size (),
                                           0) != 0)
            return NULL;
          if (verbose > 1)
            std::clog << _("Attached kernel build ID to dwfl module for debuginfod")
                      << std::endl;
        }
      return a.mod;
    }

  std::string local;
  char *dbg_path = NULL;
  int fd = -1;
  const char *report_path = NULL;

  if (kernel_build_id_local_path (pending_kernel_build_id_bits, local))
    {
      report_path = local.c_str ();
      fd = -1; // report_offline will open
    }
#if defined(HAVE_LIBDEBUGINFOD)
  else
    {
      debuginfod_client *c = dwfl_get_debuginfod_client (dwfl);
      if (c != NULL)
        {
          // Prefer debuginfo: Fedora indexes the fat unstripped vmlinux
          // under both names, and that is what we need for ELF+DWARF.
          fd = debuginfod_find_debuginfo (c,
                                          pending_kernel_build_id_bits.data (),
                                          pending_kernel_build_id_bits.size (),
                                          &dbg_path);
          if (fd < 0)
            fd = debuginfod_find_executable (c,
                                             pending_kernel_build_id_bits.data (),
                                             pending_kernel_build_id_bits.size (),
                                             &dbg_path);
          if (fd >= 0)
            report_path = dbg_path;
        }
    }
#endif

  if (report_path == NULL)
    {
      if (verbose > 1)
        std::clog << _("No kernel ELF found via build-id cache/debuginfod for non-ELF vmlinuz")
                  << std::endl;
      return NULL;
    }

  a.mod = dwfl_report_offline (dwfl, "kernel", report_path, fd);
  if (a.mod == NULL)
    {
      if (fd >= 0)
        close (fd);
      if (verbose > 1)
        std::clog << _F("dwfl_report_offline(kernel, %s) failed: %s",
                        report_path, dwfl_errmsg (-1) ?: "?")
                  << std::endl;
    }
  else if (verbose > 1)
    std::clog << _F("Reported kernel ELF+DWARF from %s", report_path)
              << std::endl;

  free (dbg_path);
  return a.mod;
}

static const Dwfl_Callbacks kernel_callbacks =
  {
    stap_linux_kernel_find_elf,
    internal_find_debuginfo,
    dwfl_offline_section_address,
    (char **) & debuginfo_path
  };

static const Dwfl_Callbacks user_callbacks =
  {
    NULL,
    internal_find_debuginfo,
    NULL, /* ET_REL not supported for user space, only ET_EXEC and ET_DYN.
	     dwfl_offline_section_address, */
    (char **) & debuginfo_usr_path
  };

using namespace std;

// Setup in setup_dwfl_kernel(), for use in setup_dwfl_report_kernel_p().
// Either offline_search_modname or offline_search_names is
// used. When offline_search_modname is not NULL then
// offline_search_names is ignored.
static const char *offline_search_modname;
static set<string> offline_search_names;
static unsigned offline_modules_found;

// Whether or not we are done reporting kernel modules in
// set_dwfl_report_kernel_p().
static bool setup_dwfl_done;

// Determines whether or not we will make setup_dwfl_report_kernel_p
// report true for all module dependencies. This is necessary for
// correctly resolving some dwarf constructs that relocate against
// symbols in vmlinux and/or other modules they depend on. See PR10678.
static const bool setup_all_deps = true;

// Where to find the kernel (and the Modules.dep file).  Setup in
// setup_dwfl_kernel(), used by dwfl_linux_kernel_report_offline() and
// setup_mod_deps().
static string elfutils_kernel_path;

static bool is_comma_dash(const char c) { return (c == ',' || c == '-'); }

// The path to the abrt-action-install-debuginfo-to-abrt-cache program.
static const string abrt_path =
                    (access ("/usr/bin/abrt-action-install-debuginfo-to-abrt-cache", X_OK) == 0
                      ? "/usr/bin/abrt-action-install-debuginfo-to-abrt-cache"
                    : (access ("/usr/libexec/abrt-action-install-debuginfo-to-abrt-cache", X_OK) == 0
                      ? "/usr/libexec/abrt-action-install-debuginfo-to-abrt-cache"
                    : ""));

// The module name is the basename (without the extension) of the module path,
// with ',' and '-' replaced by '_'. This is a (more or less safe) heuristic:
// the actual name by which the module is known once inside the kernel is not
// derived from the path, but from the .gnu.linkonce.this_module section of the
// KO. In practice, modules in /lib/modules/ respect this convention, and we
// require it as well for out-of-tree kernel modules.
string
modname_from_path(const string &path)
{
  size_t slash = path.rfind('/');
  if (slash == string::npos)
    return "";
  string name = path.substr(slash + 1);

  // First look for .ko extension variants like ".ko" or ".ko.xz"
  // If that fails, look for any ".*" extension at all.
  size_t extension = name.rfind(".ko");
  if (extension == string::npos)
    extension = name.rfind('.');
  if (extension == string::npos)
    return "";

  name.erase(extension);
  replace_if(name.begin(), name.end(), is_comma_dash, '_');
  return name;
}

static bool offline_search_names_find(const string &modpath) {
	if (offline_search_names.find(modpath) != offline_search_names.end()) return 1;
	string modname = modname_from_path (modpath);
	return offline_search_names.find(modname) != offline_search_names.end();
}

// Try to parse modules.dep file,
// Simple format: module path (either full or relative), colon,
// (possibly empty) space delimited list of module (path)
// dependencies.
static void
setup_mod_deps()
{
  string modulesdep;
  string kernel_path;
  ifstream in;
  string l;

  if (elfutils_kernel_path[0] == '/')
    {
      kernel_path = elfutils_kernel_path;
    }
  else
    {
      string sysroot = "";
      if (current_session_for_find_debuginfo)
        sysroot = current_session_for_find_debuginfo->sysroot;
      kernel_path = sysroot + "/lib/modules/" + elfutils_kernel_path;
    }
  modulesdep = kernel_path + "/modules.dep";
  in.open(modulesdep.c_str());
  if (in.fail ())
    return;

  while (getline (in, l))
    {
      size_t off = l.find (':');
      if (off != string::npos)
	{
	  string modpath, modname;
	  modpath = l.substr (0, off);
	  modname = modname_from_path (modpath);
	  if (modname == "")
	    continue;
	  if (modpath[0] != '/') modpath = kernel_path + "/" + modpath;

	  bool dep_needed = 0;
	  if (offline_search_modname != NULL)
	    {
	      if (dwflpp::name_has_wildcard (offline_search_modname))
		{
		  dep_needed = !fnmatch (offline_search_modname,
					 modname.c_str (), 0);
		  if (dep_needed)
		    offline_search_names.insert (modpath);
		}
	      else
		{
		  dep_needed = ! strcmp(modname.c_str (),
					offline_search_modname);
		  if (dep_needed)
		    offline_search_names.insert (modpath);
		}
	    }
	  else if (offline_search_names.find(modpath) != offline_search_names.end())
	    dep_needed = 1;
	  else 
	    {
		set<string>::iterator it = offline_search_names.begin();
		while (it != offline_search_names.end())
		  {
		    string modname;
		    modname = modname_from_path(modpath);
		    if (*it == modname)
		      {
			dep_needed = 1;
			offline_search_names.erase(it);
			offline_search_names.insert(modpath);
			break;
		      }
		    it++;
		  }
	    }

	  if (! dep_needed)
	    continue;

	  string depstring = l.substr (off + 1);
	  if (depstring.size () > 0)
	    {
	      stringstream ss (depstring);
	      string deppath;
	      while (ss >> deppath)
		offline_search_names.insert (deppath);

	    }
	}
    }

  // We always want kernel (needed in list so size checks match).
  // Everything needed now stored in offline_search_names.
  offline_search_names.insert ("kernel");
  offline_search_modname = NULL;
}

// Set up our offline search for kernel modules.  We don't want the
// offline search iteration to do a complete search of the kernel
// build tree, since that's wasteful, so create a predicate that
// filters and stops reporting as soon as we got everything.
static int
setup_dwfl_report_kernel_p(const char* modname, const char* filename)
{
  assert_no_interrupts();

  if (setup_dwfl_done)
    return -1;

  assert (current_session_for_find_debuginfo);
  if (current_session_for_find_debuginfo->verbose > 4)
    clog << _F("checking pattern '%s' vs. module '%s' file '%s'\n",
               offline_search_modname ?: "",
               modname ?: "",
               filename ?: "");

  // elfutils sends us NULL filenames sometimes if it can't find dwarf
  if (filename == NULL)
    return 0;

  // Check kernel first since it is often the only thing needed,
  // then we never have to parse and setup the module deps map.
  // It will be reported as the very first thing.
  if (setup_all_deps && ! strcmp (modname, "kernel"))
    {
      if ((offline_search_modname != NULL
	   && ! strcmp (offline_search_modname, "kernel"))
	  || (offline_search_names.size() == 1
	      && *offline_search_names.begin() == "kernel"))
	setup_dwfl_done = true;
      else
	setup_mod_deps();

      offline_modules_found++;
      return 1;
    }

  // If offline_search_modname is setup use it (either as regexp or
  // explicit module/kernel name) and ignore offline_search_names.
  // Otherwise use offline_search_names exclusively.
  if (offline_search_modname != NULL)
    {
      if (dwflpp::name_has_wildcard (offline_search_modname))
	{
          // XXX: see also dwflpp::module_name_matches()
	  int match_p = !fnmatch(offline_search_modname, modname, 0);
	  // In the wildcard case, we don't short-circuit (return -1)
	  // analogously to dwflpp::module_name_final_match().
	  if (match_p)
	    offline_modules_found++;
	  return match_p;
	}
      else
	{ /* non-wildcard mode, reject mismatching module names */
	  if (strcmp(modname, offline_search_modname))
	    return 0;
	  else
	    {
	      // Done, only one name needed and found it.
	      offline_modules_found++;
	      setup_dwfl_done = true;
	      return 1;
	    }
	}
    }
  else
    { /* find all in set mode, reject mismatching module names */
      if (!offline_search_names_find(filename))
	return 0;
      else
	{
	  offline_modules_found++;
	  if (offline_search_names.size() == offline_modules_found)
	    setup_dwfl_done = true;
	  return 1;
	}
    }
}

static char * path_insert_sysroot(string sysroot, string path)
{
  char * path_new;
  size_t pos = 1;
  if (path[0] == '/')
    path.replace(0, 1, sysroot);
  while (true) {
    pos = path.find(":/", pos);
    if (pos == string::npos)
      break;
    path.replace(pos, 2, ":" + sysroot);
    ++pos;
  }
  path_new = new char[path.size()+1];
  strcpy (path_new, path.c_str());
  return path_new;
}


void debuginfo_path_insert_sysroot(string sysroot)
{
  // FIXME: This is a short-term fix, until we expect sysroot paths to
  // always end with a '/' (and never be empty).
  //
  // The path_insert_sysroot() function assumes that sysroot has a '/'
  // on the end. Make sure that is true.
  if (! sysroot.empty() && *(sysroot.end() - 1) != '/')
    sysroot.append(1, '/');
  debuginfo_path = path_insert_sysroot(sysroot, debuginfo_path);
  debuginfo_usr_path = path_insert_sysroot(sysroot, debuginfo_usr_path);
}


#if defined(HAVE_LIBDEBUGINFOD)
static
int
debuginfod_progressfn (debuginfod_client *c,
                       long a, long b)
{
  // PR31368: Cancel the download in case Ctrl-C is hit.
  if (pending_interrupts > 0) {
    fprintf(stderr, "\nInterrupt received, exiting.\n");
    return 1;
  }

  // Skip showing unsolicited information.
  if (debuginfod_progress == NULL
      || strcmp(debuginfod_progress, "0") == 0)
    return 0;

  // Model after debuginfod-client.c's default_progressfn()
  // If it was a public function, we could reuse, but alas.
  const char* url = debuginfod_get_url (c);
  int len = 0;

  /* We prefer to print the host part of the URL to keep the
     message short. */
  if (url != NULL)
    {
      const char* buildid = strstr(url, "buildid/");
      if (buildid != NULL)
        len = (buildid - url);
      else
        len = strlen(url);
    }

  if (b == 0 || url==NULL) /* early stage */
    dprintf(STDERR_FILENO,
            "\rDownloading %c", "-/|\\"[a % 4]);
  else if (b < 0) /* download in progress but unknown total length */
    dprintf(STDERR_FILENO,
            "\rDownloading from %.*s %ld",
            len, url, a);
  else /* download in progress, and known total length */
    dprintf(STDERR_FILENO,
            "\rDownloading from %.*s %ld/%ld",
            len, url, a, b);

  return 0;
}

void
setup_debuginfod_progress(Dwfl *dwfl)
{
  debuginfod_client *c = dwfl_get_debuginfod_client (dwfl);
  if (c != NULL)
    debuginfod_set_progressfn (c, debuginfod_progressfn);
}
#endif


static Dwfl *
setup_dwfl_kernel (unsigned *modules_found, systemtap_session &s)
{
  Dwfl *dwfl = dwfl_begin (&kernel_callbacks);
  DWFL_ASSERT ("dwfl_begin", dwfl);
  dwfl_report_begin (dwfl);

#if defined(HAVE_LIBDEBUGINFOD)
  setup_debuginfod_progress(dwfl);
#endif
  
  // We have a problem with -r REVISION vs -r BUILDDIR here.  If
  // we're running against a fedora/rhel style kernel-debuginfo
  // tree, s.kernel_build_tree is not the place where the unstripped
  // vmlinux will be installed.  Rather, it's over yonder at
  // /usr/lib/debug/lib/modules/$REVISION/.  It seems that there is
  // no way to set the dwfl_callback.debuginfo_path and always
  // passs the plain kernel_release here.  So instead we have to
  // hard-code this magic here.
  string lib_path = s.sysroot + "/lib/modules/" + s.kernel_release + "/build";
  if (s.kernel_build_tree == lib_path)
    {
      if (s.sysroot != "")
        // If we have sysroot set does not make sense to pass
        // short release to dwfl, it won't take a sysroot into
        // account. Let's construct full path in such case.
	elfutils_kernel_path = string(s.sysroot + "/lib/modules/" + s.kernel_release);
      else
	elfutils_kernel_path = s.kernel_release;
    }
  else
    elfutils_kernel_path = s.kernel_build_tree;
  offline_modules_found = 0;

  // First try to report full path modules.
  set<string>::iterator it = offline_search_names.begin();
  int kernel = 0;
  while (it != offline_search_names.end())
    {
      if ((*it)[0] == '/')
        {
          const char *cname = (*it).c_str();
          Dwfl_Module *mod = dwfl_report_offline (dwfl, cname, cname, -1);
          if (mod)
            offline_modules_found++;
        }
      else if ((*it) == "kernel")
        kernel = 1;
      it++;
    }
  if (offline_search_modname != NULL
      && !strcmp (offline_search_modname, "kernel"))
    kernel = 1;

  // Extract the kernel build-id *before* reporting modules so
  // stap_linux_kernel_find_elf can publish it to dwfl for debuginfod when
  // the installed vmlinuz is not ELF (PR34488).  Cheap when vmlinux.id
  // exists; setup_all_deps often reports the kernel even for .ko queries.
  string hex = get_kernel_build_id (s);
  pending_kernel_build_id_bits.clear ();
  if (!hex.empty () && parse_build_id_hex (hex, pending_kernel_build_id_bits)
      && s.verbose > 1)
    clog << _F("Kernel build ID %s staged for dwfl/debuginfod",
               hex.c_str ()) << endl;

    // We always need this, even when offline_search_modname is NULL
    // and offline_search_names is empty because we still might want
    // the kernel vmlinux reported.
  setup_dwfl_done = false;
  int rc = dwfl_linux_kernel_report_offline (dwfl,
                                             elfutils_kernel_path.c_str(),
					     &setup_dwfl_report_kernel_p);

  (void) rc; /* Ignore since the predicate probably returned -1 at some point,
                And libdwfl interprets that as "whole query failed" rather than
                "found it already, stop looking". */

  // NB: the result of an _offline call is the assignment of
  // virtualized addresses to relocatable objects such as
  // modules.  These have to be converted to real addresses at
  // run time.  See the dwarf_derived_probe ctor and its caller.

  // Non-ELF vmlinuz: report_elf failed but the predicate already ran.
  // Publish a build-id-only kernel module before closing the report.
  // Only when the caller asked for "kernel" — don't inject a placeholder
  // into pure .ko queries.
  Dwfl_Module *kmod = NULL;
  if (kernel)
    {
      find_kernel_mod_arg before = { NULL };
      (void) dwfl_getmodules (dwfl, &find_kernel_mod_cb, &before, 0);
      kmod = ensure_kernel_build_id_module (dwfl, s.verbose);
      // Predicate may have counted a match even when report_elf failed;
      // only bump the counter when we newly created the placeholder.
      if (kmod != NULL && before.mod == NULL)
        offline_modules_found++;
    }

  // If no modules were found, and we are probing the kernel,
  // attempt to download the kernel debuginfo via abrt.
  if (kernel
      && offline_modules_found == 0 && s.download_dbinfo != 0 && !hex.empty())
    {
      pending_kernel_build_id_bits.clear ();
      rc = download_kernel_debuginfo(s, hex);
      if(rc >= 0)
        {
          dwfl_end (dwfl);
          return setup_dwfl_kernel (modules_found, s);
        }
    }

  DWFL_ASSERT ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));

  // For the offline-reported kernel (local .build-id or debuginfod),
  // open ELF+DWARF now so failures surface early.  Keep
  // pending_kernel_build_id_bits until after getelf for the find_elf
  // fallback used when report_offline was not needed / not used.
  if (kmod != NULL)
    {
      Dwarf_Addr bias = 0;
      Elf *elf = dwfl_module_getelf (kmod, &bias);
      Dwarf *dw = dwfl_module_getdwarf (kmod, &bias);
      if (s.verbose > 1)
        {
          const char *mainf = NULL;
          const char *dbgf = NULL;
          Dwarf_Addr start = 0, end = 0;
          (void) dwfl_module_info (kmod, NULL, &start, &end, NULL, NULL,
                                   &mainf, &dbgf);
          if (elf != NULL && dw != NULL)
            // NB: this function also has a local string named hex (build-id).
            clog << _("Opened kernel ELF+DWARF ")
                 << "[" << std::hex << std::showbase << start << "-" << end
                 << std::noshowbase << std::dec << "] "
                 << (mainf ?: dbgf ?: "?") << endl;
          else
            clog << _F("Kernel ELF/DWARF still missing after build-id lookup: %s",
                       dwfl_errmsg (-1) ?: "?") << endl;
        }
    }
  pending_kernel_build_id_bits.clear ();

  *modules_found = offline_modules_found;

  return dwfl;
}

Dwfl*
setup_dwfl_kernel(const std::string &name,
		  unsigned *found,
		  systemtap_session &s)
{
  lock_guard<recursive_mutex> g (setup_dwfl_mutex);
  current_session_for_find_debuginfo = &s;
  const char *modname = name.c_str();
  set<string> names; // Default to empty

  /* Support full path kernel modules, these cannot be regular
     expressions, so just put them in the search set. */
  if (name[0] == '/' || ! dwflpp::name_has_wildcard (modname))
    {
      names.insert(name);
      modname = NULL;
    }

  offline_search_modname = modname;
  offline_search_names = names;

  return setup_dwfl_kernel(found, s);
}

Dwfl*
setup_dwfl_kernel(const std::set<std::string> &names,
		  unsigned *found,
		  systemtap_session &s)
{
  lock_guard<recursive_mutex> g (setup_dwfl_mutex);
  current_session_for_find_debuginfo = &s;

  offline_search_modname = NULL;
  offline_search_names = names;
  return setup_dwfl_kernel(found, s);
}

Dwfl*
setup_dwfl_user(const std::string &name)
{
  lock_guard<recursive_mutex> g (setup_dwfl_mutex);
  Dwfl *dwfl = dwfl_begin (&user_callbacks);
  DWFL_ASSERT("dwfl_begin", dwfl);
  dwfl_report_begin (dwfl);

#if defined(HAVE_LIBDEBUGINFOD)
  setup_debuginfod_progress(dwfl);
#endif
  
  // XXX: should support buildid-based naming
  const char *cname = name.c_str();
  Dwfl_Module *mod = dwfl_report_offline (dwfl, cname, cname, -1);
  DWFL_ASSERT ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
  if (! mod)
    {
      dwfl_end(dwfl);
      dwfl = NULL;
    }

  return dwfl;
}

Dwfl*
setup_dwfl_user(std::vector<std::string>::const_iterator &begin,
		const std::vector<std::string>::const_iterator &end,
		bool all_needed, systemtap_session &s)
{
  lock_guard<recursive_mutex> g (setup_dwfl_mutex);
  current_session_for_find_debuginfo = &s;
  // See if we have this dwfl already cached
  set<string> modset(begin, end);

  Dwfl *dwfl = dwfl_begin (&user_callbacks);
  DWFL_ASSERT("dwfl_begin", dwfl);
  dwfl_report_begin (dwfl);

#if defined(HAVE_LIBDEBUGINFOD)
  setup_debuginfod_progress(dwfl);
#endif

  Dwfl_Module *mod = NULL;
  // XXX: should support buildid-based naming
  while (begin != end && dwfl != NULL)
    {
      const char *cname = (*begin).c_str();
      mod = dwfl_report_offline (dwfl, cname, cname, -1);
      if (! mod && all_needed)
	{
	  dwfl_end(dwfl);
	  dwfl = NULL;
	}
      begin++;
    }

  /* Extract the build id and add it to the session variable
   * so it will be added to the script hash */
  if (mod)
    {
      const unsigned char *bits;
      GElf_Addr vaddr;
      if(s.verbose > 2)
        clog << _("Extracting build ID.") << endl;
      int bits_length = dwfl_module_build_id(mod, &bits, &vaddr);

      /* Convert the binary bits to a hex string */
      string hex = hex_dump(bits, bits_length);

      //Store the build ID in the session
      lock_guard<recursive_mutex> gl (s.session_data_mutex);
      s.build_ids.push_back(hex);
    }

  if (dwfl)
    DWFL_ASSERT ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));

  return dwfl;
}

bool
is_user_module(const std::string &m)
{
  return m.find('/') != std::string::npos &&
         !(endswith(m, ".ko") ||
           endswith(m, ".ko.gz") ||
           endswith(m, ".ko.bz2") ||
           endswith(m, ".ko.xz") ||
           endswith(m, ".ko.zst"));
}

int
internal_find_debuginfo (Dwfl_Module *mod,
      void **userdata __attribute__ ((unused)),
      const char *modname __attribute__ ((unused)),
      GElf_Addr base __attribute__ ((unused)),
      const char *file_name,
      const char *debuglink_file,
      GElf_Word debuglink_crc,
      char **debuginfo_file_name)
{

  int bits_length;
  string hex;

  /* To Keep track of whether the abrt successfully installed the debuginfo */
  static int install_dbinfo_failed = 0;

  /* Make sure the current session variable is not null */
  if(current_session_for_find_debuginfo == NULL)
    goto call_dwfl_standard_find_debuginfo;

  /* Check to see if download-debuginfo=0 was set */
  if(!current_session_for_find_debuginfo->download_dbinfo || abrt_path.empty())
    goto call_dwfl_standard_find_debuginfo;

  {
    /* The symbol-dump worker threads (translate.cxx:emit_symbol_data) can
       reach this path concurrently.  */
    static std::mutex download_mutex;

    /* Check that we haven't already run this */
    {
      std::lock_guard<std::mutex> g (download_mutex);
      if (install_dbinfo_failed < 0)
        {
          if(current_session_for_find_debuginfo->verbose > 1)
            current_session_for_find_debuginfo->print_warning(_F("We already tried running '%s'", abrt_path.c_str()));
          goto call_dwfl_standard_find_debuginfo;
        }
    }

    /* Extract the build ID.  */
    const unsigned char *bits;
    GElf_Addr vaddr;
    if(current_session_for_find_debuginfo->verbose > 2)
      clog << _("Extracting build ID.") << endl;
    bits_length = dwfl_module_build_id(mod, &bits, &vaddr);

    /* Convert the binary bits to a hex string */
    hex = hex_dump(bits, bits_length);

    /* Search for the debuginfo with the build ID */
    if(current_session_for_find_debuginfo->verbose > 2)
      clog << _F("Searching for debuginfo with build ID: '%s'.", hex.c_str()) << endl;
    if (bits_length > 0)
      {
        int fd = dwfl_build_id_find_debuginfo(mod,
               NULL, NULL, 0,
               NULL, NULL, 0,
               debuginfo_file_name);
        if (fd >= 0)
          return fd;
      }

    {
      /* The above failed, so call abrt-action-install-debuginfo-to-abrt-cache
         to download and install the debuginfo.  Re-check the flag under the
         lock in case another thread failed while we were looking up the
         build ID above.  */
      std::lock_guard<std::mutex> g (download_mutex);

      if (install_dbinfo_failed < 0)
        goto call_dwfl_standard_find_debuginfo;

      if(current_session_for_find_debuginfo->verbose > 1)
        clog << _F("Downloading and installing debuginfo with build ID: '%s' using %s.",
                hex.c_str(), abrt_path.c_str()) << endl;

      struct tms tms_before;
      times (& tms_before);
      struct timeval tv_before;
      struct tms tms_after;
      unsigned _sc_clk_tck;
      struct timeval tv_after;
      gettimeofday (&tv_before, NULL);

      if(execute_abrt_action_install_debuginfo_to_abrt_cache (hex) < 0)
        {
          install_dbinfo_failed = -1;
          current_session_for_find_debuginfo->print_warning(_F("%s failed.", abrt_path.c_str()));
          goto call_dwfl_standard_find_debuginfo;
        }

      _sc_clk_tck = sysconf (_SC_CLK_TCK);
      times (& tms_after);
      gettimeofday (&tv_after, NULL);
      if(current_session_for_find_debuginfo->verbose > 1)
        clog << _("Download completed in ")
                  << ((tms_after.tms_cutime + tms_after.tms_utime
                  - tms_before.tms_cutime - tms_before.tms_utime) * 1000 / (_sc_clk_tck)) << "usr/"
                  << ((tms_after.tms_cstime + tms_after.tms_stime
                  - tms_before.tms_cstime - tms_before.tms_stime) * 1000 / (_sc_clk_tck)) << "sys/"
                  << ((tv_after.tv_sec - tv_before.tv_sec) * 1000 +
                  ((long)tv_after.tv_usec - (long)tv_before.tv_usec) / 1000) << "real ms"<< endl;
    }
  }

  call_dwfl_standard_find_debuginfo:

  /* Call the original dwfl_standard_find_debuginfo */
  return dwfl_standard_find_debuginfo(mod, userdata, modname, base,
              file_name, debuglink_file,
              debuglink_crc, debuginfo_file_name);

}

int
execute_abrt_action_install_debuginfo_to_abrt_cache (string hex)
{
  /* Be sure that abrt exists */
  if (abrt_path.empty())
    return -1;

  int timeout = current_session_for_find_debuginfo->download_dbinfo;;
  vector<string> cmd;
  cmd.push_back ("/bin/sh");
  cmd.push_back ("-c");
  
  /* NOTE: abrt does not currently work with asking for confirmation
   * in version abrt-2.0.3-1.fc15.x86_64, Bugzilla: BZ726192 */
  if(current_session_for_find_debuginfo->download_dbinfo == -1)
    {
      cmd.push_back ("echo " + hex + " | " + abrt_path + " --ids=-");
      timeout = INT_MAX; 
      current_session_for_find_debuginfo->print_warning(_("Due to bug in abrt, it may continue downloading anyway without asking for confirmation."));
    }
  else
    cmd.push_back ("echo " + hex + " | " + abrt_path + " -y --ids=-");
 
  /* NOTE: abrt does not allow canceling the download process at the moment
   * in version abrt-2.0.3-1.fc15.x86_64, Bugzilla: BZ730107 */
  if(timeout != INT_MAX)
    current_session_for_find_debuginfo->print_warning(_("Due to a bug in abrt, it  may continue downloading after stopping stap if download times out."));
  
  int pid;
  if(current_session_for_find_debuginfo->verbose > 1 ||  current_session_for_find_debuginfo->download_dbinfo == -1)
    /* Execute abrt-action-install-debuginfo-to-abrt-cache, 
     * showing output from abrt */
    pid = stap_spawn(current_session_for_find_debuginfo->verbose, cmd, NULL);
  else
    {
      /* Execute abrt-action-install-debuginfo-to-abrt-cache,
       * without showing output from abrt */
      posix_spawn_file_actions_t fa;
      if (posix_spawn_file_actions_init(&fa) != 0)
        return -1;
      if(posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0) != 0)
        {
          posix_spawn_file_actions_destroy(&fa);
          return -1;
        }
      pid = stap_spawn(current_session_for_find_debuginfo->verbose, cmd, &fa);
      posix_spawn_file_actions_destroy(&fa);
    }

  /* Check to see if either the program successfully completed, or if it timed out. */
  int rstatus = 0;
  int timer = 0;
  int rc = 0;
  while(timer < timeout)
    {
      sleep(1); 
      rc = waitpid(pid, &rstatus, WNOHANG);
      if(rc < 0)
        return -1;
      if (rc > 0 && WIFEXITED(rstatus)) 
        break;
      assert_no_interrupts();
      timer++;
    }
  if(timer == timeout)
    {
      /* Timed out! */
      kill(-pid, SIGINT);
      current_session_for_find_debuginfo->print_warning(_("Aborted downloading debuginfo: timed out."));
      return -1;
    }

  /* Successfully finished downloading! */
  #if 0 // Should not print this until BZ733690 is fixed as abrt could fail to download
        // and it would still print success.
  if(current_session_for_find_debuginfo->verbose > 1 || current_session_for_find_debuginfo->download_dbinfo == -1)
     clog << _("Download Completed Successfully!") << endl;
  #endif
  if(current_session_for_find_debuginfo->verbose > 1 || current_session_for_find_debuginfo->download_dbinfo == -1)
    clog << _("ABRT finished attempting to download debuginfo.") << endl;

  return 0;
}

/* Read an entire file into BUF.  Returns false on any error.  */
static bool
read_whole_file (const string &path, vector<unsigned char> &buf)
{
  ifstream f (path.c_str (), ios::binary);
  if (!f)
    return false;
  buf.assign (istreambuf_iterator<char> (f), istreambuf_iterator<char> ());
  // Note: draining via istreambuf_iterator does not reliably set eofbit,
  // so don't require f.eof() here — only reject hard stream errors.
  return !f.bad () && !buf.empty ();
}

/* Locate a vmlinuz image for the session's kernel release / sysroot.  */
static string
find_vmlinuz_path (systemtap_session &s)
{
  vector<string> candidates;
  candidates.push_back (s.sysroot + "/lib/modules/" + s.kernel_release
                        + "/vmlinuz");
  candidates.push_back (s.sysroot + "/boot/vmlinuz-" + s.kernel_release);
  // Some layouts keep a plain "vmlinuz" next to the build tree.
  if (!s.kernel_build_tree.empty ())
    {
      string bt = s.kernel_build_tree;
      // .../lib/modules/REL/build -> .../lib/modules/REL/vmlinuz
      if (endswith (bt, "/build"))
        candidates.push_back (bt.substr (0, bt.size () - 5) + "vmlinuz");
      candidates.push_back (bt + "/vmlinuz");
    }

  for (size_t i = 0; i < candidates.size (); i++)
    if (access (candidates[i].c_str (), R_OK) == 0)
      return candidates[i];
  return "";
}

/* Try dwelf_elf_gnu_build_id on an ELF vmlinuz (works on ppc64le).  */
static string
build_id_from_elf_vmlinuz (const string &path, int verbose)
{
  int fd = open (path.c_str (), O_RDONLY);
  if (fd < 0)
    return "";

  Elf *elf = elf_begin (fd, ELF_C_READ_MMAP_PRIVATE, NULL);
  if (elf == NULL)
    {
      close (fd);
      return "";
    }

  const void *build_id = NULL;
  ssize_t len = dwelf_elf_gnu_build_id (elf, &build_id);
  string hex;
  if (len > 0 && build_id != NULL)
    {
      hex = hex_dump ((const unsigned char *) build_id, (size_t) len);
      if (verbose > 1)
        clog << _F("Extracted kernel build ID %s from ELF notes in %s",
                   hex.c_str (), path.c_str ()) << endl;
    }

  elf_end (elf);
  close (fd);
  return hex;
}

#if defined(HAVE_LIBZ)
/* Decompress a gzip member; trailing garbage after the stream is OK
   (s390x zipl images append a " zIPL" marker).  */
static bool
gzip_decompress (const unsigned char *src, size_t src_len,
                 vector<unsigned char> &dst)
{
  z_stream strm;
  memset (&strm, 0, sizeof strm);
  if (inflateInit2 (&strm, 16 + MAX_WBITS) != Z_OK)
    return false;

  dst.clear ();
  strm.next_in = const_cast<Bytef *> (src);
  strm.avail_in = src_len;

  int ret = Z_OK;
  while (ret == Z_OK)
    {
      size_t have = dst.size ();
      dst.resize (have + (1u << 20));
      strm.next_out = dst.data () + have;
      strm.avail_out = dst.size () - have;
      ret = inflate (&strm, Z_NO_FLUSH);
      dst.resize (have + ((dst.size () - have) - strm.avail_out));
      if (ret == Z_STREAM_END)
        break;
      if (ret != Z_OK)
        {
          inflateEnd (&strm);
          dst.clear ();
          return false;
        }
    }

  inflateEnd (&strm);
  return ret == Z_STREAM_END && !dst.empty ();
}

/* Peel an aarch64 EFI zboot PE image down to the raw ARM64 Image, or
   an s390x zipl image down to vmlinux.bin.  Returns false if the file
   does not look like either (caller may still scan the raw bytes).  */
static bool
peel_boot_image (const vector<unsigned char> &in,
                 vector<unsigned char> &out, string &kind, int verbose)
{
  // EFI zboot: MZ + "zimg" + little-endian payload_{offset,size} +
  // compression method string at +0x18 (see drivers/firmware/efi/libstub).
  if (in.size () >= 0x38
      && in[0] == 'M' && in[1] == 'Z'
      && in[4] == 'z' && in[5] == 'i' && in[6] == 'm' && in[7] == 'g')
    {
      uint32_t payload_off, payload_size;
      memcpy (&payload_off, &in[8], 4);
      memcpy (&payload_size, &in[12], 4);
      string comp;
      for (size_t i = 0x18; i < 0x38 && in[i]; i++)
        comp.push_back ((char) in[i]);

      if (verbose > 2)
        clog << _F("vmlinuz looks like EFI zboot (%s payload at %#x size %#x)",
                   comp.c_str (), payload_off, payload_size) << endl;

      if (comp == "gzip"
          && payload_off < in.size ()
          && payload_size > 0
          && (size_t) payload_off + (size_t) payload_size <= in.size ()
          && gzip_decompress (&in[payload_off], payload_size, out))
        {
          kind = "aarch64-zboot";
          return true;
        }
      return false;
    }

  // s390x zipl bzImage: boot wrapper + gzip(vmlinux.bin), trailing " zIPL".
  if (in.size () >= 16
      && memcmp (&in[in.size () - 5], " zIPL", 5) == 0)
    {
      size_t off = 0;
      while (off + 3 < in.size ())
        {
          // memchr for 0x1f then check gzip header — cheaper than per-byte.
          const void *hit = memchr (&in[off], 0x1f, in.size () - off);
          if (hit == NULL)
            break;
          off = (const unsigned char *) hit - in.data ();
          if (off + 3 < in.size ()
              && in[off + 1] == 0x8b && in[off + 2] == 0x08
              && gzip_decompress (&in[off], in.size () - off, out)
              && out.size () > (1u << 20))
            {
              if (verbose > 2)
                clog << _F("vmlinuz looks like s390x zipl (gzip piggy at %#zx -> %zu bytes)",
                           off, out.size ()) << endl;
              kind = "s390x-zipl";
              return true;
            }
          out.clear ();
          off++;
        }
    }

  return false;
}

static uint32_t
read_note_u32 (const unsigned char *p, bool be)
{
  uint32_t v;
  memcpy (&v, p, 4);
#if __BYTE_ORDER == __LITTLE_ENDIAN
  if (be)
    v = bswap_32 (v);
#elif __BYTE_ORDER == __BIG_ENDIAN
  if (!be)
    v = bswap_32 (v);
#else
#error Bad host __BYTE_ORDER
#endif
  return v;
}

/* Rough extent of an embedded ELF at OFF, based on phdr/shdr tables.
   Returns 0 if OFF does not look like a plausible ELF header.  Used only
   to decide whether a build-id note belongs to a small embedded blob
   (vdso, etc.) vs. the flat vmlinux image.  */
static size_t
embedded_elf_size (const vector<unsigned char> &blob, size_t off)
{
  if (off + 64 > blob.size ()
      || blob[off] != 0x7f || blob[off + 1] != 'E'
      || blob[off + 2] != 'L' || blob[off + 3] != 'F')
    return 0;

  unsigned char ei_class = blob[off + 4];
  unsigned char ei_data = blob[off + 5];
  bool be = (ei_data == 2);
  auto u16 = [&] (size_t o) -> uint32_t {
    uint16_t v;
    memcpy (&v, &blob[o], 2);
#if __BYTE_ORDER == __LITTLE_ENDIAN
    if (be) v = bswap_16 (v);
#elif __BYTE_ORDER == __BIG_ENDIAN
    if (!be) v = bswap_16 (v);
#endif
    return v;
  };
  auto u32 = [&] (size_t o) -> uint64_t {
    return read_note_u32 (&blob[o], be);
  };
  auto u64 = [&] (size_t o) -> uint64_t {
    uint64_t v;
    memcpy (&v, &blob[o], 8);
#if __BYTE_ORDER == __LITTLE_ENDIAN
    if (be) v = bswap_64 (v);
#elif __BYTE_ORDER == __BIG_ENDIAN
    if (!be) v = bswap_64 (v);
#endif
    return v;
  };

  uint64_t e_phoff, e_shoff;
  uint32_t e_phentsize, e_phnum, e_shentsize, e_shnum;
  if (ei_class == 2) // ELFCLASS64
    {
      e_phoff = u64 (off + 32);
      e_shoff = u64 (off + 40);
      e_phentsize = u16 (off + 54);
      e_phnum = u16 (off + 56);
      e_shentsize = u16 (off + 58);
      e_shnum = u16 (off + 60);
    }
  else if (ei_class == 1) // ELFCLASS32
    {
      e_phoff = u32 (off + 28);
      e_shoff = u32 (off + 32);
      e_phentsize = u16 (off + 42);
      e_phnum = u16 (off + 44);
      e_shentsize = u16 (off + 46);
      e_shnum = u16 (off + 48);
    }
  else
    return 0;

  // Cap absurd tables so a corrupt header can't run away.
  if (e_phnum > 512 || e_shnum > 1024
      || e_phentsize > 1024 || e_shentsize > 1024)
    return 0;

  uint64_t end = 64;
  if (e_shoff && e_shnum && e_shentsize)
    end = max (end, e_shoff + (uint64_t) e_shnum * e_shentsize);
  if (e_phoff && e_phnum && e_phentsize)
    {
      end = max (end, e_phoff + (uint64_t) e_phnum * e_phentsize);
      for (uint32_t i = 0; i < e_phnum; i++)
        {
          size_t poff = off + (size_t) e_phoff + i * e_phentsize;
          if (poff + e_phentsize > blob.size ())
            break;
          uint64_t p_offset, p_filesz;
          if (ei_class == 2)
            {
              p_offset = u64 (poff + 8);
              p_filesz = u64 (poff + 32);
            }
          else
            {
              p_offset = u32 (poff + 4);
              p_filesz = u32 (poff + 16);
            }
          if (p_filesz > blob.size ())
            continue;
          end = max (end, p_offset + p_filesz);
        }
    }

  if (end > blob.size () - off)
    end = blob.size () - off;
  // Reject "ELFs" that claim to span most of a kernel image — those are
  // almost certainly false-positive magics inside .rodata.
  if (end > blob.size () / 4 && end > (16u << 20))
    return 0;
  return (size_t) end;
}

/* Find embedded ELF [start, start+size) ranges inside the flat image.  */
static void
find_embedded_elves (const vector<unsigned char> &blob,
                     vector<pair<size_t, size_t> > &elves)
{
  elves.clear ();
  for (size_t off = 0; off + 4 <= blob.size (); )
    {
      const void *hit = memchr (&blob[off], 0x7f, blob.size () - off);
      if (hit == NULL)
        break;
      off = (const unsigned char *) hit - blob.data ();
      if (off + 4 <= blob.size ()
          && blob[off + 1] == 'E' && blob[off + 2] == 'L'
          && blob[off + 3] == 'F')
        {
          size_t sz = embedded_elf_size (blob, off);
          if (sz > 0)
            elves.push_back (make_pair (off, sz));
        }
      off++;
    }
}

/* Size of the object that "owns" a note at HDR.  Notes that fall inside
   an embedded ELF (vdso, …) get that ELF's size; notes in the flat
   objcopy -O binary image itself get the whole blob size — i.e. the
   kernel, which is almost certainly the largest thing in the bundle.  */
static size_t
note_owner_size (size_t hdr, size_t blob_size,
                 const vector<pair<size_t, size_t> > &elves)
{
  for (size_t i = 0; i < elves.size (); i++)
    if (hdr >= elves[i].first
        && hdr < elves[i].first + elves[i].second)
      return elves[i].second;
  return blob_size;
}

/* Scan a raw (possibly non-ELF) kernel image for NT_GNU_BUILD_ID notes.
   Prefer notes that belong to the largest owning object (the flat
   vmlinux image beats embedded vdso/etc. ELFs), then break ties with
   Linux BUILD_SALT / LTO_INFO neighbour affinity.  */
static string
build_id_from_raw_notes (const vector<unsigned char> &blob,
                         const string &kernel_release, int verbose)
{
  struct cand {
    size_t owner_size;
    int score;
    size_t off;
    string hex;
  };
  vector<cand> cands;
  vector<pair<size_t, size_t> > elves;
  find_embedded_elves (blob, elves);

  for (int pass = 0; pass < 2; pass++)
    {
      bool be = (pass == 1);
      // Walk looking for name "GNU\0" preceded by a plausible note header.
      const unsigned char gnu[4] = { 'G', 'N', 'U', 0 };
      for (size_t i = 12; i + 4 <= blob.size (); i++)
        {
          if (memcmp (&blob[i], gnu, 4) != 0)
            continue;
          size_t hdr = i - 12;
          uint32_t namesz = read_note_u32 (&blob[hdr], be);
          uint32_t descsz = read_note_u32 (&blob[hdr + 4], be);
          uint32_t type = read_note_u32 (&blob[hdr + 8], be);
          if (namesz != 4 || type != NT_GNU_BUILD_ID || descsz == 0
              || descsz > 64)
            continue;
          size_t desc_off = i + ((namesz + 3) & ~3u);
          if (desc_off + descsz > blob.size ())
            continue;

          int score = be ? 0 : 1; // slight LE bias; real score comes from neighbours
          // Examine neighbouring notes in a small window.
          size_t win_lo = hdr > 256 ? hdr - 256 : 0;
          size_t win_hi = min (blob.size (), hdr + 256);
          for (size_t p = win_lo; p + 16 <= win_hi; p += 4)
            {
              uint32_t ns = read_note_u32 (&blob[p], be);
              uint32_t ds = read_note_u32 (&blob[p + 4], be);
              uint32_t ty = read_note_u32 (&blob[p + 8], be);
              if (ns != 6 /* "Linux\0" */ || ds > 256)
                continue;
              size_t name_off = p + 12;
              if (name_off + 6 > blob.size ())
                continue;
              if (memcmp (&blob[name_off], "Linux", 6) != 0)
                continue;
              size_t d_off = name_off + ((ns + 3) & ~3u);
              if (d_off + ds > blob.size ())
                continue;
              if (ty == LINUX_ELFNOTE_BUILD_SALT)
                {
                  score += 10;
                  if (!kernel_release.empty ()
                      && ds >= kernel_release.size ()
                      && memcmp (&blob[d_off], kernel_release.data (),
                                 kernel_release.size ()) == 0)
                    score += 20;
                }
              else if (ty == LINUX_ELFNOTE_LTO_INFO)
                score += 5;
              else
                score += 1;
            }

          cand c;
          c.owner_size = note_owner_size (hdr, blob.size (), elves);
          c.score = score;
          c.off = hdr;
          c.hex = hex_dump (&blob[desc_off], descsz);
          cands.push_back (c);
        }
    }

  if (cands.empty ())
    return "";

  // Largest owning object first (flat vmlinux >> vdso); note-neighbour
  // score is only a tie-breaker.
  sort (cands.begin (), cands.end (),
        [] (const cand &a, const cand &b) {
          if (a.owner_size != b.owner_size)
            return a.owner_size > b.owner_size;
          if (a.score != b.score)
            return a.score > b.score;
          return a.off < b.off;
        });

  if (verbose > 2)
    {
      clog << _F("Kernel build-id note candidates (%zu):", cands.size ())
           << endl;
      for (size_t i = 0; i < cands.size () && i < 5; i++)
        clog << "  " << cands[i].hex
             << " owner_size=" << cands[i].owner_size
             << " score=" << cands[i].score
             << " @" << hex << cands[i].off << dec << endl;
    }

  // Require either a dominant owner (the flat image itself) or some
  // Linux-note affinity, so we don't return a random GNU note from a
  // lone embedded firmware blob.
  if (cands[0].owner_size < blob.size () && cands[0].score < 10)
    return "";
  return cands[0].hex;
}

/* Extract NT_GNU_BUILD_ID from a (possibly non-ELF) vmlinuz boot image.
   Used on aarch64 (EFI zboot) and s390x (zipl), where objcopy -O binary
   leaves the .notes contents in the payload but strips the ELF container
   that elfutils knows how to read.  */
static string
build_id_from_vmlinuz_image (const string &path, const string &kernel_release,
                            int verbose)
{
  vector<unsigned char> file;
  if (!read_whole_file (path, file))
    return "";

  vector<unsigned char> payload;
  string kind;
  if (peel_boot_image (file, payload, kind, verbose))
    {
      if (verbose > 1)
        clog << _F("Attempting to extract kernel build ID from %s (%s)",
                   path.c_str (), kind.c_str ()) << endl;
      return build_id_from_raw_notes (payload, kernel_release, verbose);
    }

  // Already a raw Image / uncompressed payload, or unknown wrapper:
  // try scanning the file bytes directly (cheap if no notes).
  if (verbose > 2)
    clog << _F("Scanning %s for raw NT_GNU_BUILD_ID notes",
               path.c_str ()) << endl;
  return build_id_from_raw_notes (file, kernel_release, verbose);
}
#endif // HAVE_LIBZ

/* Get the kernel build ID */
string
get_kernel_build_id(systemtap_session &s)
{
  string hex;

  // Try to find BuildID from vmlinux.id (kernel-devel on Fedora/RHEL).
  string kernel_buildID_path = s.kernel_build_tree + "/vmlinux.id";
  if (s.verbose > 2)
    clog << _F("Attempting to extract kernel debuginfo build ID from %s",
               kernel_buildID_path.c_str ()) << endl;
  ifstream buildIDfile (kernel_buildID_path.c_str ());
  if (buildIDfile.is_open ())
    {
      getline (buildIDfile, hex);
      // Accept EOF after a successful read (vmlinux.id may lack a
      // trailing newline); only reject hard failbit / empty results.
      if (!buildIDfile.fail () && !hex.empty ())
        {
          if (s.verbose > 1)
            clog << _F("Extracted kernel build ID %s from %s",
                       hex.c_str (), kernel_buildID_path.c_str ()) << endl;
          return hex;
        }
      hex.clear ();
    }

  // Try to find BuildID from the notes file if we are building natively.
  if (s.native_build)
    {
      if (s.verbose > 1)
        clog << _("Attempting to extract kernel debuginfo build ID from /sys/kernel/notes")
             << endl;

      const char *notesfile = "/sys/kernel/notes";
      int fd = open64 (notesfile, O_RDONLY);
      if (fd >= 0)
        {
          assert (sizeof (Elf32_Nhdr) == sizeof (GElf_Nhdr));
          assert (sizeof (Elf64_Nhdr) == sizeof (GElf_Nhdr));

          union
          {
            GElf_Nhdr nhdr;
            unsigned char data[8192];
          } buf;

          ssize_t n = read (fd, buf.data, sizeof buf);
          close (fd);

          if (n > 0)
            {
              unsigned char *p = buf.data;
              while (p < &buf.data[n])
                {
                  /* Native kernel: no endian translation required.  */
                  GElf_Nhdr *nhdr = (GElf_Nhdr *) p;
                  p += sizeof *nhdr;
                  unsigned char *name = p;
                  p += (nhdr->n_namesz + 3) & -4U;
                  unsigned char *bits = p;
                  p += (nhdr->n_descsz + 3) & -4U;

                  if (p <= &buf.data[n]
                      && nhdr->n_type == NT_GNU_BUILD_ID
                      && nhdr->n_namesz == sizeof "GNU"
                      && !memcmp (name, "GNU", sizeof "GNU"))
                    {
                      hex = hex_dump (bits, nhdr->n_descsz);
                    }
                }
              if (!hex.empty ())
                return hex;
            }
        }
    }

  // Fall back to the installed vmlinuz.  On ppc64le this is a real ELF;
  // on aarch64/s390x it is a bootloader image that still embeds the
  // vmlinux .notes (including NT_GNU_BUILD_ID) after decompression.
  string vmlinuz = find_vmlinuz_path (s);
  if (vmlinuz.empty ())
    {
      if (s.verbose > 2)
        clog << _("No vmlinuz found for kernel build-id extraction") << endl;
      return "";
    }

  if (s.verbose > 2)
    clog << _F("Attempting to extract kernel debuginfo build ID from %s",
               vmlinuz.c_str ()) << endl;

  hex = build_id_from_elf_vmlinuz (vmlinuz, s.verbose);
  if (!hex.empty ())
    return hex;

#if defined(HAVE_LIBZ)
  hex = build_id_from_vmlinuz_image (vmlinuz, s.kernel_release, s.verbose);
  if (!hex.empty ())
    {
      if (s.verbose > 1)
        clog << _F("Extracted kernel build ID %s from %s",
                   hex.c_str (), vmlinuz.c_str ()) << endl;
      return hex;
    }
#else
  (void) 0;
#endif

  return "";
}

/* Find the kernel build ID and attempt to download the matching debuginfo */
int download_kernel_debuginfo (systemtap_session &s, string hex)
{
  // NOTE: At some point we want to base the
  // already_tried_downloading_kernel_debuginfo flag on the build ID rather
  // than just the stap process.

  // Don't try this again if we already did.
  static int already_tried_downloading_kernel_debuginfo = 0;
  if(already_tried_downloading_kernel_debuginfo)
    return -1;

  // Attempt to download the debuginfo
  if(s.verbose > 1)
    clog << _F("Success! Extracted kernel debuginfo build ID: %s", hex.c_str()) << endl;
  int rc = execute_abrt_action_install_debuginfo_to_abrt_cache(hex);
  already_tried_downloading_kernel_debuginfo = 1;
  if (rc < 0)
    return -1;

  // Success!
  return 0;
}
