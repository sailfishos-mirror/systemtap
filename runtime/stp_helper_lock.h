/* -*- linux-c -*- 
 * Locking helper function api to support preempt-rt variant raw locks
 * and keep legacy locking compatibility intact.
 *
 * Author: Santosh Shukla <sshukla@mvista.com>
 *
 * Copyright (C) 2014 Red Hat Inc.
 * 
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 * */

#ifndef _STP_HELPER_LOCK_H_
#define _STP_HELPER_LOCK_H_

#include <linux/spinlock.h>

#ifndef STAP_SPIN_TRYLOCK_MAX_COUNT
#define STAP_SPIN_TRYLOCK_MAX_COUNT  5000
#endif

#define stp_nmi_spin_lock_irqsave(lock, flags, label) \
	if (in_nmi()) { \
		long i; \
		for (i = 0; i < STAP_SPIN_TRYLOCK_MAX_COUNT; i++) { \
			if (stp_spin_trylock(lock))  \
				break; \
		} \
		if (unlikely(i >= STAP_SPIN_TRYLOCK_MAX_COUNT)) { \
			goto label; \
		} \
	} else { \
		stp_spin_lock_irqsave(lock, flags); \
	}

#define stp_nmi_spin_unlock_irqrestore(lock, flags) \
	if (in_nmi()) { \
		stp_spin_unlock(lock); \
	} else { \
		stp_spin_unlock_irqrestore(lock, flags); \
	}

#if defined(CONFIG_PREEMPT_RT_FULL) || defined(CONFIG_PREEMPT_RT)

#define stp_spinlock_t raw_spinlock_t

#define STP_DEFINE_SPINLOCK(lock)	DEFINE_RAW_SPINLOCK(lock)

static inline void stp_spin_lock_init(raw_spinlock_t *lock)	{ raw_spin_lock_init(lock); }

static inline void stp_spin_lock(raw_spinlock_t *lock)		{ raw_spin_lock(lock); }
static inline void stp_spin_unlock(raw_spinlock_t *lock)	{ raw_spin_unlock(lock); }

#define stp_spin_trylock(lock)		raw_spin_trylock(lock)
#define stp_spin_lock_irqsave(lock, flags)		raw_spin_lock_irqsave(lock, flags)
#define stp_spin_unlock_irqrestore(lock, flags)		raw_spin_unlock_irqrestore(lock, flags)

#define stp_rwlock_t raw_spinlock_t

#define STP_DEFINE_RWLOCK(lock)		DEFINE_RAW_SPINLOCK(lock)

static inline void stp_rwlock_init(raw_spinlock_t *lock)	{ raw_spin_lock_init(lock); }

static inline void stp_read_lock(raw_spinlock_t *lock)		{ raw_spin_lock(lock); }
static inline void stp_read_unlock(raw_spinlock_t *lock)	{ raw_spin_unlock(lock); }
static inline void stp_write_lock(raw_spinlock_t *lock)		{ raw_spin_lock(lock); }
static inline void stp_write_unlock(raw_spinlock_t *lock)	{ raw_spin_unlock(lock); }

static inline int stp_read_trylock(raw_spinlock_t *lock)	{ return raw_spin_trylock(lock); }
static inline int stp_write_trylock(raw_spinlock_t *lock)	{ return raw_spin_trylock(lock); }

#define stp_read_lock_irqsave(lock, flags)		raw_spin_lock_irqsave(lock, flags)
#define stp_read_unlock_irqrestore(lock, flags)		raw_spin_unlock_irqrestore(lock, flags)
#define stp_write_lock_irqsave(lock, flags)		raw_spin_lock_irqsave(lock, flags)
#define stp_write_unlock_irqrestore(lock, flags) 	raw_spin_unlock_irqrestore(lock, flags)
  
#else

#define stp_spinlock_t spinlock_t

#define STP_DEFINE_SPINLOCK(lock)	DEFINE_SPINLOCK(lock)

static inline void stp_spin_lock_init(spinlock_t *lock)		{ spin_lock_init(lock); }

static inline void stp_spin_lock(spinlock_t *lock)		{ spin_lock(lock); }
static inline void stp_spin_unlock(spinlock_t *lock)		{ spin_unlock(lock); }

#define stp_spin_trylock(lock)		spin_trylock(lock)
#define stp_spin_lock_irqsave(lock, flags)		spin_lock_irqsave(lock, flags)
#define stp_spin_unlock_irqrestore(lock, flags)		spin_unlock_irqrestore(lock, flags)

#define stp_rwlock_t rwlock_t

#define STP_DEFINE_RWLOCK(lock)				DEFINE_RWLOCK(lock)

static inline void stp_rwlock_init(rwlock_t *lock)	{ rwlock_init(lock); }

static inline void stp_read_lock(rwlock_t *lock)	{ read_lock(lock); }
static inline void stp_read_unlock(rwlock_t *lock)	{ read_unlock(lock); }
static inline void stp_write_lock(rwlock_t *lock)	{ write_lock(lock); }
static inline void stp_write_unlock(rwlock_t *lock)	{ write_unlock(lock); }

static inline int stp_read_trylock(rwlock_t *lock)	{ return read_trylock(lock); }
static inline int stp_write_trylock(rwlock_t *lock)	{ return write_trylock(lock); }

#define stp_read_lock_irqsave(lock, flags)		read_lock_irqsave(lock, flags)
#define stp_read_unlock_irqrestore(lock, flags)		read_unlock_irqrestore(lock, flags)
#define stp_write_lock_irqsave(lock, flags)		write_lock_irqsave(lock, flags)
#define stp_write_unlock_irqrestore(lock, flags) 	write_unlock_irqrestore(lock, flags)

#endif

#endif /* _STP_HELPER_LOCK_H_ */

