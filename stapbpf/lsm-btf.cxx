/* lsm-btf.cxx - BTF helpers for LSM hook support
 *
 * This file is part of systemtap, and is free software.
 */

#include "config.h"

#ifdef HAVE_LIBBPF

#include "lsm-btf.h"
#include <cstdio>
#include <cerrno>
#include <cstring>

// Must include system libbpf headers outside of extern "C"
#include <bpf/btf.h>
#include <bpf/libbpf.h>

__s32
lsm_hook_btf_id(const std::string& hook_name)
{
  struct btf *vmlinux_btf = NULL;
  __s32 btf_id = -1;

  // Load vmlinux BTF
  vmlinux_btf = btf__load_vmlinux_btf();
  if (!vmlinux_btf)
    {
      fprintf(stderr, "Failed to load vmlinux BTF: %s\n", strerror(errno));
      return -1;
    }

  // LSM hooks are named "bpf_lsm_<hook_name>" in BTF
  std::string func_name = "bpf_lsm_" + hook_name;

  btf_id = btf__find_by_name_kind(vmlinux_btf, func_name.c_str(), BTF_KIND_FUNC);

  if (btf_id < 0)
    fprintf(stderr, "Warning: Could not find BTF ID for %s\n", func_name.c_str());
  else
    fprintf(stderr, "Found BTF ID %d for %s\n", btf_id, func_name.c_str());

  btf__free(vmlinux_btf);
  return btf_id;
}

#endif /* HAVE_LIBBPF */
