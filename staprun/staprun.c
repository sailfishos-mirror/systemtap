/* -*- linux-c -*-
 *
 * staprun.c - SystemTap module loader
 *
 * Copyright (C) 2005-2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _XOPEN_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#include "staprun.h"
#include "../privilege.h"
#include "../runtime/k_syms.h"
#include <string.h>
#include <sys/uio.h>
#include <glob.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/utsname.h>

/* used in dbug, _err and _perr */
char *__name__ = "staprun";

extern long delete_module(const char *, unsigned int);

int send_relocations ();
int send_tzinfo ();
int send_privilege_credentials (privilege_t user_credentials);
int send_remote_id ();

static int remove_module(const char *name, int verb);

static int stap_module_inserted = -1;

static void term_signal_handler(int signum __attribute ((unused)))
{
	if (stap_module_inserted == 0) {
		remove_module(modname, 1);
		free(modname);
	}
	_exit(1);
}

void setup_term_signals(void)
{
	sigset_t s;
	struct sigaction a;

	/* blocking all signals while we set things up */
	sigfillset(&s);
	sigprocmask(SIG_SETMASK, &s, NULL);

	/* handle signals */
	memset(&a, 0, sizeof(a));
	sigfillset(&a.sa_mask);
	a.sa_handler = term_signal_handler;
	sigaction(SIGHUP, &a, NULL);
	sigaction(SIGINT, &a, NULL);
	sigaction(SIGTERM, &a, NULL);
	sigaction(SIGQUIT, &a, NULL);

	/* unblock all signals */
	sigemptyset(&s);
	sigprocmask(SIG_SETMASK, &s, NULL);
}

static int run_as(int exec_p, uid_t uid, gid_t gid, const char *path, char *const argv[])
{
	pid_t pid;
	int rstatus;

	if (verbose >= 2) {
		int i = 0;
		eprintf(exec_p ? "execing: ": "spawning: ");
		while (argv[i]) {
			eprintf("%s ", argv[i]);
			i++;
		}
		eprintf("\n");
	}

        if (exec_p)
          pid = 0;
        else
          pid = fork();

        if (pid < 0)
          {
            _perr("fork");
            return -1;
          }

	if (pid == 0) /* child process, or exec_p */
          {
            /* Make sure we run as the full user.  If we're
             * switching to a non-root user, this won't allow
             * that process to switch back to root (since the
             * original process is setuid). */
            if (setresgid (gid, gid, gid) < 0) {
              _perr("setresgid");
              exit(1);
            }
            if (setresuid (uid, uid, uid) < 0) {
              _perr("setresuid");
              exit(1);
            }

            /* Actually run the command. */
            if (execv(path, argv) < 0)
              perror(path);
            _exit(1);
          }

	if (waitpid(pid, &rstatus, 0) < 0)
          return -1;

	if (WIFEXITED(rstatus))
          return WEXITSTATUS(rstatus);
	return -1;
}

/*
 * Module to be inserted has one or more user-space probes.  Make sure
 * uprobes is enabled.
 * If /proc/kallsyms lists a symbol in uprobes (e.g. unregister_uprobe),
 * we're done.
 * Else try "modprobe uprobes" to load the uprobes module (if any)
 * built with the kernel.
 * If that fails, load the uprobes module built in runtime/uprobes.
 */
static int enable_uprobes(void)
{
	char *argv[10];
	char runtimeko[2048];
        int rc;

        /* Formerly, we did a grep /proc/kallsyms search to see if
           uprobes was already loaded into the kernel.  But this is
           a race waiting to happen.  Just try to load the thing.
           Quietly accept a -EEXIST error. */

        /* NB: don't use /sbin/modprobe, without more env. sanitation. */

	/* Try the specified module or the one from the runtime.  */
	if (uprobes_path)
	  snprintf (runtimeko, sizeof(runtimeko), "%s", uprobes_path);
	else
          /* NB: since PR5163, share/runtime/uprobes/uprobes.ko is not built 
             by systemtap. */
	  snprintf (runtimeko, sizeof(runtimeko), "%s/uprobes/uprobes.ko",
		    (getenv("SYSTEMTAP_RUNTIME") ?: PKGDATADIR "/runtime"));
	dbug(2, "Inserting uprobes module from %s.\n", runtimeko);
	/* This module may be signed, so use insert_module to load it.  */
	argv[0] = NULL;

	rc = insert_module(runtimeko, NULL, argv, assert_uprobes_module_permissions, NULL);
        if ((rc == 0) || /* OK */
            (rc == -EEXIST)) /* Someone else might have loaded it */
		return 0;

        err("Couldn't insert module '%s': %s\n", runtimeko, moderror(errno));
	return 1; /* failure */
}

