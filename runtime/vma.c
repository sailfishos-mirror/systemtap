/* -*- linux-c -*- 
 * VMA tracking and lookup functions.
 *
 * Copyright (C) 2005-2019 Red Hat Inc.
 * Copyright (C) 2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_VMA_C_
#define _STP_VMA_C_

#include "sym.h"
#include "stp_string.c"
#include "task_finder_vma.c"

#include <asm/uaccess.h>

static void _stp_vma_match_vdso(struct task_struct *tsk)
{
/* vdso is arch specific */
#if defined(STAPCONF_MM_CONTEXT_VDSO) || defined(STAPCONF_MM_CONTEXT_VDSO_BASE)
  int i, j;
  if (tsk->mm)
    {
      struct _stp_module *found = NULL;

#ifdef STAPCONF_MM_CONTEXT_VDSO
      unsigned long vdso_addr = (unsigned long) tsk->mm->context.vdso;
#else
      unsigned long vdso_addr = tsk->mm->context.vdso_base;
#endif

      dbug_task_vma(1,"tsk: %d vdso: 0x%lx\n", tsk->pid, vdso_addr);

      for (i = 0; i < _stp_num_modules && found == NULL; i++) {
	struct _stp_module *m = _stp_modules[i];
	if (m->path
	    && m->path[0] == '/'
	    && m->num_sections == 1)
	  {
	    unsigned long notes_addr;
	    int all_ok = 1;

	    /* Assume that if the path's basename starts with 'vdso'
	     * and ends with '.so', it is the vdso.
	     *
	     * Note that this logic should match up with the logic in
	     * the find_vdso() function in translate.cxx. */
	    const char *name = strrchr(m->path, '/');
	    if (name)
	      {
		const char *ext;

		name++;
		ext = strrchr(name, '.');
		if (!ext
		    || strncmp("vdso", name, 4) != 0
		    || strcmp(".so", ext) != 0)
		  continue;
	      }

	    notes_addr = vdso_addr + m->build_id_offset;
	    dbug_task_vma(1,"notes_addr %s: 0x%lx + 0x%lx = 0x%lx (len: %x)\n", m->path,
		  vdso_addr, m->build_id_offset, notes_addr, m->build_id_len);
	    for (j = 0; j < m->build_id_len; j++)
	      {
		int rc;
		unsigned char b;

		/*
		 * Since we're only reading here, we can call
		 * __access_process_vm_noflush(), which only calls
		 * things that are exported.
		 */
		if (tsk == current)
		  {
		    rc = copy_from_user(&b, (void*)(notes_addr + j), 1);
		  }
		else
		  {
		    rc = (__access_process_vm_noflush(tsk, (notes_addr + j),
						      &b, 1, 0) != 1);
		  }
		if (rc || b != m->build_id_bits[j])
		  {
		    dbug_task_vma(1,"darn, not equal (rc=%d) at %d (0x%x != 0x%x)\n",
			  rc, j, b, m->build_id_bits[j]);
		    all_ok = 0;
		    break;
		  }
	      }
	    if (all_ok)
	      found = m;
	  }
      }
      if (found != NULL)
	{
	  stap_add_vma_map_info(tsk, vdso_addr,
				vdso_addr + found->sections[0].size, 0,
				"vdso", found);
	  dbug_task_vma(1,"found vdso: %s\n", found->path);
	}
    }
#endif /* STAPCONF_MM_CONTEXT_VDSO */
}

#ifdef HAVE_TASK_FINDER
/* exec callback, will try to match vdso for new process,
   will drop all vma maps for a process that disappears. */
static int _stp_vma_exec_cb(struct stap_task_finder_target *tgt,
			    struct task_struct *tsk,
			    int register_p,
			    int process_p)
{
  dbug_task_vma(1,
	    "tsk %d:%d , register_p: %d, process_p: %d\n",
	    tsk->pid, tsk->tgid, register_p, process_p);
  if (process_p)
    {
      if (register_p)
	_stp_vma_match_vdso(tsk);
      else
	stap_drop_vma_maps(tsk);
    }

  return 0;
}

