/* lsm-btf.h - BTF helpers for LSM hook support
 *
 * This file is part of systemtap, and is free software.
 */

#ifndef LSM_BTF_H
#define LSM_BTF_H

#ifdef HAVE_LIBBPF
#include <string>
#include <linux/types.h>

// Look up BTF ID for an LSM hook
// Returns BTF type ID for "bpf_lsm_<hook_name>", or -1 on error
__s32 lsm_hook_btf_id(const std::string& hook_name);

#endif /* HAVE_LIBBPF */
#endif /* LSM_BTF_H */