static int insert_stap_module(privilege_t *user_credentials)
{
	char special_options[128];
        int rc, fips_mode_fd;
        char fips_mode = '0';
        char *misc = "";

	/* Add the _stp_bufsize option.  */
	if (snprintf_chk(special_options, sizeof (special_options),
			 "_stp_bufsize=%d", (int)buffer_size))
		return -1;

        fips_mode_fd = open("/proc/sys/crypto/fips_enabled", O_RDONLY);
        if (fips_mode_fd >= 0) {
                char c;
                rc = read(fips_mode_fd, &c, 1);
                if (rc == 1) fips_mode = c;
                close (fips_mode_fd);
        }

        /* In FIPS mode, a kernel may panic if given an improperly-signed module.
           Right now, we have no way of signing them with the kernel build-time keys,
           so we punt.  See also SecureBoot. */
        if ((fips_mode != '0') && !getenv("STAP_FIPS_OVERRIDE")) {
                errno = EPERM;
                stap_module_inserted = -1;
                misc = "in FIPS mode ";
        } else {
        	stap_module_inserted = insert_module(modpath, special_options,
                                                     modoptions,
                                                     assert_stap_module_permissions,
                                                     user_credentials);
        }
        if (stap_module_inserted != 0)
                err("Couldn't insert module %s'%s': %s\n", misc, modpath, moderror(errno));
	return stap_module_inserted;
}

static void remove_all_modules(void)
{
	char *base;
	struct statfs st;
	struct dirent *d;
	DIR *moddir;

        /* NB: nothing to do with PR14245 */
	if (statfs("/sys/kernel/debug", &st) == 0 && (int)st.f_type == (int)DEBUGFS_MAGIC)
		base = "/sys/kernel/debug/systemtap";
	else
		base = "/proc/systemtap";

	moddir = opendir(base);
	if (moddir) {
		while ((d = readdir(moddir))) {
                        if (strcmp(d->d_name, ".") == 0) continue;
                        if (strcmp(d->d_name, "..") == 0) continue;
                        relay_basedir_fd = -1; /* each time! */
			if (remove_module(d->d_name, 0) == 0)
				printf("Module %s removed.\n", d->d_name);
                }
		closedir(moddir);
	}
}

static int remove_module(const char *name, int verb)
{
	int i, ret;
	enum {
		MAX_EINTR_TRIES = 5
	};

	dbug(2, "%s\n", name);

#ifdef PR_SET_NAME
        /* Make self easier to identify in vmcrash images */
        prctl (PR_SET_NAME, "staprun-d");
#endif

        (void) verb; /* XXX: ignore */

	if (strcmp(name, "*") == 0) {
		remove_all_modules();
		return 0;
	}

        /* We call init_ctl_channel/close_ctl_channel to check whether
           the module is a systemtap-built one (having the right files),
           and that it's already unattached (because otherwise it'd EBUSY
           the opens, and that it belongs to our uid (because otherwise
           a faccessat(2) test on the .cmd file will fail). */
        ret = init_ctl_channel (name, 0);
        if (ret < 0) {
                err("'%s' is not a zombie systemtap module.\n", name);
                return ret;
        }
        close_ctl_channel ();

	dbug(2, "removing module %s\n", name);
	PROBE1(staprun, remove__module, name);

	for (i = 0; i < MAX_EINTR_TRIES; i++) {
		ret = delete_module (name, O_NONBLOCK);
		if (ret == 0 || (errno != EINTR && errno != EWOULDBLOCK))
			break;
		usleep(100 * i);
	}

	if (ret != 0) {
		err("Couldn't remove module '%s': %s.\n", name, strerror(errno));
		return 1;
	}

	dbug(1, "Module %s removed.\n", name);
	return 0;
}


