/* target operations in the Dyninst mode
 * Copyright (C) 2012, 2016 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAPDYN_LOC2C_RUNTIME_H_
#define _STAPDYN_LOC2C_RUNTIME_H_

#include "../loc2c-runtime.h"

#define __get_user_asm(x, addr, err, itype, rtype, ltype, errret)	\
        (err) = __get_user((x), (typeof(x)*)(uintptr_t)(addr))

#define __put_user_asm(x, addr, err, itype, rtype, ltype, errret)	\
        (err) = __put_user((x), (typeof(x)*)(uintptr_t)(addr))


#define u_fetch_register(regno) \
  pt_regs_fetch_register(c->uregs, regno)
#define u_store_register(regno, value) \
  pt_regs_store_register(c->uregs, regno, value)

#if defined(__i386__)
// The kernel's way of getting esp doesn't work as an lvalue
#undef pt_dwarf_register_4
#define pt_dwarf_register_4(regs)	regs->esp
#endif


#define _stp_deref_nofault(value, size, addr, seg)			\
    __copy_from_user((void *)&(value), (void *)(addr), (size_t)(size))

#define uread(ptr) ({ \
	typeof(*(ptr)) _v = 0; \
	if (__copy_from_user((void *)&_v, (void *)(ptr), sizeof(*(ptr)))) \
	    DEREF_FAULT(ptr); \
	_v; \
    })

#define uwrite(ptr, value) ({ \
	typeof(*(ptr)) _v; \
	_v = (typeof(*(ptr)))(value); \
	if (__copy_to_user((void *)(ptr), (void *)&_v, sizeof(*(ptr)))) \
	    STORE_DEREF_FAULT(ptr); \
    })

#define uderef(size, addr) ({ \
    intptr_t _i = 0; \
    switch (size) { \
        case 1: _i = uread((u8 *)(uintptr_t)(addr)); break; \
        case 2: _i = uread((u16 *)(uintptr_t)(addr)); break;    \
        case 4: _i = uread((u32 *)(uintptr_t)(addr)); break;        \
        case 8: _i = uread((u64 *)(uintptr_t)(addr)); break;            \
	default: __get_user_bad(); \
    } \
    _i; \
  })

#define store_uderef(size, addr, value) ({ \
    switch (size) { \
        case 1: uwrite((u8 *)(uintptr_t)(addr), (value)); break;    \
        case 2: uwrite((u16 *)(uintptr_t)(addr), (value)); break;   \
        case 4: uwrite((u32 *)(uintptr_t)(addr), (value)); break;   \
        case 8: uwrite((u64 *)(uintptr_t)(addr), (value)); break;   \
	default: __put_user_bad(); \
    } \
  })


/* We still need to clean the runtime more before these can go away... */
#define kread uread
#define kwrite uwrite
#define kderef uderef
#define store_kderef store_uderef

#define stp_mem_access_begin(type, ptr, size, oldfs, seg, is_user_ptr)
#define stp_mem_access_end(oldfs, is_user)
#define stp_user_access_begin(type, ptr, size, oldfs, seg)
#define stp_user_access_end(oldfs)

#endif /* _STAPDYN_LOC2C_RUNTIME_H_ */
