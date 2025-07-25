/* main header file for Linux
 * Copyright (C) 2005-2019 Red Hat Inc.
 * Copyright (C) 2005-2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _LINUX_RUNTIME_H_
#define _LINUX_RUNTIME_H_

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <asm/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)
#include <generated/utsrelease.h>
#else
#include <linux/vermagic.h>
#endif
#include <linux/utsname.h>
#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include "timer_compatibility.h"
#include <linux/delay.h>
#include <linux/profile.h>
#include <linux/rcupdate.h>
//#include <linux/utsrelease.h> // newer kernels only
//#include <linux/compile.h>
#ifdef STAPCONF_GENERATED_COMPILE
#include <generated/compile.h>
#endif

// PR31373: Linux 6.8 kernel removed strlcpy
// This provides equivalent to strlcpy using strscpy
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0)
#define strlcpy(dest, src, count)  ({		\
  size_t size=(count);				\
  ssize_t retval = strscpy((dest), (src), size);\
  if (retval<0)					\
    retval = size; /* was truncated */		\
  else						\
    retval++; /* count the NULL */		\
  retval;					\
})
#endif

// PR26811: Replace some declarations after set_fs() removal in kernel 5.10+.
// Should use the STP_* prefixed defines outside of STAPCONF_SET_FS.
#if defined(STAPCONF_SET_FS)
#define stp_mm_segment_t mm_segment_t
#define STP_KERNEL_DS KERNEL_DS
#define STP_USER_DS USER_DS
// XXX MM_SEG_IS_KERNEL only used #ifndef STAPCONF_SET_FS
#else
#define stp_mm_segment_t unsigned long
#define STP_KERNEL_DS 0
#define STP_USER_DS 1
#define STP_NUMERICAL_DS 1
#define MM_SEG_IS_KERNEL(seg) ((seg)==STP_KERNEL_DS)

// Required for kernel write operations:
static void *kallsyms_copy_to_kernel_nofault;
#endif

#ifndef STAPCONF_NMI_UACCESS_OKAY
/* alas. nmi_access_okay() is not exported by modern kernels */
static void *kallsyms_nmi_uaccess_okay = NULL;
#endif

static void *kallsyms_switch_task_namespaces = NULL;
static void *kallsyms_unshare_nsproxy_namespaces = NULL;
static void *kallsyms_free_nsproxy = NULL;
static void *kallsyms_proc_ns_file = NULL;

/* A fallthrough; macro to let the runtime survive -Wimplicit-fallthrough=5 */
/* from <linux/compiler_attribute.h> */
#ifndef fallthrough
#if __GNUC__ < 5
# define fallthrough                    do {} while (0)  /* fallthrough */
#else
#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif
#endif
#endif

#include <linux/user_namespace.h>

#if !defined (CONFIG_DEBUG_FS)  && !defined (CONFIG_DEBUG_FS_MODULE)
#error "DebugFS is required and was not found in the kernel."
#endif

#if defined(STAPCONF_UDELAY_SIMPLE_EXPORTED)
#undef udelay
#define udelay(x) udelay_simple(x)
#elif defined(STAPCONF_UDELAY_SIMPLE)
#undef udelay
static void *kallsyms_udelay_simple;
typedef typeof(&udelay_simple) udelay_simple_fn;
#define udelay(x) void_ibt_wrapper((* (udelay_simple_fn)(kallsyms_udelay_simple))((x)))
#endif

#ifndef clamp
#define clamp(val, low, high)     min(max(low, val), high)
#endif

#ifndef clamp_t
#define clamp_t(type, val, low, high)   min_t(type, max_t(type, low, val), high)
#endif

