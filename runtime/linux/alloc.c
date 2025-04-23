/* -*- linux-c -*- 
 * Memory allocation functions
 * Copyright (C) 2005-2018 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAPLINUX_ALLOC_C_
#define _STAPLINUX_ALLOC_C_

#include <linux/percpu.h>
#include "stp_helper_lock.h"

static long _stp_allocated_net_memory = 0;
/* Default, and should be "safe" from anywhere. */
/* NB: GFP_NOWAIT is defined since kernel 2.6.37 */
#define STP_ALLOC_FLAGS (GFP_NOWAIT | __GFP_NORETRY | __GFP_NOWARN)
/* May only be used in context that can sleep. __GFP_NORETRY is to
   suppress the oom-killer from kicking in. */
#define STP_ALLOC_SLEEP_FLAGS (GFP_KERNEL | __GFP_NORETRY)

#ifdef __GFP_DIRECT_RECLAIM

#define STP_ALLOC_FLAGS_MIGHT_SLEEP(flags)  \
	((flags) & (__GFP_DIRECT_RECLAIM | __GFP_IO | __GFP_FS))

#elif defined(__GFP_WAIT)

#define STP_ALLOC_FLAGS_MIGHT_SLEEP(flags)  \
	((flags) & (__GFP_WAIT | __GFP_IO | __GFP_FS))

#elif defined(__GFP_RECLAIM)

#define STP_ALLOC_FLAGS_MIGHT_SLEEP(flags)  \
	((flags) & (__GFP_RECLAIM | __GFP_IO | __GFP_FS))

#endif

/* #define DEBUG_MEMALLOC_MIGHT_SLEEP */
/*
 * If DEBUG_MEMALLOC_MIGHT_SLEEP is defined (stap -DDEBUG_MEMALLOC_MIGHT_SLEEP ...)
 * then all memory allocation operations are assumed to sleep. This
 * can be used to check whether any memory allocations occur in atomic
 * context, which is discouraged on realtime kernels. */

/* #define DEBUG_MEM */
/*
 * If DEBUG_MEM is defined (stap -DDEBUG_MEM ...) then full memory
 * tracking is used. Each allocation is recorded and matched with 
 * a free. Also a fence is set around the allocated memory so overflows
 * and underflows can be detected. Errors are written to the system log
 * with printk.
 *
 * NOTE: if youy system is slow or your script makes a very large number
 * of allocations, you may get a warning in the system log:
 * BUG: soft lockup - CPU#1 stuck for 11s! [staprun:28269]
 * This is an expected side-effect of the overhead of tracking, especially
 * with a simple linked list of allocations. Optimization
 * would be nice, but DEBUG_MEM is only for testing.
 */

static long _stp_allocated_memory = 0;

#ifdef DEBUG_MEM

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
#define MTAG  _stp_curr_debug_mem_tag = __FILE__ ":" LINE_STRING;

#else

#define MTAG

#endif

#ifdef DEBUG_MEM
static STP_DEFINE_SPINLOCK(_stp_mem_lock);

static const char *_stp_curr_debug_mem_tag = NULL;

#define STP_MEM_MAGIC 0xc11cf77f
#define MEM_FENCE_SIZE 32

enum _stp_memtype { STP_MEM_KMALLOC, STP_MEM_VMALLOC, STP_MEM_PERCPU };

typedef struct {
	char *alloc;
	char *free;
} _stp_malloc_type;

static const _stp_malloc_type _stp_malloc_types[] = {
	{"kmalloc", "kfree"},
	{"vmalloc", "vfree"},
	{"alloc_percpu", "free_percpu"}
};

struct _stp_mem_entry {
	struct list_head list;
	int32_t magic;
	enum _stp_memtype type;
	size_t len;
	void *addr;
	const char *tag;
};

#define MEM_DEBUG_SIZE (2*MEM_FENCE_SIZE+sizeof(struct _stp_mem_entry))

static LIST_HEAD(_stp_mem_list);

static void _stp_check_mem_fence (char *addr, int size)
{
	char *ptr;
	int i;

	ptr = addr - MEM_FENCE_SIZE;
	while (ptr < addr) {
		if (*ptr != 0x55) {
			printk("SYSTEMTAP ERROR: Memory fence corrupted before allocated memory\n");
			printk("at addr %p. (Allocation starts at %p)", ptr, addr);
			return;
		}
		ptr++;
	}
	ptr = addr + size;
	while (ptr < addr + size + MEM_FENCE_SIZE) {
		if (*ptr != 0x55) {
			printk("SYSTEMTAP ERROR: Memory fence corrupted after allocated memory\n");
			printk("at addr %p. (Allocation ends at %p)", ptr, addr + size - 1);
			return;
		}
		ptr++;
	}
}

