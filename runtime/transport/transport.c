/* -*- linux-c -*- 
 * transport.c - stp transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Red Hat Inc, 2005-2014
 * Copyright (C) Intel Corporation, 2006
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _TRANSPORT_TRANSPORT_C_
#define _TRANSPORT_TRANSPORT_C_

#include "transport.h"
#include "control.h"
#include "../sym.h"
#include <linux/debugfs.h>
#include <linux/namei.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#ifdef STAPCONF_LOCKDOWN_DEBUGFS
#include <linux/security.h>
#endif
#ifdef STAPCONF_514_PANIC
#include <linux/panic_notifier.h>
#endif
#include "../uidgid_compatibility.h"

static int _stp_exit_flag = 0;

static uid_t _stp_uid = 0;
static gid_t _stp_gid = 0;
static int _stp_pid = 0;
static int _stp_namespaces_pid = 0;
static int _stp_remote_id = -1;
static char _stp_remote_uri[MAXSTRINGLEN];

static atomic_t _stp_ctl_attached = ATOMIC_INIT(0);

static pid_t _stp_target = 0;
static int _stp_probes_started = 0;

#define _STP_TS_UNINITIALIZED	0
#define _STP_TS_START_CALLED	1
#define _STP_TS_START_FINISHED	2
#define _STP_TS_EXIT_CALLED	3

/* We only want to do the startup and exit sequences once, so the
 * transport state flag is atomic. */
static atomic_t _stp_transport_state = ATOMIC_INIT(_STP_TS_UNINITIALIZED);

static inline void _stp_lock_inode(struct inode *inode);
static inline void _stp_unlock_inode(struct inode *inode);

#ifndef STP_CTL_TIMER_INTERVAL
/* ctl timer interval in jiffies (default 20 ms) */
#define STP_CTL_TIMER_INTERVAL		((HZ+49)/50)
#endif

/* Defines the number of buffers allocated in control.c (which #includes
   this file) for the _stp_pool_q.  This is the number of .cmd messages
   the module can store before they have to be read by stapio.
   40 is somewhat arbitrary, 8 pre-allocated messages, 32 dynamic.  */
#define STP_DEFAULT_BUFFERS 256

#include "control.h"
#include "relay_v2.c"
#include "debugfs.c"
#include "procfs.c"
#include "control.c"

/* set default buffer parameters.  User may override these via stap -s #, and
   the runtime may auto-shrink it on low memory machines too. */
/* NB: Note default in man/stap.1.in */
static unsigned _stp_nsubbufs = 16*1024*1024 / PAGE_SIZE;
static unsigned _stp_subbuf_size = PAGE_SIZE;

/* module parameters */
static int _stp_bufsize;
module_param(_stp_bufsize, int, 0);
MODULE_PARM_DESC(_stp_bufsize, "buffer size");

/* forward declarations */
static void systemtap_module_exit(void);
static int systemtap_module_init(void);

static int _stp_module_notifier_active = 0;
static int _stp_module_notifier (struct notifier_block * nb,
                                 unsigned long val, void *data);
static struct notifier_block _stp_module_notifier_nb = {
        .notifier_call = _stp_module_notifier,
        // We used to have this set to 1 before since that is also what
        // kernel/trace/trace_kprobe.c does as well. The idea was that we should
        // be notified _after_ the kprobe infrastruture itself is notified.
        // However, that was the exact opposite of what was happening (we were
        // called _before_ kprobes). In the end, we do not have a hard
        // requirement as to being called before or after kprobes itself, so
        // just leave the default of 0. (See also PR16861).
        .priority = 0
};

static int _stp_module_panic_notifier (struct notifier_block * nb,
                                 unsigned long val, void *data);
static struct notifier_block _stp_module_panic_notifier_nb = {
        .notifier_call = _stp_module_panic_notifier,
        .priority = INT_MAX
};

static struct timer_list _stp_ctl_work_timer;



// ------------------------------------------------------------------------

// Dispatching functions to choose between procfs and debugfs variants
// of the transport.  PR26665

