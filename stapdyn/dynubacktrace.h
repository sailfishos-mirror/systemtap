// stapdyn third-party user backtrace (StackwalkerAPI)
// Copyright (C) 2026 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef DYNUBACKTRACE_H
#define DYNUBACKTRACE_H

#include <string>

#include <dyninst/BPatch_process.h>
#include <dyninst/PCProcess.h>

// Walk the stopped mutatee (third-party) and format a
// print_ubacktrace_fileline-like multiline string.  Empty on failure.
std::string stapdyn_capture_ubacktrace
  (Dyninst::ProcControlAPI::Process::ptr proc,
   Dyninst::THR_ID tid,
   BPatch_process* bp_process);

#endif // DYNUBACKTRACE_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