static void *_stp_mem_debug_setup(void *addr, size_t size, enum _stp_memtype type)
{
	struct list_head *p;
	struct _stp_mem_entry *m;
	unsigned long flags;
	memset(addr, 0x55, MEM_FENCE_SIZE);
	addr += MEM_FENCE_SIZE;
	memset(addr + size, 0x55, MEM_FENCE_SIZE);
	p = (struct list_head *)(addr + size + MEM_FENCE_SIZE);
	m = (struct _stp_mem_entry *)p;
	m->magic = STP_MEM_MAGIC;
	m->type = type;
	m->len = size;
	m->addr = addr;
	m->tag = _stp_curr_debug_mem_tag;
	stp_spin_lock_irqsave(&_stp_mem_lock, flags);
	list_add(p, &_stp_mem_list); 
	stp_spin_unlock_irqrestore(&_stp_mem_lock, flags);
	return addr;
}

/* Percpu allocations don't have the fence. Implementing it is problematic. */
static void _stp_mem_debug_percpu(struct _stp_mem_entry *m, void *addr, size_t size)
{
	struct list_head *p = (struct list_head *)m;
	unsigned long flags;
	m->magic = STP_MEM_MAGIC;
	m->type = STP_MEM_PERCPU;
	m->len = size;
	m->addr = addr;
	stp_spin_lock_irqsave(&_stp_mem_lock, flags);
	list_add(p, &_stp_mem_list);
	stp_spin_unlock_irqrestore(&_stp_mem_lock, flags);
}

static void _stp_mem_debug_free(void *addr, enum _stp_memtype type)
{
	int found = 0;
	struct list_head *p, *tmp;
	struct _stp_mem_entry *m = NULL;
	unsigned long flags;

	if (!addr) {
		/* Passing NULL to these *free() functions is safe */
		switch (type) {
		case STP_MEM_KMALLOC:
		case STP_MEM_PERCPU:
		case STP_MEM_VMALLOC:
			return;
		}
	}

	stp_spin_lock_irqsave(&_stp_mem_lock, flags);
	list_for_each_safe(p, tmp, &_stp_mem_list) {
		m = list_entry(p, struct _stp_mem_entry, list);
		if (m->addr == addr) {
			list_del(p);
			found = 1;
			break;
		}
	}
	stp_spin_unlock_irqrestore(&_stp_mem_lock, flags);
	if (!found) {
		printk("SYSTEMTAP ERROR: Free of unallocated memory %p type=%s\n", 
		       addr, _stp_malloc_types[type].free);
		return;
	}
	if (m->magic != STP_MEM_MAGIC) {
		printk("SYSTEMTAP ERROR: Memory at %p corrupted!!\n", addr);
		return;
	}
	if (m->type != type) {
		printk("SYSTEMTAP ERROR: Memory allocated with %s and freed with %s\n",
		       _stp_malloc_types[m->type].alloc, 		       
		       _stp_malloc_types[type].free);
	}
	
	switch (m->type) {
	case STP_MEM_KMALLOC:
		_stp_check_mem_fence(addr, m->len);
		kfree(addr - MEM_FENCE_SIZE);
		break;
	case STP_MEM_PERCPU:
		free_percpu(addr);
		kfree(p);
		break;
	case STP_MEM_VMALLOC:
		_stp_check_mem_fence(addr, m->len);
		vfree(addr - MEM_FENCE_SIZE);		
		break;
	default:
		printk("SYSTEMTAP ERROR: Attempted to free memory at addr %p len=%d with unknown allocation type.\n", addr, (int)m->len);
	}

	return;
}