static int _stp_transport_fs_init(const char *module_name)
{
	// Default to using procfs
	procfs_p = 1;

        // BTW: testing the other !FOO_p first is to protect against repeated
        // invocations of this function with security_locked_down() changing
#ifdef STAPCONF_LOCKDOWN_DEBUGFS
        if (!debugfs_p && security_locked_down (LOCKDOWN_DEBUGFS)) {
                procfs_p = 1;
		dbug_trans(1, "choosing procfs_p=1\n");
        }
#endif
#ifdef STAPCONF_LOCKDOWN_KERNEL
        if (!debugfs_p && kernel_is_locked_down ("debugfs")) {
                procfs_p = 1;
		dbug_trans(1, "choosing procfs_p=1\n");
        }
#endif
#ifdef STAP_TRANS_PROCFS
        procfs_p = 1;
        debugfs_p = 0;
        dbug_trans(1, "forcing procfs_p=1\n");
#endif
#ifdef STAP_TRANS_DEBUGFS
        procfs_p = 0;
        debugfs_p = 1;
        dbug_trans(1, "forcing debugfs_p=1\n");        
#endif        
        
        if (debugfs_p)
                return _stp_debugfs_transport_fs_init(module_name);        
        if (procfs_p)
                return _stp_procfs_transport_fs_init(module_name);
        return -ENOSYS;
}

static void _stp_transport_fs_close(void)
{
        if (debugfs_p)
                _stp_debugfs_transport_fs_close();
        if (procfs_p)
                _stp_procfs_transport_fs_close();
}


static int _stp_ctl_write_fs(int type, void *data, unsigned len)
{
        if (procfs_p)
                return _stp_procfs_ctl_write_fs (type, data, len);
        if (debugfs_p)
                return _stp_debugfs_ctl_write_fs (type, data, len);
        return -ENOSYS;
}


static int _stp_register_ctl_channel_fs(void)
{
        if (debugfs_p)
                return _stp_debugfs_register_ctl_channel_fs();
        if (procfs_p)
                return _stp_procfs_register_ctl_channel_fs();

        return -ENOSYS;
}


static void _stp_unregister_ctl_channel_fs(void)
{
        if (procfs_p)
                _stp_procfs_unregister_ctl_channel_fs();
        if (debugfs_p)
                _stp_debugfs_unregister_ctl_channel_fs();
}



static struct dentry *_stp_get_module_dir(void)
{
        if (procfs_p)
                return _stp_procfs_get_module_dir();
        if (debugfs_p)
                return _stp_debugfs_get_module_dir();
        return NULL;
}



// ------------------------------------------------------------------------

/*
 *	_stp_handle_start - handle STP_START
 */

// PR17232: This might be called more than once, but not concurrently
// or reentrantly with itself, or with _stp_cleanup_and_exit.  (The
// latter case is not obvious: _stp_cleanup_and_exit could be called
// from the mutex-protected ctl message handler, so that's fine; or
// it could be called from the module cleanup function, by which time
// we know there is no ctl connection and thus no messages.  So again
// no concurrency.