/* As per PR13193 & PR1548, some kernels have a buggy
   kprobes-optimization code, which results in BUG/panics in certain
   circumstances.  We turn off kprobes optimization as a conservative
   measure, unless told otherwise by an environment variable.
*/
void disable_kprobes_optimization()
{
        /* Test if the file exists at all. */
        const char* proc_kprobes = "/proc/sys/debug/kprobes-optimization";
        char prev;
        int rc, fd;
        struct utsname uts;

        /* PR13814; disable this facility for new enough kernels, containing
         * these fix commits: 86b4ce31 46484688 3f33ab1c */
        /* PR15484; whoops, not enough, problem still seen on Debian
         * 3.8.12 kernel. */
        if (0 && (uname (&uts) == 0) && (strverscmp (uts.release, "3.4") >= 0))
                return;
        /* Disable kprobes optimization due to problems seen on F29 5.0 kernel.
           PR24416; RCU hang detection with uprobes_onthefly.exp. */
        /* RHBZ1697531 - x86 kprobe optimization causes rcu hang */
        if ((0 && uname (&uts) == 0) && (strverscmp (uts.release, "4.8") >= 0))
                return;

        if (getenv ("STAP_PR13193_OVERRIDE"))
                return;

        /* See the initial state; if it's already disabled, we do nothing. */
        fd = open (proc_kprobes, O_RDONLY);
        if (fd < 0) 
                return;
        rc = read (fd, &prev, sizeof(prev));
        (void) close (fd);
        if (rc < 1 || prev == '0') /* Already disabled or unavailable */
                return;

        fd = open (proc_kprobes, O_WRONLY);
        if (fd < 0) 
                return;
        prev = '0'; /* really, next */
        rc = write (fd, &prev, sizeof(prev));
        (void) close (fd);
        if (rc == 1)
                dbug(1, "Disabled %s.\n", proc_kprobes);
        else
                dbug(1, "Error %d/%d disabling %s.\n", rc, errno, proc_kprobes);
}


/* BZ1552745: /proc/sys/kernel/kptr_restrict makes /sys/module
   ... addresses unreliable on 2018+ kernels.  circumstances.  We
   tweak this security measure (setting it to '1'), unless told
   otherwise by an environment variable.  We could turn it back later,
   but this would create a race condition between concurrent runs of
   staprun.  The '1' setting is nominally more secure than the default
   '0', except that for /sys/module/$MODULE/sections/$SECTION the '0'
   case produces obfuscated 0-based pointers, and '1' produces good
   ones (to a root user).  Strange but true.
*/
void tweak_kptr_restrict()
{
        const char* proc_kptr = "/proc/sys/kernel/kptr_restrict";
        char prev;
        int rc, fd;
        struct utsname uts;

        /* Relevant change appears to have been introduced in v4.15 in
         * commit ef0010a30935de4e0211. */
        if ((uname (&uts) == 0) && (strverscmp (uts.release, "4.15") < 0))
                return;

        if (getenv ("STAP_BZ1552745_OVERRIDE"))
                return;

        /* See the initial state; if it's already set, we do nothing. */
        fd = open (proc_kptr, O_RDONLY);
        if (fd < 0) 
                return;
        rc = read (fd, &prev, sizeof(prev));
        (void) close (fd);
        if (rc < 1 || prev == '1') /* Already set or unavailable */
                return;

        fd = open (proc_kptr, O_WRONLY);
        if (fd < 0) 
                return;
        prev = '1'; /* really, next */
        rc = write (fd, &prev, sizeof(prev));
        (void) close (fd);
        if (rc == 1)
                dbug(1, "Set %s.\n", proc_kptr);
        else
                dbug(1, "Error %d/%d setting %s.\n", rc, errno, proc_kptr);
}



