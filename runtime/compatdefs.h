/* Compatibility definitions for older kernels.
 * Copyright (C) 2010 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_COMPAT_H_ /* -*- linux-c -*- */
#define _STP_COMPAT_H_

#if defined(CONFIG_COMPAT)
#include <linux/thread_info.h>


/* _stp_is_compat_task - returns true if this is a 32-on-64 bit user task.
   Note that some kernels/architectures define a function called
   is_compat_task(), but that just tests for being inside a 32bit compat
   syscall. We want to test whether the current task is a 32 bit compat
   task itself.*/
static inline int _stp_is_compat_task2(struct task_struct* tsk)
{
/* x86_64 has a different flag name from all other arches and s390... */
#if defined (__x86_64__) && defined(TIF_IA32)
  return test_tsk_thread_flag(tsk, TIF_IA32);
#elif defined (__x86_64__) /* post TIF_IA32 */
#if MM_CONTEXT_UPROBE_IA32 != 0
  return (tsk->mm && (tsk->mm->context.flags & MM_CONTEXT_UPROBE_IA32));
#else
  return (tsk->mm && (test_bit(MM_CONTEXT_UPROBE_IA32, &tsk->mm->context.flags)));
#endif
#elif defined(__s390__) || defined(__s390x__)
  return test_tsk_thread_flag(tsk, TIF_31BIT);  
#elif defined (__mips__) && !defined(TIF_32BIT)
  #ifdef CONFIG_MIPS32_O32
  return test_tsk_thread_flag(tsk, TIF_32BIT_REGS);    
  #elif defined(CONFIG_MIPS32_N32)
  return test_tsk_thread_flag(tsk, TIF_32BIT_ADDR);      
  #endif
#elif defined (__aarch64__) || defined(__powerpc__) || defined (__riscv)
  return test_tsk_thread_flag(tsk, TIF_32BIT);
#else
#error architecture not supported, no TIF_32BIT flag?
#endif
}
#else

static inline int _stp_is_compat_task2(struct task_struct* tsk)
{
  return 0;
}

#endif /* CONFIG_COMPAT */

static inline int _stp_is_compat_task(void)
{
  return _stp_is_compat_task2(current);
}



/* task_pt_regs is used in some core tapset functions, so try to make
 * sure something sensible is defined. task_pt_regs is required for
 * the tracehook interface api so is normally defined already.
 */
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <linux/sched.h>
#if defined(STAPCONF_LINUX_SCHED_HEADERS)
#include <linux/sched/task_stack.h>
#endif

#if !defined(task_pt_regs)
#if defined(__powerpc__)
#define task_pt_regs(tsk)       ((struct pt_regs *)(tsk)->thread.regs)
#endif
#if defined(__x86_64__)
#define task_pt_regs(tsk)	((struct pt_regs *)(tsk)->thread.rsp0 - 1)
#endif
#if defined(__ia64__)
/* pre-commit 6450578f32 */
#define task_pt_regs(tsk)	ia64_task_regs(tsk)
#endif
#endif

/* Always use _stp_current_pt_regs() in tapset/runtime code to make sure
   the returned user pt_regs are sane. */
#define _stp_current_pt_regs()	(current->mm ? task_pt_regs(current) : NULL)

/* Whether all user registers are valid. If not the pt_regs needs,
 * architecture specific, scrubbing before usage (in the unwinder).
 * XXX Currently very simple heuristics, just check arch. Should
 * user task and user pt_regs state.
 *
 * See arch specific "scrubbing" code in runtime/unwind/<arch>.h
 */
static inline int _stp_task_pt_regs_valid(struct task_struct *task,
					  struct pt_regs *uregs)
{
/* It would be nice to just use syscall_get_nr(task, uregs) < 0
 * but that might trigger false negatives or false positives
 * (bad syscall numbers or syscall tracing being in effect).
 */
#if defined(__i386__)
  return 1; /* i386 has so little registers, all are saved. */
#elif defined(__x86_64__)
  return 0;
#endif
  return 0;
}


#ifndef STAPCONF_GET_KRETPROBE
/* prior to linux commit d741bf41d7c7db4898 */
static inline struct kretprobe* get_kretprobe(struct kretprobe_instance *inst)
{
  return inst->rp;
}
#endif


#endif /* _STP_COMPAT_H_ */