/* mmap callback, will match new vma with _stp_module or register vma name. */
static int _stp_vma_mmap_cb(struct stap_task_finder_target *tgt,
			    struct task_struct *tsk,
			    char *path, struct dentry *dentry,
			    unsigned long addr,
			    unsigned long length,
			    unsigned long offset,
			    unsigned long vm_flags)
{
	int i, res;
	struct _stp_module *module = NULL;
	const char *ori_path = NULL;
	const char *name = ((dentry != NULL) ? (char *)dentry->d_name.name
			    : NULL);
        
        if (path == NULL || *path == '\0') /* unknown? */
                path = (char *)name; /* we'll copy this soon, in ..._add_vma_... */

	dbug_task_vma(1,
		  "mmap_cb: tsk %d:%d path %s, addr 0x%08lx, length 0x%08lx, offset 0x%lx, flags 0x%lx\n",
		  tsk->pid, tsk->tgid, path, addr, length, offset, vm_flags);
        
	// We used to be only interested in the first load of the whole module that
	// is executable.  But with modern enough gcc/ld.so, executables are mapped
        // in more small pieces (r--p,r-xp,rw-p, instead of r-xp, rw-p).  NB:
	// the first section might not be executable, so there can be an offset.
        //
        // We register whether or not we know the module,
	// so we can later lookup the name given an address for this task.
	if (path != NULL &&
	    (stap_find_vma_map_info(tsk->group_leader, addr, NULL, NULL, NULL, &ori_path, NULL) != 0 ||
	     strcmp(ori_path ?: "", path) != 0)) {
		for (i = 0; i < _stp_num_modules; i++) {
			// PR20433: papering over possibility of NULL pointers
			if (strcmp(path ?: "", _stp_modules[i]->path ?: "") == 0)
			{
			  unsigned long vm_start = 0;
			  unsigned long vm_end = 0;
			  dbug_task_vma(1,
				    "vm_cb: matched path %s to module (sec: %s)\n",
				    path, _stp_modules[i]->sections[0].name);
			  module = _stp_modules[i];
			  /* Make sure we really don't know about this module
			     yet.  If we do know, we might want to extend
			     the coverage. */
			  res = stap_find_vma_map_info_user(tsk->group_leader,
							    module,
							    &vm_start, &vm_end,
							    NULL);
			  if (res == -ESRCH)
			    res = stap_add_vma_map_info(tsk->group_leader,
							addr, addr + length,
							0, path, module);
			  else
			    res = stap_add_vma_map_info(tsk->group_leader,
							addr, addr + length,
							addr - vm_start, path, module);

			  /* VMA entries are allocated dynamically, this is fine,
			   * since we are in a task_finder callback, which is in
			   * user context. */
			  if (res != 0 && res != -EEXIST) {
				_stp_error ("Couldn't register module '%s' for pid %d (%d)\n", _stp_modules[i]->path, tsk->group_leader->pid, res);
			  }
			  return 0;
			}
		}

		/* None of the tracked modules matched, register without,
		 * to make sure we can lookup the name later. Ignore errors,
		 * we will just report unknown when asked and tables were
		 * full. Restrict to target process when given to preserve
		 * vma_map entry slots. */
		if (_stp_target == 0
		    || _stp_target == tsk->group_leader->pid)
		  {
		    res = stap_add_vma_map_info(tsk->group_leader, addr,
						addr + length, offset, path,
						NULL);
		    dbug_task_vma(1,
			      "registered '%s' for %d (res:%d) [%lx-%lx]\n",
			      path, tsk->group_leader->pid,
			      res, addr, addr + length);
		  }

	} else if (path != NULL) {
		// Once registered, we may want to extend an earlier
		// registered region. A segment might be mapped with
		// different flags for different offsets. If so we want
		// to record the extended range so we can address more
		// precisely to module names and symbols.
		res = stap_extend_vma_map_info(tsk->group_leader,
					       addr, addr + length);
		dbug_task_vma(1,
			  "extended '%s' for %d (res:%d) [%lx-%lx]\n",
			  path, tsk->group_leader->pid,
			  res, addr, addr + length);
	}
	return 0;
}

/* munmap callback, removes vma map info. */
static int _stp_vma_munmap_cb(struct stap_task_finder_target *tgt,
			      struct task_struct *tsk,
			      unsigned long addr,
			      unsigned long length)
{
        /* Unconditionally remove vm map info, ignore if not present. */
	stap_remove_vma_map_info(tsk->group_leader, addr);
	return 0;
}

#endif

/* Initializes the vma tracker. */
static int _stp_vma_init(void)
{
        int rc = 0;
#ifdef HAVE_TASK_FINDER
        static struct stap_task_finder_target vmcb = {
                // NB: no .pid, no .procname filters here.
                // This means that we get a system-wide mmap monitoring
                // widget while the script is running. (The
                // system-wideness may be restricted by stap -c or
                // -x.)  But this seems to be necessary if we want to
                // to stack tracebacks through arbitrary shared libraries.
                //
                // XXX: There may be an optimization opportunity
                // for executables (for which the main task-finder
                // callback should be sufficient).
                .pid = 0,
                .procname = NULL,
                .build_id_len = 0,
                .purpose = "vma tracking",
                .callback = &_stp_vma_exec_cb,
                .mmap_callback = &_stp_vma_mmap_cb,
                .munmap_callback = &_stp_vma_munmap_cb,
                .mprotect_callback = NULL
        };
	rc = stap_initialize_vma_map ();
	if (rc != 0) {
		_stp_error("Couldn't initialize vma map: %d\n", rc);
		return rc;
	}
	dbug_task_vma(1,
		  "registering vmcb (_stap_target: %d)\n", _stp_target);
	rc = stap_register_task_finder_target (& vmcb);
        /* NB: we don't need to call stap_cleanup_task_finder_target() to
         * free up any dynamically-allocated procname buffers since .procname
         * is always NULL (see above). */
	if (rc != 0)
		_stp_error("Couldn't register task finder target: %d\n", rc);
#endif
	return rc;
}

/* Get rid of the vma tracker (memory). */
static void _stp_vma_done(void)
{
/* See runtime/linux/runtime.h for more details. See also PR26123 */
#ifdef HAVE_TASK_FINDER
	stap_destroy_vma_map();
#endif
}

#endif /* _STP_VMA_C_ */