static void _stp_handle_start(struct _stp_msg_start *st)
{
	int handle_startup;
#if defined(CONFIG_USER_NS)
	struct pid *_upid = NULL;
	struct task_struct *_utask = NULL;
#endif

        // protect against excessive or premature startup
	handle_startup = (atomic_cmpxchg(&_stp_transport_state,
					 _STP_TS_UNINITIALIZED,
					 _STP_TS_START_CALLED)
			  == _STP_TS_UNINITIALIZED);
	if (handle_startup) {
		dbug_trans(1, "stp_handle_start\n");

		_stp_target = st->target;
		_stp_orig_target = _stp_target;

#if defined(CONFIG_USER_NS)
                rcu_read_lock();
                _upid = find_vpid(_stp_target);
                if (_upid)
                {
                    _utask = pid_task(_upid, PIDTYPE_PID);
                    if (_utask)
                    {
                        #ifdef DEBUG_UPROBES
                        _stp_dbug(__FUNCTION__,__LINE__, "translating vpid %d to pid %d\n", _stp_target, _utask->pid);
                        #endif
                        _stp_target = _utask->pid;
                    }
                }

                #ifdef DEBUG_UPROBES
                if (!_upid || !_utask)
                    _stp_dbug(__FUNCTION__,__LINE__, "cannot map pid %d to host namespace pid\n", _stp_target);
                #endif

                rcu_read_unlock();
#endif

#ifdef STAP_MODULE_INIT_HOOK
		st->res = STAP_MODULE_INIT_HOOK();
		if (st->res != 0) {
			_stp_error ("Failed to run STAP_MODULE_INIT_HOOK "
				    "(%d)\n", st->res);
		} else {
#else
		{
#endif
			st->res = systemtap_module_init();
		}

		if (st->res == 0) {
			_stp_probes_started = 1;

                        /* Register the module notifier ... */
                        /* NB: but not if the module_init stuff
                           failed: something nasty has happened, and
                           we want no further probing started.  PR16766 */
                        if (!_stp_module_notifier_active) {
                                int rc = register_module_notifier(& _stp_module_notifier_nb);
                                if (rc == 0)
                                        _stp_module_notifier_active = 1;
                                else
                                        _stp_warn ("Cannot register module notifier (%d)\n", rc);
                        }
                }

		/* Called from the user context in response to a proc
		   file write (in _stp_ctl_write_cmd), so may notify
		   the reader directly. */
		_stp_ctl_send_notify(STP_START, st, sizeof(*st));

		/* Register the panic notifier. */
		atomic_notifier_chain_register(&panic_notifier_list, &_stp_module_panic_notifier_nb);

		/* If we're here, we know that _stp_transport_state
		 * is _STP_TS_START_CALLED. Go ahead and set it to
		 * _STP_TS_START_FINISHED. */
		atomic_set(&_stp_transport_state, _STP_TS_START_FINISHED);
	}
}

#if defined(CONFIG_KALLSYMS) && !defined(STAPCONF_KALLSYMS_LOOKUP_NAME_EXPORTED)
unsigned _stp_need_kallsyms_stext;
#endif

// PR26074: Most kallsyms_lookup_name() calls are done in appropriate
// subsystem init() functions, but a handful are done at module load
// time:
static int _stp_handle_kallsyms_lookups(void)
{
  const char *symname = NULL;
  might_sleep();

#define NEW_KALLSYMS_SYM(name) \
  kallsyms_ ## name = (void *) kallsyms_lookup_name (#name); \
  cond_resched(); \
  if (unlikely(kallsyms_ ## name == NULL)) { \
    /* ignore */ \
    ; \
  }

NEW_KALLSYMS_SYM(switch_task_namespaces)
NEW_KALLSYMS_SYM(unshare_nsproxy_namespaces)
NEW_KALLSYMS_SYM(proc_ns_file)
NEW_KALLSYMS_SYM(free_nsproxy)

#undef KALLSYMS_LOOKUP_SYM

#ifndef STAPCONF_NMI_UACCESS_OKAY
  kallsyms_nmi_uaccess_okay = (void*) kallsyms_lookup_name ("nmi_uaccess_okay");
  cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
  if (kallsyms_nmi_uaccess_okay == NULL) {
    /* Not finding this symbol is non-fatal for non-X86 architectures
     * since nmi_uaccess_okay() does not do anything for non-X86 archs.
     * Also, we don't treat it as fatal even on X86 since nmi_uaccess_okay()
     * is a relatively new addition in upstream kernel commit 4012e77a90
     * since Aug 2018.
     */
    ;
  }
#endif  /* STAPCONF_NMI_UACCESS_OKAY */

#if !defined(STAPCONF_SET_FS)
  /* PR26811, missing copy_to_kernel_nofault symbol-export */
  kallsyms_copy_to_kernel_nofault = (void*) kallsyms_lookup_name ("copy_to_kernel_nofault");
  cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
  /* Not finding this symbol is non-fatal. Kernel writes will fault safely: */
  if (kallsyms_copy_to_kernel_nofault == NULL) {
    ;
  }
#endif
  /* PR13489, missing inode-uprobes symbol-export workaround */
#if !defined(STAPCONF_TASK_USER_REGSET_VIEW_EXPORTED) /* RHEL5 era utrace */
        kallsyms_task_user_regset_view = (void*) kallsyms_lookup_name ("task_user_regset_view");
        cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
        /* There exist interesting kernel versions without task_user_regset_view(), like ARM before 3.0.
           For these kernels, uprobes etc. are out of the question, but plain kernel stap works fine.
           All we have to accomplish is have the loc2c runtime code compile.  For that, it's enough
           to leave this pointer zero. */
        if (kallsyms_task_user_regset_view == NULL) {
                ;
        }
#endif
#if defined(CONFIG_UPROBES) // i.e., kernel-embedded uprobes
#if !defined(STAPCONF_UPROBE_REGISTER_EXPORTED)
        kallsyms_uprobe_register = (void*) kallsyms_lookup_name ("uprobe_register");
        cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
        if (kallsyms_uprobe_register == NULL) {
                kallsyms_uprobe_register = (void*) kallsyms_lookup_name ("register_uprobe");
                cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
        }
        if (kallsyms_uprobe_register == NULL) {
                _stp_error("Can't resolve uprobe_register!");
                goto err0;
        }
#endif
#if !defined(STAPCONF_UPROBE_UNREGISTER_EXPORTED)
        kallsyms_uprobe_unregister = (void*) kallsyms_lookup_name ("uprobe_unregister");
        cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
#if defined(STAPCONF_PR32194_UPROBE_REGISTER_UNREGISTER)
        if (kallsyms_uprobe_unregister == NULL) {
                kallsyms_uprobe_unregister = (void*) kallsyms_lookup_name ("uprobe_unregister_nosync");
                cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
        }
#endif
        if (kallsyms_uprobe_unregister == NULL) {
                kallsyms_uprobe_unregister = (void*) kallsyms_lookup_name ("unregister_uprobe");
                cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
        }
        if (kallsyms_uprobe_unregister == NULL) {
                _stp_error("Can't resolve uprobe_unregister!");
                goto err0;
        }
#endif
#if !defined(STAPCONF_UPROBE_GET_SWBP_ADDR_EXPORTED)
        kallsyms_uprobe_get_swbp_addr = (void*) kallsyms_lookup_name ("uprobe_get_swbp_addr");
        cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
        if (kallsyms_uprobe_get_swbp_addr == NULL) {
                _stp_error("Can't resolve uprobe_get_swbp_addr!");
                goto err0;
        }
#endif
#if !defined(STAPCONF_GET_MM_EXE_FILE_EXPORTED) && (defined(get_file_rcu) || LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0))
        kallsyms_get_mm_exe_file = (void*) kallsyms_lookup_name ("get_mm_exe_file");
        cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
        if (kallsyms_get_mm_exe_file == NULL) {
                _stp_error("Can't resolve get_mm_exe_file!");
                goto err0;
        }
#endif
#endif
#if defined(CONFIG_KALLSYMS) && !defined(STAPCONF_KALLSYMS_LOOKUP_NAME_EXPORTED)
        {
                uint64_t address;
                if (_stp_need_kallsyms_stext) {
                        address = kallsyms_lookup_name("_stext");
                        cond_resched();  /* kallsyms_lookup_name is expensive; avoid blocking here */
                        _stp_set_stext(address);
                        _stp_need_kallsyms_stext = 0;
                }
        }
#endif
        return 0;
err0:
        return -1;
}

// _stp_cleanup_and_exit: handle STP_EXIT and cleanup_module
//
/* We need to call it both times because we want to clean up properly */
/* when someone does /sbin/rmmod on a loaded systemtap module. */
static void _stp_cleanup_and_exit(int send_exit)
{
	int handle_exit;

        // protect against excessive or premature cleanup
	handle_exit = (atomic_cmpxchg(&_stp_transport_state,
				      _STP_TS_START_FINISHED,
				      _STP_TS_EXIT_CALLED)
		       == _STP_TS_START_FINISHED);
	if (handle_exit) {
		int failures;

                dbug_trans(1, "cleanup_and_exit (%d)\n", send_exit);

	        /* Unregister the module notifier. */
	        if (_stp_module_notifier_active) {
                        int rc = unregister_module_notifier(& _stp_module_notifier_nb);
                        if (rc)
                                _stp_warn("module_notifier unregister error %d", rc);
	                _stp_module_notifier_active = 0;
                        stp_synchronize_sched(); // paranoia: try to ensure no further calls in progress
	        }

		_stp_exit_flag = 1;

		if (_stp_probes_started) {
			dbug_trans(1, "calling systemtap_module_exit\n");
			/* tell the stap-generated code to unload its probes, etc */
			systemtap_module_exit();
			dbug_trans(1, "done with systemtap_module_exit\n");
		}

		failures = atomic_read(&_stp_transport_failures);
		if (failures)
			_stp_warn("There were %d transport failures. Try stap -s to increase the buffer size from %d.\n", failures, _stp_bufsize);

		dbug_trans(1, "*** calling _stp_transport_data_fs_stop ***\n");
		_stp_transport_data_fs_stop();

		dbug_trans(1, "ctl_send STP_EXIT\n");
		if (send_exit) {
			/* send_exit is only set to one if called from
			   _stp_ctl_write_cmd() in response to a write
			   to the proc cmd file, so in user context. It
			   is safe to immediately notify the reader.  */
			_stp_ctl_send_notify(STP_EXIT, NULL, 0);
		}
		dbug_trans(1, "done with ctl_send STP_EXIT\n");

		/* Unregister the panic notifier. */
		atomic_notifier_chain_unregister(&panic_notifier_list, &_stp_module_panic_notifier_nb);
	}
}


// Coming from script type sources, e.g. the exit() tapset function:
// consists of sending a message to staprun/stapio, and definitely
// NOT calling _stp_cleanup_and_exit(), since that function requires
// a more user context to run from.
static void _stp_request_exit(void)
{
	static int called = 0;
	if (!called) {
		/* we only want to do this once; XXX: why? what's the harm? */
		called = 1;
		dbug_trans(1, "ctl_send STP_REQUEST_EXIT\n");
		/* Called from the timer when _stp_exit_flag has been
		   been set. So safe to immediately notify any readers. */
		_stp_ctl_send_notify(STP_REQUEST_EXIT, NULL, 0);
		dbug_trans(1, "done with ctl_send STP_REQUEST_EXIT\n");
	}
}

/*
 * Called when stapio closes the control channel.
 */
static void _stp_detach(void)
{
	dbug_trans(1, "detach\n");
	_stp_pid = 0;
	_stp_namespaces_pid = 0;
	_stp_target_mnt_ns_fd = -1;
	_stp_orig_mnt_ns_fd = -1;
	_stp_fs_struct_unshared = false;

	if (!_stp_exit_flag)
		_stp_transport_data_fs_overwrite(1);

        del_timer_sync(&_stp_ctl_work_timer);
	wake_up_interruptible(&_stp_ctl_wq);
}


static void _stp_ctl_work_callback(stp_timer_callback_parameter_t unused);

/*
 * Called when stapio opens the control channel.
 */
static void _stp_attach(void)
{
	dbug_trans(1, "attach\n");
	_stp_pid = current->pid;
	if (_stp_namespaces_pid < 1)
		_stp_namespaces_pid = _stp_pid;
	_stp_transport_data_fs_overwrite(0);

	timer_setup(&_stp_ctl_work_timer, _stp_ctl_work_callback, 0);
	_stp_ctl_work_timer.expires = jiffies + STP_CTL_TIMER_INTERVAL;
	add_timer(&_stp_ctl_work_timer);
}

/*
 *	_stp_ctl_work_callback - periodically check for IO or exit
 *	This IO comes from control messages like system(), warn(),
 *	that could potentially have been send from krpobe context,
 *	so they don't immediately trigger a wake_up of _stp_ctl_wq.
 *	This is run by a kernel thread and may NOT sleep, but it
 *	may call wake_up_interruptible on _stp_ctl_wq to notify
 *	any readers, or send messages itself that are immediately
 *	notified. Reschedules itself if someone is still attached
 *	to the cmd channel.
 */
static void _stp_ctl_work_callback(stp_timer_callback_parameter_t unused)
{
	int do_io = 0;
	unsigned long flags;
	struct context* __restrict__ c = NULL;

	/* Prevent probe reentrancy while grabbing probe-used locks.  */
	c = _stp_runtime_entryfn_get_context();

	stp_spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
	if (!list_empty(&_stp_ctl_ready_q))
		do_io = 1;
	stp_spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);

	_stp_runtime_entryfn_put_context(c);

	if (do_io)
		wake_up_interruptible(&_stp_ctl_wq);

	/* if exit flag is set AND we have finished with systemtap_module_init() */
	if (unlikely(_stp_exit_flag && _stp_probes_started))
		_stp_request_exit();
	if (atomic_read(& _stp_ctl_attached))
                mod_timer (&_stp_ctl_work_timer, jiffies + STP_CTL_TIMER_INTERVAL);
}

/**
 *	_stp_transport_close - close ctl and relayfs channels
 *
 *	This is called automatically when the module is unloaded.
 *     
 */
static void _stp_transport_close(void)
{
	dbug_trans(1, "%d: ************** transport_close *************\n",
		   current->pid);
	_stp_cleanup_and_exit(0);
	_stp_unregister_ctl_channel();
	_stp_print_cleanup(); /* Requires the transport, so free this first */
	_stp_transport_fs_close();
#ifdef STAP_MODULE_EXIT_HOOK
	STAP_MODULE_EXIT_HOOK();
#endif
	_stp_mem_debug_done();

	dbug_trans(1, "---- CLOSED ----\n");
}

/**
 * _stp_transport_init() is called from the module initialization.
 *   It does the bare minimum to exchange commands with staprun 
 */
static int _stp_transport_init(void)
{
	int ret;

	dbug_trans(1, "transport_init\n");
#ifdef STAPCONF_TASK_UID
	_stp_uid = current->uid;
	_stp_gid = current->gid;
#else
#if defined(CONFIG_USER_NS) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0))
	_stp_uid = from_kuid_munged(current_user_ns(), current_uid());
	_stp_gid = from_kgid_munged(current_user_ns(), current_gid());
#else
	_stp_uid = current_uid();
	_stp_gid = current_gid();
#endif
#endif

        /* PR26074: kallsyms_lookup_name() invocations are now performed later.
           Just in case, we should init some of the variables to NULL. */
#if !defined(STAPCONF_TASK_USER_REGSET_VIEW_EXPORTED)
        kallsyms_task_user_regset_view = NULL;
#endif
#if defined(CONFIG_UPROBES) // i.e., kernel-embedded uprobes
#if !defined(STAPCONF_UPROBE_REGISTER_EXPORTED)
        kallsyms_uprobe_register = NULL;
#endif
#if !defined(STAPCONF_UPROBE_UNREGISTER_EXPORTED)
        kallsyms_uprobe_unregister = NULL;
#endif
#if !defined(STAPCONF_UPROBE_GET_SWBP_ADDR_EXPORTED)
        kallsyms_uprobe_get_swbp_addr = NULL;
#endif
#endif
#if !defined(STAPCONF_KALLSYMS_LOOKUP_NAME_EXPORTED)
        _stp_kallsyms_lookup_name = NULL;
#endif
#if defined(STAPCONF_KALLSYMS_ON_EACH_SYMBOL) && !defined(STAPCONF_KALLSYMS_ON_EACH_SYMBOL_EXPORTED)
        _stp_kallsyms_on_each_symbol = NULL;
#endif
        _stp_module_kallsyms_on_each_symbol = NULL;
#if defined(CONFIG_KALLSYMS) && !defined(STAPCONF_KALLSYMS_LOOKUP_NAME_EXPORTED)
        _stp_need_kallsyms_stext = 0;
#endif

        if (_stp_bufsize == 0) { // option not specified?
		struct sysinfo si;
                long _stp_bufsize_avail;
                si_meminfo(&si);
                _stp_bufsize_avail = (long)((si.freeram + si.bufferram) / 4 / num_online_cpus())
                        << PAGE_SHIFT; // limit to quarter of free ram total, divided between cpus
                if ((_stp_nsubbufs * _stp_subbuf_size) > _stp_bufsize_avail) {
                        _stp_bufsize = max_t (int, 1, _stp_bufsize_avail / 1024 / 1024);
                        dbug_trans(1, "Shrinking default _stp_bufsize to %d MB/cpu due to low free memory\n", _stp_bufsize);
                }
        }      
        
	if (_stp_bufsize) { // overridden by user or by si_meminfo heuristic?
		long size = _stp_bufsize * 1024 * 1024;
		_stp_subbuf_size = 1 << PAGE_SHIFT; // XXX: allow this to be bumped up too?
		_stp_nsubbufs = size / _stp_subbuf_size;
	}

        _stp_bufsize = (long)_stp_subbuf_size * (long)_stp_nsubbufs / 1024 / 1024; // for diagnostics later
        dbug_trans(1, "Using %d subbufs of size %d * %d CPUs\n",
                   _stp_nsubbufs, _stp_subbuf_size, num_online_cpus());
        
	ret = _stp_transport_fs_init(THIS_MODULE->name);
	if (ret)
		goto err0;

	/* create control channel */
	ret = _stp_register_ctl_channel();
	if (ret < 0)
		goto err1;

	/* create print buffers */
	ret = _stp_print_init();
	if (ret < 0) {
		errk("%s: can't create print buffers!", THIS_MODULE->name);
		goto err2;
	}

	/* set _stp_module_self dynamic info */
	ret = _stp_module_update_self();
	if (ret < 0) {
		errk("%s: can't update dynamic info!", THIS_MODULE->name);
		goto err3;
	}

	/* start transport */
	_stp_transport_data_fs_start();

        /* Signal stapio to send us STP_START back.
           This is an historic convention. This was called
	   STP_TRANSPORT_INFO and had a payload that described the
	   transport buffering, this is no longer the case.
	   Called during module initialization time, so safe to immediately
	   notify reader we are ready.  */
	_stp_ctl_send_notify(STP_TRANSPORT, NULL, 0);

	dbug_trans(1, "returning 0...\n");
	return 0;

err3:
	_stp_print_cleanup();
err2:
	_stp_unregister_ctl_channel();
err1:
	_stp_transport_fs_close();
err0:
	return ret;
}

static inline void _stp_lock_inode(struct inode *inode)
{
#ifdef STAPCONF_INODE_RWSEM
	inode_lock(inode);
#else
	might_sleep();
	mutex_lock(&inode->i_mutex);
#endif
}

static inline void _stp_unlock_inode(struct inode *inode)
{
#ifdef STAPCONF_INODE_RWSEM
	inode_unlock(inode);
#else
	mutex_unlock(&inode->i_mutex);
#endif
}



/* NB: Accessed from tzinfo.stp tapset */
static uint64_t tz_gmtoff;
static char tz_name[MAXSTRINGLEN];

static void _stp_handle_tzinfo (struct _stp_msg_tzinfo* tzi)
{
        tz_gmtoff = tzi->tz_gmtoff;
        strlcpy (tz_name, tzi->tz_name, MAXSTRINGLEN);
        /* We may silently truncate the incoming string,
         * for example if MAXSTRINGLEN < STP_TZ_NAME_LEN-1 */
}


static int32_t _stp_privilege_credentials = 0;

static void _stp_handle_privilege_credentials (struct _stp_msg_privilege_credentials* pc)
{
  _stp_privilege_credentials = pc->pc_group_mask;
}

static void _stp_handle_remote_id (struct _stp_msg_remote_id* rem)
{
  _stp_remote_id = (int64_t) rem->remote_id;
  strlcpy(_stp_remote_uri, rem->remote_uri, min(STP_REMOTE_URI_LEN,MAXSTRINGLEN));
}

static void _stp_handle_namespaces_pid (struct _stp_msg_ns_pid *nspid)
{
  if (nspid->target > 0) {
    _stp_namespaces_pid = (int) nspid->target;
  }
}

static void _stp_handle_mnt_ns_fds (struct _stp_msg_mnt_ns_fds *nsfds)
{
  if (nsfds->target_fd > 0) {
    _stp_target_mnt_ns_fd = nsfds->target_fd;
    _stp_orig_mnt_ns_fd = nsfds->orig_fd;
  }
}


#endif /* _TRANSPORT_C_ */