int init_staprun(void)
{
	privilege_t user_credentials = pr_unknown;
	int rc;
	dbug(2, "init_staprun\n");

	if (mountfs() < 0)
		return -1;

	rc = 0;
	if (delete_mod)
		exit(remove_module(modname, 1));
        if (attach_mod) {
                /* PR14245: prime the relay_basedir_fd pump. */
		rc = init_ctl_channel (modname, 0);
		if (rc >= 0)
		  close_ctl_channel ();
        } else /* if (!attach_mod) */ {
		if (need_uprobes && enable_uprobes() != 0)
			return -1;

                disable_kprobes_optimization();

		if (insert_stap_module(& user_credentials) < 0) {
			if(!rename_mod && errno == EEXIST)
				err("Rerun with staprun option '-R' to rename this module.\n");
			return -1;
		}
		rc = init_ctl_channel (modname, 0);
		if (rc >= 0) {
		  /* If we are unable to send privilege credentials then we have an old
		     (pre 1.7) stap module or a non-stap module. In either case, the privilege
		     credentials required for loading the module have already been determined and
		     checked (see check_groups, get_module_required_credentials).
		  */
		  send_privilege_credentials(user_credentials);
		  rc = send_relocations();
		  if (rc == 0) {
		    rc = send_tzinfo();
		    if (rc == 0 && remote_id >= 0)
		      send_remote_id();
		  }
		  close_ctl_channel ();
		}
		if (rc != 0)
		  remove_module(modname, 1);
	}
	return rc;
}

int main(int argc, char **argv)
{
	int rc;

	/* Force libc to make our stderr messages atomic by enabling line
	   buffering since stderr is unbuffered by default. Without this, libc
	   is at liberty to split a single stderr message into multiple writes
	   to the fd while holding flockfile(stderr). POSIX only guarantees that
	   a single write(2) is atomic; chaining several write(2) calls together
	   won't be atomic, and we don't want libc to do that within a single
	   *fprintf(stderr) call since it'll mangle messages printed across
	   different processes (*not* threads). */
	setlinebuf(stderr);

	/* NB: Don't do the geteuid()!=0 check here, since we want to
	   test command-line error-handling while running non-root. */
	/* Get rid of a few standard environment variables (which */
	/* might cause us to do unintended things). */
	rc = unsetenv("IFS") || unsetenv("CDPATH") || unsetenv("ENV")
	    || unsetenv("BASH_ENV");
	if (rc) {
		_perr("unsetenv failed");
		exit(-1);
	}

	if (getuid() != geteuid()) { /* setuid? */
		rc = unsetenv("SYSTEMTAP_STAPRUN") ||
                  unsetenv("SYSTEMTAP_STAPIO") ||
                  unsetenv("SYSTEMTAP_RUNTIME");

		if (rc) {
			_perr("unsetenv failed");
			exit(-1);
		}
	}

	setup_signals();
	setup_term_signals();

	parse_args(argc, argv);

        /* PR14245, For security reasons, preclude "staprun -F fd".
           The -F option is only for stapio, but the overzealous quest
           for commonality doesn't let us express that nicer. */
        if (relay_basedir_fd >= 0) {
                err(_("Relay basedir -F option is invalid for staprun\n"));
                exit(1);
        }
        /* NB: later on, some of our own code may set relay_basedir_fd, for
           passing onto stapio - or for our own reuse.  That's OK.  */


	if (buffer_size)
		dbug(2, "Using a buffer of %u MB.\n", buffer_size);

	int mod_optind = optind;
	if (optind < argc) {
		parse_modpath(argv[optind++]);
		dbug(2, "modpath=\"%s\", modname=\"%s\"\n", modpath, modname);
	}

	if (optind < argc) {
		if (attach_mod) {
			err("Cannot have module options with attach (-A).\n");
			usage(argv[0],1);
		} else {
			unsigned start_idx = 0;
			while (optind < argc && start_idx + 1 < MAXMODOPTIONS)
				modoptions[start_idx++] = argv[optind++];
			modoptions[start_idx] = NULL;
		}
	}

	if (modpath == NULL || *modpath == '\0') {
		err("Need a module name or path to load.\n");
		usage(argv[0],1);
	}

	if (geteuid() != 0) {
		err("The effective user ID of staprun must be set to the root user.\n"
		    "  Check permissions on staprun and ensure it is a setuid root program.\n");
		exit(1);
	}

	char verbose_level[33];
	sprintf(verbose_level, "%d", verbose);
	rc = setenv("SYSTEMTAP_VERBOSE", verbose_level, 0);
	if (rc) {
		_perr("SYSTEMTAP_VERBOSE setenv failed");
		exit(-1);
	}

	if (init_staprun())
		exit(1);

	argv[0] = getenv ("SYSTEMTAP_STAPIO") ?: PKGLIBDIR "/stapio";

	/* Copy nenamed modname into argv */
	if(rename_mod)
		argv[mod_optind] = modname;

        /* PR14245: pass -F fd to stapio. Unfortunately, this requires
           us to extend argv[], with all the C fun that entails. */
#ifdef HAVE_OPENAT
        if (relay_basedir_fd >= 0) {
                char ** new_argv = calloc(argc+2, sizeof(char *));
                const int new_Foption_size = 10; /* -FNNNNN */
                char * new_Foption = malloc(new_Foption_size);
                int i;

                if (new_argv && new_Foption) {
                        snprintf (new_Foption, new_Foption_size, "-F%d", relay_basedir_fd);
                        for (i=0; i < argc && argv[i] != NULL; i++)
                                new_argv[i] = argv[i];
                        new_argv[i++] = new_Foption; /* overwrite the NULL */
                        new_argv[i++] = NULL; /* ensconce a new NULL */

                        argv = new_argv;
                }
        }
#endif

	/* Run stapio */
	if (run_as (1, getuid(), getgid(), argv[0], argv) < 0) {
		perror(argv[0]);
		goto err;
	}

	free(modname);
	return 0;

err:
	remove_module(modname, 1);
	free(modname);
	return 1;
}



