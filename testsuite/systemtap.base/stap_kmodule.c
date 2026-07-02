/* -*- linux-c -*- 
 * Systemtap Test Module 1
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/version.h>

/*
 * The purpose of this module is to provide a function that can be
 * triggered from user context via a /proc file.  Systemtap scripts
 * set probes on the function and run tests to see if the expected
 * output is received. This is better than using the kernel's modules
 * because kernel internals frequently change.
 */

/************ Below are the functions to create this module ************/

struct timer_list simple_timer;
static const int timer_interval = 5;

static void simple_timer_function(struct timer_list *timer)
{
	static int count;
	mod_timer (&simple_timer, jiffies + ( msecs_to_jiffies(timer_interval)));
	if (count) {
	  count = 0;
	}
	else {
	  count = 1;
	}
}

static int __init stap_kmodule_init(void)
{
	timer_setup (&simple_timer, simple_timer_function,0);
	mod_timer (&simple_timer, jiffies + msecs_to_jiffies(timer_interval));
	return 0;
}
module_init(stap_kmodule_init);

static void __exit stap_kmodule_exit(void)
{
	/*
	 * Use del_timer_sync() on RHEL8 (4.18) / RHEL9 (5.14) and other
	 * pre-6.15 kernels.  Upstream 6.15+ (linux 8fa7292fee5c) removed
	 * the del_timer*() wrappers; call timer_delete_sync() there instead.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
	timer_delete_sync (&simple_timer);
#else
	del_timer_sync (&simple_timer);
#endif
}
module_exit(stap_kmodule_exit);

MODULE_DESCRIPTION("systemtap test module");
MODULE_LICENSE("GPL");