static void _stp_dbug (const char *func, int line, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));
static void _stp_error (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void _stp_warn (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

static void _stp_exit(void);


#ifdef STAPCONF_HLIST_4ARGS
#define stap_hlist_for_each_entry(a,b,c,d) hlist_for_each_entry(a,b,c,d)
#define stap_hlist_for_each_entry_rcu(a,b,c,d) hlist_for_each_entry_rcu(a,b,c,d)
#define stap_hlist_for_each_entry_safe(a,b,c,d,e) hlist_for_each_entry_safe(a,b,c,d,e)
#else
#define stap_hlist_for_each_entry(a,b,c,d) (void) b; hlist_for_each_entry(a,c,d)
#define stap_hlist_for_each_entry_rcu(a,b,c,d) (void) b; hlist_for_each_entry_rcu(a,c,d)
#define stap_hlist_for_each_entry_safe(a,b,c,d,e) (void) b; hlist_for_each_entry_safe(a,c,d,e)
#endif

#ifndef preempt_enable_no_resched
#ifdef CONFIG_PREEMPT_COUNT
#define preempt_enable_no_resched() do { \
	barrier();           \
	preempt_count_dec(); \
	} while (0)
#else
#define preempt_enable_no_resched() barrier()
#endif
#endif

#ifndef rcu_dereference_sched
#define rcu_dereference_sched(p) rcu_dereference(p)
#endif

/* unprivileged user support */

#ifdef STAPCONF_TASK_UID
#define STP_CURRENT_EUID (current->euid)
#else
#if defined(CONFIG_USER_NS) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0))
#ifndef STAPCONF_FROM_KUID_MUNGED
#define from_kuid_munged(user_ns, uid) ((uid))
#define from_kgid_munged(user_ns, gid) ((gid))
#endif /* !STAPCONF_FROM_KUID_MUNGED */
#define STP_CURRENT_EUID (from_kuid_munged(current_user_ns(), task_euid(current)))
#else
#define STP_CURRENT_EUID (task_euid(current))
#endif
#endif

#define is_myproc() (STP_CURRENT_EUID == _stp_uid)

/* Check whether the required privilege level has been attained. */
#define STP_PRIVILEGE_CONTAINS(actualPrivilege, requiredPrivilege) ( \
  ((actualPrivilege) & (requiredPrivilege)) == (requiredPrivilege) \
)

/* Translate user privilege mask to text. */
static const char *privilege_to_text (int p) {
  if (STP_PRIVILEGE_CONTAINS (p, STP_PR_STAPDEV)) return "stapdev";
  if (STP_PRIVILEGE_CONTAINS (p, STP_PR_STAPSYS)) return "stapsys";
  if (STP_PRIVILEGE_CONTAINS (p, STP_PR_STAPUSR)) return "stapusr";
  return "unknown";
}

#if STP_PRIVILEGE_CONTAINS (STP_PRIVILEGE, STP_PR_STAPDEV) || \
    STP_PRIVILEGE_CONTAINS (STP_PRIVILEGE, STP_PR_STAPSYS)
#define assert_is_myproc() do {} while (0)
#else
#define assert_is_myproc() do { \
  if (! is_myproc()) { \
    snprintf (CONTEXT->error_buffer, MAXSTRINGLEN, STAP_MSG_RUNTIME_H_01, \
             current->tgid, STP_CURRENT_EUID); \
    CONTEXT->last_error = CONTEXT->error_buffer; \
    goto out; \
  } } while (0)
#endif

#include "debug.h"

/* atomic globals */
static atomic_t _stp_transport_failures = ATOMIC_INIT (0);

static struct
{
	atomic_t ____cacheline_aligned_in_smp seq;
} _stp_seq = { ATOMIC_INIT (0) };

#define _stp_seq_inc() (atomic_inc_return(&_stp_seq.seq))

/* dwarf unwinder only tested so far on arm, i386, x86_64, ppc64 and s390x.
   Only define STP_USE_DWARF_UNWINDER when STP_NEED_UNWIND_DATA,
   as set through a pragma:unwind in one of the [u]context-unwind.stp
   functions. */
#if (defined(__arm__) || defined(__i386__) || defined(__x86_64__) || defined(__powerpc64__)) || defined (__s390x__) || defined(__aarch64__) || defined(__mips__) || defined(__riscv)
#ifdef STP_NEED_UNWIND_DATA
#ifndef STP_USE_DWARF_UNWINDER
#define STP_USE_DWARF_UNWINDER
#endif
#endif
#endif

// PR26074: Obtain kallsyms_*() addresses from relocations,
// call via wrapper in runtime/sym.c:
#if !defined(STAPCONF_KALLSYMS_LOOKUP_NAME_EXPORTED)
static void *_stp_kallsyms_lookup_name;
#endif
// PR11514: kallsyms_on_each_symbol() should not be used on PPC64.
#ifndef CONFIG_PPC64
#define STAPCONF_KALLSYMS_ON_EACH_SYMBOL
#if !defined(STAPCONF_KALLSYMS_ON_EACH_SYMBOL_EXPORTED)
// Not static, since it is linked into kprobes.c:
void *_stp_kallsyms_on_each_symbol;
#endif
#endif

// No export check and gates.  This one seems simply like a non-export.
// Not static, since it is linked into kprobes.c:
void *_stp_module_kallsyms_on_each_symbol;

