// tracepoint metadata from kernel vmlinux.h (BTF trace callback typedefs)
// Copyright (C) 2026 Red Hat Inc.

#ifndef TRACEPOINT_VMLINUX_H
#define TRACEPOINT_VMLINUX_H

#include <string>
#include <vector>

struct systemtap_session;

struct btf_tracepoint_meta
{
  std::string btf_name;       // e.g. btf_trace_sched_switch
  std::string hook_name;      // kernel registration name, e.g. sched_switch
  bool declare_trace_hook;    // true when btf name used DECLARE_TRACE (_tp suffix)
};

struct dwflpp;

bool vmlinux_h_path (systemtap_session& s, std::string& path);
bool btf_tracepoint_meta_from_name (const std::string& btf_name,
                                    btf_tracepoint_meta& out);
const std::vector<btf_tracepoint_meta>&
get_btf_tracepoint_catalog (systemtap_session& s);
void get_btf_tracepoint_catalog_from_dwarf (dwflpp& dw, systemtap_session& s,
                                            std::vector<btf_tracepoint_meta>& out);

#endif