static void _stp_mem_debug_validate(void *addr)
{
	int found = 0;
	struct list_head *p, *tmp;
	struct _stp_mem_entry *m = NULL;
	unsigned long flags;

	stp_spin_lock_irqsave(&_stp_mem_lock, flags);
	list_for_each_safe(p, tmp, &_stp_mem_list) {
		m = list_entry(p, struct _stp_mem_entry, list);
		if (m->addr == addr) {
			found = 1;
			break;
		}
	}
	stp_spin_unlock_irqrestore(&_stp_mem_lock, flags);
	if (!found) {
		printk("SYSTEMTAP ERROR: Couldn't validate memory %p\n", 
		       addr);
		return;
	}
	if (m->magic != STP_MEM_MAGIC) {
		printk("SYSTEMTAP ERROR: Memory at %p corrupted!!\n", addr);
		return;
	}
	
	switch (m->type) {
	case STP_MEM_KMALLOC:
		_stp_check_mem_fence(addr, m->len);
		break;
	case STP_MEM_PERCPU:
		/* do nothing */
		break;
	case STP_MEM_VMALLOC:
		_stp_check_mem_fence(addr, m->len);
		break;
	default:
		printk("SYSTEMTAP ERROR: Attempted to validate memory at addr %p len=%d with unknown allocation type.\n", addr, (int)m->len);
	}

	return;
}
#endif

/* #define STP_MAXMEMORY 8192 */
/*
 * If STP_MAXMEMORY is defined to a value (stap -DSTP_MAXMEMORY=8192
 * ...) then every memory allocation is checked to make sure the
 * systemtap module doesn't use more than STP_MAXMEMORY of memory.
 * STP_MAXMEMORY is specified in kilobytes, so, for example, '8192'
 * means that the systemtap module won't use more than 8 megabytes of
 * memory.
 *
 * Note 1: This size does include the size of the module itself, plus
 * any additional allocations.
 *
 * Note 2: Since we can't be ensured that the module transport is set
 * up when a memory allocation problem happens, this code can't
 * directly report an error back to a user (so instead it uses
 * 'printk').  If the modules transport has been set up, the code that
 * calls the memory allocation functions
 * (_stp_kmalloc/_stp_kzalloc/etc.) should report an error directly to
 * the user.
 *
 * Note 3: This only tracks direct allocations by the systemtap
 * runtime.  This does not track indirect allocations (such as done by
 * kprobes/uprobes/etc. internals).
 */

#ifdef STP_MAXMEMORY
#ifdef STAPCONF_MODULE_LAYOUT
#define _STP_MODULE_CORE_SIZE (THIS_MODULE->core_layout.size)
#elif defined(STAPCONF_MODULE_MEMORY)
#define _STP_MODULE_CORE_SIZE (THIS_MODULE->mem[MOD_TEXT].size)
#elif defined(STAPCONF_GRSECURITY)
#define _STP_MODULE_CORE_SIZE (THIS_MODULE->core_size_rw)
#else
#define _STP_MODULE_CORE_SIZE (THIS_MODULE->core_size)
#endif
#endif

static void *_stp_kmalloc_gfp(size_t size, gfp_t gfp_mask)
{
	void *ret;
#ifdef DEBUG_MEMALLOC_MIGHT_SLEEP
	if (STP_ALLOC_FLAGS_MIGHT_SLEEP(gfp_mask)) {
		might_sleep();
	}
#endif
#ifdef STP_MAXMEMORY
	if ((_STP_MODULE_CORE_SIZE + _stp_allocated_memory + size)
	    > (STP_MAXMEMORY * 1024)) {
		return NULL;
	}
#endif
#ifdef DEBUG_MEM
	ret = kmalloc(size + MEM_DEBUG_SIZE, gfp_mask);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
		ret = _stp_mem_debug_setup(ret, size, STP_MEM_KMALLOC);
	}
#else
	ret = kmalloc(size, gfp_mask);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
	}
#endif
	return ret;
}

static void *_stp_kmalloc(size_t size)
{
	return _stp_kmalloc_gfp(size, STP_ALLOC_FLAGS);
}

static void *_stp_kzalloc_gfp(size_t size, gfp_t gfp_mask)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
{
	void *ret;
#ifdef STP_MAXMEMORY
	if ((_STP_MODULE_CORE_SIZE + _stp_allocated_memory + size)
	    > (STP_MAXMEMORY * 1024)) {
		return NULL;
	}
#endif
#ifdef DEBUG_MEM
	ret = kmalloc(size + MEM_DEBUG_SIZE, gfp_mask);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
		ret = _stp_mem_debug_setup(ret, size, STP_MEM_KMALLOC);
		memset (ret, 0, size);
	}
#else
	ret = kmalloc(size, gfp_mask);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
		memset (ret, 0, size);
	}