/* Send a variety of relocation-related data to the kernel: for the
   kernel proper, just the "_stext" symbol address; for all loaded
   modules, a variety of symbol base addresses.

   We do this under protest.  The kernel ought expose this data to
   modules such as ourselves, but instead the upstream community
   continually shrinks its module-facing interfaces, including this
   stuff, even when users exist.

   PR26074: as of kernel 5.7+ / commit 0bd476e6c671 and under further
   protest, we must also send the address of kallsyms_lookup_name and
   kallsyms_for_each_symbol.
*/


int send_a_relocation (const char* module, const char* reloc, unsigned long long address)
{
  struct _stp_msg_relocation msg;
  int rc;

  if (strlen(module) >= STP_MODULE_NAME_LEN-1) {
          dbug (1, "module name too long: %s\n", module);
          return -EINVAL; 
  }
  strncpy (msg.module, module, STP_MODULE_NAME_LEN - 1);
  
  if (strlen(reloc) >= STP_SYMBOL_NAME_LEN-1) {
          dbug (1, "reloc name too long: %s\n", reloc);
          return -EINVAL; 
  }
  strncpy (msg.reloc, reloc, STP_MODULE_NAME_LEN - 1);

  msg.address = address;

  rc = send_request (STP_RELOCATION, & msg, sizeof (msg));
  if (rc != 0)
    perror ("Unable to send relocation");
  return rc;
}


