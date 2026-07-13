// tracepoint metadata from kernel vmlinux.h (BTF trace callback typedefs)
// Copyright (C) 2026 Red Hat Inc.

#include "tracepoint-vmlinux.h"
#include "session.h"
#include "staputil.h"
#include "dwflpp.h"

#include <elfutils/libdw.h>

#include <fstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

bool
btf_tracepoint_meta_from_name (const string& btf_name, btf_tracepoint_meta& out)
{
  static const string prefix("btf_trace_");
  if (!startswith(btf_name, prefix))
    return false;

  string suffix = btf_name.substr(prefix.size());
  if (startswith(suffix, "bpf_")) // btf_bpf_* helpers, not tracepoints
    return false;

  out.btf_name = btf_name;
  out.declare_trace_hook = endswith(suffix, "_tp");
  if (out.declare_trace_hook && suffix.size() > 3)
    out.hook_name = suffix.substr(0, suffix.size() - 3);
  else
    out.hook_name = suffix;
  return true;
}

static bool
parse_btf_trace_typedef (const string& line, btf_tracepoint_meta& out)
{
  static const regex re(
    R"(^\s*typedef\s+void\s+\(\*btf_trace_([A-Za-z0-9_]+)\)\((.*)\)\s*;\s*$)");
  smatch m;
  if (!regex_match(line, m, re))
    return false;

  return btf_tracepoint_meta_from_name("btf_trace_" + m[1].str(), out);
}

bool
vmlinux_h_path (systemtap_session& s, string& path)
{
  path = s.kernel_build_tree + "/vmlinux.h";
  return access(path.c_str(), R_OK) == 0;
}

const vector<btf_tracepoint_meta>&
get_btf_tracepoint_catalog (systemtap_session& s)
{
  static unordered_map<string, vector<btf_tracepoint_meta>> cache;
  string path;
  if (!vmlinux_h_path(s, path))
    {
      static vector<btf_tracepoint_meta> empty;
      return empty;
    }

  struct stat st;
  string cache_key = path;
  if (stat(path.c_str(), &st) == 0)
    cache_key += ":" + lex_cast((unsigned long long)st.st_mtime)
      + ":" + lex_cast((unsigned long long)st.st_size);
  auto it = cache.find(cache_key);
  if (it != cache.end())
    return it->second;

  vector<btf_tracepoint_meta> catalog;
  ifstream in(path.c_str());
  string line;
  while (getline(in, line))
    {
      btf_tracepoint_meta meta;
      if (parse_btf_trace_typedef(line, meta))
        catalog.push_back(meta);
    }

  cache[cache_key] = catalog;
  if (s.verbose > 2)
    clog << _F("Parsed %zu btf_trace_* tracepoints from %s",
               catalog.size(), path.c_str()) << endl;
  return cache[cache_key];
}

struct btf_catalog_dwarf_search
{
  vector<btf_tracepoint_meta> catalog;
  unordered_set<string> seen_hook_names;
};

static int
btf_catalog_dwarf_type (Dwarf_Die *die, bool, const string&,
                        btf_catalog_dwarf_search *q)
{
  if (dwarf_tag(die) != DW_TAG_typedef)
    return DWARF_CB_OK;

  const char *n = dwarf_diename(die);
  if (!n)
    return DWARF_CB_OK;

  btf_tracepoint_meta meta;
  if (!btf_tracepoint_meta_from_name(n, meta))
    return DWARF_CB_OK;

  if (!q->seen_hook_names.insert(meta.hook_name).second)
    return DWARF_CB_OK;

  q->catalog.push_back(meta);
  return DWARF_CB_OK;
}

static int
btf_catalog_dwarf_cu (Dwarf_Die *cudie, btf_catalog_dwarf_search *q)
{
  return dwflpp::iterate_over_globals(cudie, btf_catalog_dwarf_type, q);
}

void
get_btf_tracepoint_catalog_from_dwarf (dwflpp& dw, systemtap_session& s,
                                       vector<btf_tracepoint_meta>& out)
{
  btf_catalog_dwarf_search q;
  dw.iterate_over_cus(btf_catalog_dwarf_cu, &q, true);
  out = q.catalog;
  if (s.verbose > 2)
    clog << _F("Parsed %zu btf_trace_* tracepoints from module '%s' dwarf",
               out.size(), dw.module_name.c_str()) << endl;
}
