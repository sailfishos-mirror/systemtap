// stapdyn hardware watchpoint (process.data) query
// Copyright (C) 2026 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "dynhwbkpt.h"
#include "dynutil.h"
#include "../staputil.h"

extern "C" {
#include "../runtime/dyninst/stapdyn.h"
}

using namespace std;

static void* g_hwbkpt_local_module = NULL;
static decltype(&enter_dyninst_hwbkpt_probe) g_hwbkpt_local_enter = NULL;
static decltype(&stp_dyninst_ubacktrace_set) g_hwbkpt_ubacktrace_set = NULL;


void
stapdyn_hwbkpt_set_local_module(void* module)
{
  g_hwbkpt_local_module = module;
  g_hwbkpt_local_enter = NULL;
  g_hwbkpt_ubacktrace_set = NULL;
  if (module)
    {
      // Soft failure: legacy modules without process.data simply leave
      // local firing disabled.
      set_dlsym(g_hwbkpt_local_enter, module,
                "enter_dyninst_hwbkpt_probe", false);
      set_dlsym(g_hwbkpt_ubacktrace_set, module,
                "stp_dyninst_ubacktrace_set", false);
    }
}


bool
stapdyn_hwbkpt_fire_local(uint64_t index, const string& ubacktrace)
{
  if (!g_hwbkpt_local_enter)
    return false;
  if (g_hwbkpt_ubacktrace_set)
    g_hwbkpt_ubacktrace_set(ubacktrace.empty() ? "" : ubacktrace.c_str());
  staplog(3) << "calling hwbkpt enter locally for probe index "
             << index << endl;
  g_hwbkpt_local_enter(index, NULL);
  return true;
}


int
find_dynhwbkpts(void* module, vector<dynhwbkpt_location>& probes)
{
  decltype(&stp_dyninst_hwbkpt_count) count_fn = NULL;
  set_dlsym(count_fn, module, "stp_dyninst_hwbkpt_count", false);
  if (count_fn == NULL)
    return 0;

  decltype(&stp_dyninst_hwbkpt_address) address_fn = NULL;
  decltype(&stp_dyninst_hwbkpt_length) length_fn = NULL;
  decltype(&stp_dyninst_hwbkpt_access) access_fn = NULL;
  decltype(&stp_dyninst_hwbkpt_symbol) symbol_fn = NULL;

  try
    {
      set_dlsym(address_fn, module, "stp_dyninst_hwbkpt_address");
      set_dlsym(length_fn, module, "stp_dyninst_hwbkpt_length");
      set_dlsym(access_fn, module, "stp_dyninst_hwbkpt_access");
      // Soft: older modules lack symbol-name process.data support.
      set_dlsym(symbol_fn, module, "stp_dyninst_hwbkpt_symbol", false);
    }
  catch (runtime_error& e)
    {
      staperror() << e.what() << endl;
      return 1;
    }

  const uint64_t n = count_fn();
  for (uint64_t i = 0; i < n; ++i)
    {
      const char* sym = symbol_fn ? symbol_fn(i) : NULL;
      dynhwbkpt_location p(i, address_fn(i), length_fn(i), access_fn(i), sym);
      staplog(3) << "hwbkpt index:" << i
                 << " addr:" << lex_cast_hex(p.address)
                 << " len:" << p.length
                 << " access:" << p.access
                 << " symbol:" << (p.symbol.empty() ? "(none)" : p.symbol)
                 << endl;
      probes.push_back(p);
    }

  return 0;
}


dynhwbkpt_location::dynhwbkpt_location(uint64_t index, uint64_t address,
                                       uint64_t length, uint64_t access,
                                       const char* symbol):
  index(index), address(address), length(length), access(access),
  symbol(symbol ? symbol : "")
{
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