int send_relocation_kernel ()
{
  FILE* kallsyms;
  int rc = 0;

  errno = 0;
  kallsyms = fopen ("/proc/kallsyms", "r");
  if (kallsyms == NULL)
    {
      perror("cannot open /proc/kallsyms");
      // ... and the kernel module will almost certainly fail to initialize.
      return errno;
    }
  else
    {
      int found_stext = 0;
      int found_kallsyms_lookup_name = 0;
      int found_kallsyms_on_each_symbol = 0;
      int found_module_kallsyms_on_each_symbol = 0;
      int done_with_kallsyms = 0;
      char *line = NULL;
      size_t linesz = 0;
      while (! feof(kallsyms) && !done_with_kallsyms)
        {
          ssize_t linesize = getline (& line, & linesz, kallsyms);
          if (linesize > 0)
            {
              unsigned long long address;
	      int pos = -1;
              if (sscanf (line, "%llx %*c %n", &address, &pos) != 1
                  || pos == -1)
                continue; // no symbols here
	      if (linesize - pos == sizeof KERNEL_RELOC_SYMBOL
		  && !strcmp(line + pos, KERNEL_RELOC_SYMBOL "\n"))
                {
                  /* NB: even on ppc, we use the _stext relocation name. */
                  rc = send_a_relocation ("kernel", "_stext", address);
		  if (rc != 0)
		    break;

                  found_stext=1;
                }
              else if (linesize - pos == sizeof "kallsyms_lookup_name"
                       && !strcmp(line + pos, "kallsyms_lookup_name" "\n"))
                {
                  rc = send_a_relocation ("kernel", "kallsyms_lookup_name", address);
		  if (rc != 0) // non fatal, follows perror()
                    dbug(1, "Relocation was kallsyms_lookup_name=%llx\n", address);

                  found_kallsyms_lookup_name = 1;
                }
              else if (linesize - pos == sizeof "kallsyms_on_each_symbol"
                       && !strcmp(line + pos, "kallsyms_on_each_symbol" "\n"))
                {
                  rc = send_a_relocation ("kernel", "kallsyms_on_each_symbol", address);
		  if (rc != 0) // non fatal, follows perror()
                    dbug(1, "Relocation was reloc kallsyms_on_each_symbol=%llx\n", address);

                  found_kallsyms_on_each_symbol = 1;
                }
              else if (linesize - pos == sizeof "module_kallsyms_on_each_symbol"
                       && !strcmp(line + pos, "module_kallsyms_on_each_symbol" "\n"))
                {
                  rc = send_a_relocation ("kernel", "module_kallsyms_on_each_symbol", address);
		  if (rc != 0) // non fatal, follows perror()
                    dbug(1, "Relocation was reloc module_kallsyms_on_each_symbol=%llx\n", address);

                  found_module_kallsyms_on_each_symbol = 1;
                }
            }
          done_with_kallsyms = found_stext
            && found_kallsyms_lookup_name
            && found_kallsyms_on_each_symbol
            && found_module_kallsyms_on_each_symbol;
        }
      free (line);
      fclose (kallsyms);

      /* PR26074: Arguably, failure to find the kallsyms_* symbols may
       * not be a fatal error. The fallback kallsyms_lookup_name()
       * function in sym.c then returns 0, but it's barely conceivable
       * some modules never call it. */
      /* if (!done_with_kallsyms) */
      if (!found_stext)
        return rc;

      /* detect note section, send flag if there
       * NB: address=2 represents existed note, the real one in _stp_module
       */
      if (!access("/sys/kernel/notes", R_OK))
	rc = send_a_relocation ("kernel", ".note.gnu.build-id", 2);
    }

  return rc;
}