#endif /* DEBUG_MEM */
	return ret;
}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) */
{
	void *ret;
#ifdef STP_MAXMEMORY
	if ((_STP_MODULE_CORE_SIZE + _stp_allocated_memory + size)
	    > (STP_MAXMEMORY * 1024)) {
		return NULL;
	}
#endif
#ifdef DEBUG_MEM
	ret = kzalloc(size + MEM_DEBUG_SIZE, gfp_mask);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
		ret = _stp_mem_debug_setup(ret, size, STP_MEM_KMALLOC);
	}
#else
	ret = kzalloc(size, gfp_mask);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
	}
#endif
	return ret;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15) */

static void *_stp_kzalloc(size_t size)
{
  return _stp_kzalloc_gfp(size, STP_ALLOC_FLAGS);
}

static void *_stp_vzalloc(size_t size)
{
	void *ret;
#ifdef STP_MAXMEMORY
	if ((_STP_MODULE_CORE_SIZE + _stp_allocated_memory + size)
	    > (STP_MAXMEMORY * 1024)) {
		return NULL;
	}
#endif
#ifdef DEBUG_MEM
	ret = vzalloc(size + MEM_DEBUG_SIZE);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
		ret = _stp_mem_debug_setup(ret, size, STP_MEM_VMALLOC);
	}
#else
	ret = vzalloc(size);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
	}
#endif
	return ret;
}

static void *_stp_vzalloc_node(size_t size, int node)
{
	void *ret;

#ifdef STP_MAXMEMORY
	if ((_STP_MODULE_CORE_SIZE + _stp_allocated_memory + size)
	    > (STP_MAXMEMORY * 1024)) {
		return NULL;
	}
#endif
#ifdef DEBUG_MEM
	ret = vzalloc_node(size + MEM_DEBUG_SIZE, node);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
		ret = _stp_mem_debug_setup(ret, size, STP_MEM_VMALLOC);
	}
#else
	ret = vzalloc_node(size, node);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
	}
#endif
	return ret;
}

#ifdef PCPU_MIN_UNIT_SIZE
#define _STP_MAX_PERCPU_SIZE PCPU_MIN_UNIT_SIZE
#else
#define _STP_MAX_PERCPU_SIZE 131072
#endif

/* Note, calls __alloc_percpu which may sleep and always uses GFP_KERNEL. */
static void __percpu *_stp_alloc_percpu(size_t size)
{
	void __percpu *ret;

	if (size > _STP_MAX_PERCPU_SIZE)
		return NULL;

#ifdef STP_MAXMEMORY
	if ((_STP_MODULE_CORE_SIZE + _stp_allocated_memory
	     + (size * num_possible_cpus()))
	    > (STP_MAXMEMORY * 1024)) {
		return NULL;
	}
#endif

#ifdef STAPCONF_ALLOC_PERCPU_ALIGN
	ret = __alloc_percpu(size, 8);
#else
	ret = __alloc_percpu(size);
#endif
#ifdef DEBUG_MEM
	if (likely(ret)) {
		struct _stp_mem_entry *m = kmalloc(sizeof(struct _stp_mem_entry), GFP_KERNEL);
		if (unlikely(m == NULL)) {
			free_percpu(ret);
			return NULL;
		}
	        _stp_allocated_memory += size * num_possible_cpus();
		_stp_mem_debug_percpu(m, ret, size);
	}
#else
	if (likely(ret)) {
	        _stp_allocated_memory += size * num_possible_cpus();
	}
#endif
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
#define _stp_kmalloc_node(size,node) _stp_kmalloc(size)
#define _stp_kmalloc_node_gfp(size,node,gfp) _stp_kmalloc_gfp(size,gfp)
#define _stp_kzalloc_node(size,node) _stp_kzalloc(size)
#define _stp_kzalloc_node_gfp(size,node,gfp) _stp_kzalloc_gfp(size,gfp)
#else
static void *_stp_kmalloc_node_gfp(size_t size, int node, gfp_t gfp_mask)
{
	void *ret;
#ifdef DEBUG_MEMALLOC_MIGHT_SLEEP
	if (STP_ALLOC_FLAGS_MIGHT_SLEEP(gfp_mask)) {
		might_sleep();
	}
#endif
#ifdef STP_MAXMEMORY
	if ((_STP_MODULE_CORE_SIZE + _stp_allocated_memory + size)
	    > (STP_MAXMEMORY * 1024)) {
		return NULL;
	}
#endif
#ifdef DEBUG_MEM
	ret = kmalloc_node(size + MEM_DEBUG_SIZE, gfp_mask, node);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
		ret = _stp_mem_debug_setup(ret, size, STP_MEM_KMALLOC);
	}
#else
	ret = kmalloc_node(size, gfp_mask, node);
	if (likely(ret)) {
	        _stp_allocated_memory += size;
	}
#endif
	return ret;
}
static void *_stp_kzalloc_node_gfp(size_t size, int node, gfp_t gfp_mask)
{
	/* This used to be simply:
	 *   return _stp_kmalloc_node_gfp(size, node, gfp_mask | __GFP_ZERO);
	 * but rhel5-era kernels BUG out on that flag. (PR14957)
	 *
	 * We could make a big #if-alternation for kernels which have support
	 * for kzalloc_node (kernel commit 979b0fea), as in _stp_kzalloc_gfp,
	 * but IMO that's needlessly complex.
	 *
	 * So for now, just malloc and zero it manually.
	 */
	void *ret = _stp_kmalloc_node_gfp(size, node, gfp_mask);
#ifdef DEBUG_MEMALLOC_MIGHT_SLEEP
	if (STP_ALLOC_FLAGS_MIGHT_SLEEP(gfp_mask)) {
		might_sleep();
	}
#endif
	if (likely(ret)) {
		memset (ret, 0, size);
	}
	return ret;
}
static void *_stp_kmalloc_node(size_t size, int node)
{
	return _stp_kmalloc_node_gfp(size, node, STP_ALLOC_FLAGS);
}
static void *_stp_kzalloc_node(size_t size, int node)
{
	return _stp_kzalloc_node_gfp(size, node, STP_ALLOC_FLAGS);
}
#endif /* LINUX_VERSION_CODE */