// PR13489, inode-uprobes sometimes lacks the necessary SYMBOL_EXPORT's.
#if !defined(STAPCONF_TASK_USER_REGSET_VIEW_EXPORTED)
static void *kallsyms_task_user_regset_view;
#endif
#if !defined(STAPCONF_UPROBE_REGISTER_EXPORTED)
static void *kallsyms_uprobe_register;
#endif
#if !defined(STAPCONF_UPROBE_UNREGISTER_EXPORTED)
static void *kallsyms_uprobe_unregister;
#endif
#if !defined(STAPCONF_UPROBE_GET_SWBP_ADDR_EXPORTED)
static void *kallsyms_uprobe_get_swbp_addr;
#endif

/* task_work functions lack the necessary SYMBOL_EXPORT's */
#if !defined(STAPCONF_TASK_WORK_ADD_EXPORTED)
static void *kallsyms_task_work_add;
#endif
#if !defined(STAPCONF_TASK_WORK_CANCEL_EXPORTED)
static void *kallsyms_task_work_cancel_fn;
#endif

#if !defined(STAPCONF_TRY_TO_WAKE_UP_EXPORTED) && !defined(STAPCONF_WAKE_UP_STATE_EXPORTED)
static void *kallsyms_wake_up_state;
#endif
#if !defined(STAPCONF_SIGNAL_WAKE_UP_STATE_EXPORTED)
static void *kallsyms_signal_wake_up_state;
#endif
#if !defined(STAPCONF_SIGNAL_WAKE_UP_EXPORTED)
static void *kallsyms_signal_wake_up;
#endif
#if !defined(STAPCONF___LOCK_TASK_SIGHAND_EXPORTED)
static void *kallsyms___lock_task_sighand;
#endif
#if !defined(STAPCONF_GET_MM_EXE_FILE_EXPORTED)
static void *kallsyms_get_mm_exe_file;
#endif

/* PR30777: Need a mechanism to temporarily turn off Intel IBT */
#ifdef CONFIG_X86_KERNEL_IBT

__noendbr u64 ibt_save(bool disable)
{
	u64 msr = 0;

	if (cpu_feature_enabled(X86_FEATURE_IBT)) {
		rdmsrl(MSR_IA32_S_CET, msr);
		if (disable)
			wrmsrl(MSR_IA32_S_CET, msr & ~CET_ENDBR_EN);
	}

	return msr;
}

__noendbr void ibt_restore(u64 save)
{
	u64 msr;

	if (cpu_feature_enabled(X86_FEATURE_IBT)) {
		rdmsrl(MSR_IA32_S_CET, msr);
		msr &= ~CET_ENDBR_EN;
		msr |= (save & CET_ENDBR_EN);
		wrmsrl(MSR_IA32_S_CET, msr);
	}
}

#define ibt_wrapper(rettype, function)  ({	\
  u64 ibt_value = ibt_save(true);		\
  rettype retval = (function);			\
  ibt_restore(ibt_value);			\
  retval;					\
})

#define void_ibt_wrapper(function)  ({		\
  u64 ibt_value = ibt_save(true);		\
  (function);					\
  ibt_restore(ibt_value);			\
  0;						\
})

#else
#define ibt_wrapper(rettype, function)  ({ (function); })
#define void_ibt_wrapper(function)  ({ (function); })
#endif

#include "access_process_vm.h"
#include "loc2c-runtime.h"

#include "alloc.c"
#include "print.h"
#include "stp_string.c"
#include "arith.c"
#include "copy.c"
#include "../regs.c"
#include "regs-ia64.c"

#if (defined(STAPCONF_UTRACE_VIA_TRACEPOINTS) || defined(STAPCONF_UTRACE_VIA_TRACEPOINTS2))
#define HAVE_TASK_FINDER
#include "task_finder.c"
#else
/* stub functionality that fails gracefully */
#include "task_finder_stubs.c"
#endif

#ifdef _HAVE_PERF_
/* Now that we've got the task_finder included (one way or another),
 * we can include the perf stuff (if needed). */
#include <linux/perf_event.h>
#include "linux/perf.h"
#endif

#include "sym.c"
#ifdef STP_PERFMON
#include "perf.c"
#endif
#include "addr-map.c"
#include "stat.c"

/* DWARF unwinder only tested so far on i386, x86_64 and ppc64.
   We only need to compile in the unwinder when both STP_NEED_UNWIND_DATA
   (set when a stap script defines pragma:unwind, as done in
   [u]context-unwind.stp) is defined and the architecture actually supports
   dwarf unwinding (as defined by STP_USE_DWARF_UNWINDER in runtime.h).  */