int send_relocation_modules ()
{
  unsigned i = 0;
  glob_t globbuf;
  globbuf.gl_pathc = 0;
  int r = glob("/sys/module/*/sections/*", GLOB_PERIOD, NULL, &globbuf);

  if (r == GLOB_NOSPACE || r == GLOB_ABORTED)
    return r;

  for (i=0; i<globbuf.gl_pathc; i++)
    {
      char *module_section_file;
      char *section_name;
      char *module_name;
      char *module_name_end;
      FILE* secfile;
      unsigned long long section_address;

      module_section_file = globbuf.gl_pathv[i];

      /* Tokenize the file name.
         Sample gl_pathv[]: /sys/modules/zlib_deflate/sections/.text
         Pieces:                         ^^^^^^^^^^^^          ^^^^^
      */
      section_name = strrchr (module_section_file, '/');
      if (! section_name) continue;
      section_name ++;

      if (!strcmp (section_name, ".")) continue;
      if (!strcmp (section_name, "..")) continue;

      module_name = strchr (module_section_file, '/');
      if (! module_name) continue;
      module_name ++;
      module_name = strchr (module_name, '/');
      if (! module_name) continue;
      module_name ++;
      module_name = strchr (module_name, '/');
      if (! module_name) continue;
      module_name ++;

      module_name_end = strchr (module_name, '/');
      if (! module_name_end) continue;

      secfile = fopen (module_section_file, "r");
      if (! secfile) continue;

      if (1 == fscanf (secfile, "0x%llx", &section_address))
        {
          /* Now we destructively modify the string, but by now the file
             is open so we won't need the full name again. */
          *module_name_end = '\0';

          /* PR6503.  /sys/module/.../sections/...init.... sometimes contain
             non-0 addresses, even though the respective module-initialization
             sections were already unloaded.  We override the addresses here. */
          if (strstr (section_name, "init.") != NULL) /* .init.text, .devinit.rodata, ... */
             section_address = 0;

          (void) send_a_relocation (module_name, section_name, section_address);
          /* PR14005: take a pill, dude, a failure with an overlong
           * name does not call for freaking out.  Nor does an error
           * coming back from the write(2) into the module.  We will
           * just stagger along without that particular module/section
           * being present in the _stp_sections[] tables. */
        }

      if (strcmp (section_name, ".gnu.linkonce.this_module"))
        fclose (secfile);
      else
        {
          (void)set_clexec (fileno (secfile));
          /* NB: don't fclose this arbitrarily-chosen section file.
             This forces the kernel to keep a nonzero reference count
             on the subject module, until staprun exits, by which time
             the kernel module will have inserted its separate claws
             into the probeworthy modules.  This prevents a race
             condition where a probe may be just starting up at the
             same time that a probeworthy module is being unloaded. */
        }
    }

  globfree (& globbuf);
  return 0;
}



int send_relocations ()
{
  int rc;

  tweak_kptr_restrict();
  
  rc = send_relocation_kernel ();
  if (rc == 0)
    rc = send_relocation_modules ();
  return rc;
}


int send_tzinfo ()
{
  struct _stp_msg_tzinfo tzi;
  time_t now_t;
  struct tm* now;
  int rc;

  /* NB: This is not good enough; it sends DST-unaware numbers. */
#if 0
  tzset ();
  tzi.tz_gmtoff = timezone;
  strncpy (tzi.tz_name, tzname[0], STP_TZ_NAME_LEN - 1);
#endif

  time (& now_t);
  now = localtime (& now_t);
  tzi.tz_gmtoff = - now->tm_gmtoff;
  strncpy (tzi.tz_name, now->tm_zone, STP_TZ_NAME_LEN - 1);

  rc = send_request(STP_TZINFO, & tzi, sizeof(tzi));
  if (rc != 0)
    perror ("Unable to send time zone information");
  return rc;
}

int send_privilege_credentials (privilege_t user_credentials)
{
  struct _stp_msg_privilege_credentials pc;
  int rc;
  pc.pc_group_mask = user_credentials;
  rc = send_request(STP_PRIVILEGE_CREDENTIALS, & pc, sizeof(pc));
  if (rc != 0) {
    /* Not an error. Happens when pre 1.7 modules are loaded.  */
    dbug (1, "Unable to send user privilege credentials\n");
  }
  return rc;
}

int send_remote_id ()
{
  struct _stp_msg_remote_id rem;
  int rc;

  rem.remote_id = remote_id;
  strncpy (rem.remote_uri, remote_uri, STP_REMOTE_URI_LEN - 1);
  rem.remote_uri [STP_REMOTE_URI_LEN-1]='\0'; /* XXX: quietly truncate */
  rc = send_request(STP_REMOTE_ID, & rem, sizeof(rem));
  if (rc != 0)
    perror ("Unable to send remote id");
  return rc;
}