static void _stp_kfree(void *addr)
{
#ifdef DEBUG_MEM
	_stp_mem_debug_free(addr, STP_MEM_KMALLOC);
#else
	kfree(addr);
#endif
}

static void _stp_vfree(void *addr)
{
#ifdef DEBUG_MEMALLOC_MIGHT_SLEEP
	might_sleep();
#endif
#ifdef DEBUG_MEM
	_stp_mem_debug_free(addr, STP_MEM_VMALLOC);
#else
	vfree(addr);
#endif
}

static void _stp_free_percpu(void __percpu *addr)
{
#ifdef DEBUG_MEM
	_stp_mem_debug_free(addr, STP_MEM_PERCPU);
#else
	free_percpu(addr);
#endif
}

static void _stp_mem_debug_done(void)
{
#ifdef DEBUG_MEM
	struct list_head *p, *tmp;
	struct _stp_mem_entry *m;
	unsigned long flags;

	stp_spin_lock_irqsave(&_stp_mem_lock, flags);
	list_for_each_safe(p, tmp, &_stp_mem_list) {
		m = list_entry(p, struct _stp_mem_entry, list);
		list_del(p);

		printk("SYSTEMTAP ERROR: Memory %p len=%d tag=%s allocation type: %s. Not freed.\n",
		       m->addr, (int)m->len, m->tag ? m->tag : "",
		       _stp_malloc_types[m->type].alloc);

		if (m->magic != STP_MEM_MAGIC) {
			printk("SYSTEMTAP ERROR: Memory at %p len=%d corrupted!!\n", m->addr, (int)m->len);
			/* Don't free. Too dangerous */
			goto done;
		}

		switch (m->type) {
		case STP_MEM_KMALLOC:
			_stp_check_mem_fence(m->addr, m->len);
			kfree(m->addr - MEM_FENCE_SIZE);
			break;
		case STP_MEM_PERCPU:
			free_percpu(m->addr);
			kfree(p);
			break;
		case STP_MEM_VMALLOC:
			_stp_check_mem_fence(m->addr, m->len);
			vfree(m->addr - MEM_FENCE_SIZE);		
			break;
		default:
			printk("SYSTEMTAP ERROR: Attempted to free memory at addr %p len=%d with unknown allocation type.\n", m->addr, (int)m->len);
		}
	}
done:
	stp_spin_unlock_irqrestore(&_stp_mem_lock, flags);

	return;

#endif
}
#endif /* _STAPLINUX_ALLOC_C_ */
