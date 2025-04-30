/* -*- linux-c -*-
 * Utility functions for handling haredware breakpoints (watchpoints).
 *
 * Copyright (C) 2023 by OpenResty Inc.
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_HW_BREAKPOINT_H_INCLUDED_
#define _STP_HW_BREAKPOINT_H_INCLUDED_

#ifndef IS_ERR_PCPU
/* Need to define our own IS_ERR_PCPU for Linux 6.11 and older */
#define IS_ERR_PCPU(ptr) (IS_ERR((const void *)(__force const unsigned long)(ptr)))
#endif

struct stap_hwbkpt_probe {
	bool kernel_p;

	bool registered_p;
	// registered_p = false signifies a probe that is unregistered (or failed)
	// registered_p = true signifies a probe that got registered successfully

	uint8_t atype;
	unsigned int len;

	// Symbol Names are mostly small and uniform enough
	// to justify putting const char*.
	const char * const symbol;

	const unsigned long address;
	const struct stap_probe * const probe;
};


static inline void
stap_hwbkpt_exit(struct stap_hwbkpt_probe *probes, int probe_count,
		 struct perf_event ** u_ret_array, struct perf_event * __percpu ** k_ret_array)
{
  int i;
  //Unregister hwbkpt probes.
  for (i=0; i<probe_count; i++) {
    struct stap_hwbkpt_probe *skp = & probes[i];
    if (unlikely(!skp->registered_p)) continue;
    if (skp->kernel_p) {
      unregister_wide_hw_breakpoint(k_ret_array[i]);
    } else {
      unregister_hw_breakpoint(u_ret_array[i]); // dual to register_user_hw_breakpoint
    }
    skp->registered_p = false;
  }
}



static inline int
stap_hwbkpt_init(perf_overflow_handler_t triggered, struct stap_hwbkpt_probe *probes, int probe_count,
		 struct perf_event_attr *probe_array,
		 struct perf_event ** u_ret_array, struct perf_event * __percpu ** k_ret_array,
		 const char **probe_point_ptr)
{
  int rc = 0;
  int i;

  if (unlikely(probe_count == 0))  /* do nothing */
    return 0;

  for (i=0; i<probe_count; i++) {
    struct stap_hwbkpt_probe *skp = & probes[i];
    struct perf_event_attr *hp = & probe_array[i];
    void *addr = (void *) skp->address;
    const char *hwbkpt_symbol_name = addr ? NULL : skp->symbol;
    hw_breakpoint_init(hp);
    if (addr)
      hp->bp_addr = (unsigned long) addr;
    else {
      hp->bp_addr = kallsyms_lookup_name(hwbkpt_symbol_name);
      if (!hp->bp_addr) {
	_stp_error("Probe %s registration failed: invalid symbol '%s' ",
		   skp->probe->pp, hwbkpt_symbol_name);
	rc = -EINVAL;
	skp->registered_p = false;
	break;
      }
    }
    hp->bp_type = skp->atype;

    // Convert actual len to bp len.
    switch(skp->len) {
    case 1:
      hp->bp_len = HW_BREAKPOINT_LEN_1;
      break;
    case 2:
      hp->bp_len = HW_BREAKPOINT_LEN_2;
      break;
    case 3:
    case 4:
      hp->bp_len = HW_BREAKPOINT_LEN_4;
      break;
    case 5:
    case 6:
    case 7:
    case 8:
    default: // XXX: could instead reject
      hp->bp_len = HW_BREAKPOINT_LEN_8;
      break;
    }

    *probe_point_ptr = skp->probe->pp; // for error messages

#ifdef STAPCONF_HW_BREAKPOINT_CONTEXT
    if (skp->kernel_p) {
#ifdef DEBUG_PROBES
      pr_warn("%s:%d: registering kernel-mode hw breakpoint at %#lx\n",
	      __func__, __LINE__, (unsigned long) hp->bp_addr);
#endif
      k_ret_array[i] = register_wide_hw_breakpoint(hp, triggered, NULL);
      if (IS_ERR_PCPU(k_ret_array[i]))
	{
	  _stp_error("Hwbkpt probe %s: registration error [man warning::pass5] %d, addr %#lx, name %s",
		     skp->probe->pp, rc, (unsigned long) addr, hwbkpt_symbol_name);
	}
      else
	skp->registered_p = true;
    } else {
      if (likely(_stp_target > 0)) {
	struct task_struct *tsk;
	rcu_read_lock();
	tsk = get_pid_task(find_vpid(_stp_orig_target), PIDTYPE_PID);
	rcu_read_unlock();
	if (unlikely(tsk == NULL)) {
	  u_ret_array[i] = ERR_PTR(-ESRCH);
	} else {
#ifdef DEBUG_PROBES
	  pr_warn("%s:%d: registering user hw breakpoint for pid %d (%s) at addr %#lx\n",
		  __func__, __LINE__, _stp_target, tsk->comm,
		  (unsigned long) hp->bp_addr);
#endif
	  u_ret_array[i] = register_user_hw_breakpoint(hp, triggered, NULL, tsk);
	  if (IS_ERR(u_ret_array[i]))
	    {
	      _stp_error("Hwbkpt probe %s: registration error [man warning::pass5] %d, addr %#lx, name %s",
			 skp->probe->pp, rc, (unsigned long) addr, hwbkpt_symbol_name);
	    }
	  else
	    skp->registered_p = true;
	  put_task_struct(tsk);
	}  /* tsk != NULL */
      } else {  /* _stp_target <= 0 */
	u_ret_array[i] = ERR_PTR(-ESRCH);
      }
    }  /* !skp->kernel_p */
#else  /* !defined(STAPCONF_HW_BREAKPOINT_CONTEXT) */
    if (unlikely(!skp->kernel_p))
      u_ret_array[i] = ERR_PTR(-ENOTSUP);
    else {
      k_ret_array[i] = register_wide_hw_breakpoint(hp, triggered);
      if (IS_ERR_PCPU(k_ret_array[i]))
	{
	  _stp_error("Hwbkpt probe %s: registration error [man warning::pass5] %d, addr %#lx, name %s",
		     skp->probe->pp, rc, (unsigned long) addr, hwbkpt_symbol_name);
	}
      else
	skp->registered_p = true;
    }
#endif
  } // for loop

  if (unlikely(rc)) {
    stap_hwbkpt_exit(probes, probe_count, u_ret_array, k_ret_array);
  }
  
  return rc;
}


#endif  /* _STP_HW_BREAKPOINT_H_INCLUDED_ */