#ifdef STP_USE_DWARF_UNWINDER
#include "unwind.c"
#else
/* We still need unwind.h for a few structures (unwind_context and
 * unwind_cache). */
#include "unwind/unwind.h"
#endif

#ifdef module_param_cb			/* kernels >= 2.6.36 */
#define _STP_KERNEL_PARAM_ARG const struct kernel_param
#else
#define _STP_KERNEL_PARAM_ARG struct kernel_param
#endif

/* Support functions for int64_t module parameters. */
static int param_set_int64_t(const char *val, _STP_KERNEL_PARAM_ARG *kp)
{
  char *endp;
  long long ll;

  if (!val)
    return -EINVAL;

  /* simple_strtoll isn't exported... */
  if (*val == '-')
    ll = -simple_strtoull(val+1, &endp, 0);
  else
    ll = simple_strtoull(val, &endp, 0);

  if ((endp == val) || ((int64_t)ll != ll))
    return -EINVAL;

  *((int64_t *)kp->arg) = ll;
  return 0;
}

static int param_get_int64_t(char *buffer, _STP_KERNEL_PARAM_ARG *kp)
{
  return sprintf(buffer, "%lli", (long long)*((int64_t *)kp->arg));
}

#define param_check_int64_t(name, p) __param_check(name, p, int64_t)

#ifdef module_param_cb			/* kernels >= 2.6.36 */
static struct kernel_param_ops param_ops_int64_t = {
	.set = param_set_int64_t,
	.get = param_get_int64_t,
};
#endif
#undef _STP_KERNEL_PARAM_ARG

#include <linux/workqueue.h>
struct workqueue_struct *systemtap_wq;

static inline void stp_synchronize_sched(void)
{
  flush_workqueue(systemtap_wq);
#if defined(STAPCONF_SYNCHRONIZE_SCHED)
  synchronize_sched();
#elif defined(STAPCONF_SYNCHRONIZE_RCU)
  synchronize_rcu();
#elif defined(STAPCONF_SYNCHRONIZE_KERNEL)
  synchronize_kernel();
#else
#error "No implementation for stp_synchronize_sched!"
#endif
}

/************* Module Stuff ********************/


static int systemtap_kernel_module_init (void);
static void systemtap_kernel_module_exit (void);

static unsigned long stap_hash_seed; /* Init during module startup */

static int stap_init_module (void)
{
  int rc;
  /* With deliberate hash-collision-inducing data conceivably fed to
     stap, it is beneficial to add some runtime-random value to the
     map hash. */
  get_random_bytes(&stap_hash_seed, sizeof (stap_hash_seed));
  systemtap_wq = alloc_workqueue("systemtap-wq",
		WQ_UNBOUND, 0);
  if (!systemtap_wq)
    return -ENOMEM;
  rc = systemtap_kernel_module_init();
  if (rc){
    destroy_workqueue(systemtap_wq);
    return rc;
  }
  rc = _stp_transport_init();
  if (rc){
    systemtap_kernel_module_exit();
    destroy_workqueue(systemtap_wq);
  }
  return rc;
}

module_init(stap_init_module);

void stap_cleanup_module(void);
void stap_cleanup_module(void)
{
  _stp_transport_close();
  /* PR19833.  /proc/systemtap/$MODULENAME may be disposed-of here,
     due to tapset-procfs.cxx cleaning up after procfs probes (such
     as in --monitor mode).  */
  systemtap_kernel_module_exit();
  destroy_workqueue(systemtap_wq);
}

module_exit(stap_cleanup_module);

#define pseudo_atomic_cmpxchg(v, old, new) ({\
	int ret;\
	unsigned long flags;\
	local_irq_save(flags);\
	ret = atomic_read(v);\
	if (likely(ret == old))\
		atomic_set(v, new);\
	local_irq_restore(flags);\
	ret; })


#if defined(__ia64__) && defined(__GNUC__) && (__GNUC__ < 4)
/* Due to http://gcc.gnu.org/PR21838, old gcc on ia64 generates calls
   to __ia64_save_stack_nonlocal(), since our generated code uses
   __label__ constructs since PR11004.  This dummy declaration works
   around the undefined reference.  */
void __ia64_save_stack_nonlocal (void) { }
#endif

MODULE_LICENSE("GPL");

#endif /* _LINUX_RUNTIME_H_ */
