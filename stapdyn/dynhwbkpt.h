// stapdyn hardware watchpoint (process.data) declarations
// Copyright (C) 2026 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef DYNHWBKPT_H
#define DYNHWBKPT_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// One process.data watchpoint described by the stap module.
struct dynhwbkpt_location {
  uint64_t index;
  uint64_t address; // 0 => resolve symbol at install time
  uint64_t length;  // 0 => use Dyninst variable size when symbol set
  uint64_t access; // STAPDYN_HWBKPT_WRITE or STAPDYN_HWBKPT_RW
  std::string symbol; // empty if address-based

  dynhwbkpt_location(uint64_t index, uint64_t address,
                     uint64_t length, uint64_t access,
                     const char* symbol = NULL);
};

// Query the stap module for process.data watchpoints.
// Returns 0 on success (including "none found").
int find_dynhwbkpts(void* module, std::vector<dynhwbkpt_location>& probes);

// When the session runs in stapdyn (shared memory), fire watchpoint
// probes locally instead of oneTimeCode in the mutatee.
void stapdyn_hwbkpt_set_local_module(void* module);
bool stapdyn_hwbkpt_fire_local(uint64_t index);

#endif // DYNHWBKPT_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
