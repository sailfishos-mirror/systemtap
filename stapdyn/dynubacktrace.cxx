// stapdyn third-party user backtrace (StackwalkerAPI + libdwfl)
// Copyright (C) 2026 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "dynubacktrace.h"
#include "dynutil.h"
#include "../staputil.h"

#include <dyninst/BPatch_frame.h>
#include <dyninst/BPatch_thread.h>
#include <dyninst/walker.h>
#include <dyninst/frame.h>

#include <elfutils/libdwfl.h>

#include <setjmp.h>
#include <signal.h>

#include <sstream>
#include <vector>

using namespace std;
using namespace Dyninst;
using namespace Dyninst::Stackwalker;
using namespace Dyninst::ProcControlAPI;


static string
basename_path(const string& path)
{
  string::size_type slash = path.find_last_of('/');
  if (slash == string::npos)
    return path;
  return path.substr(slash + 1);
}


struct dwfl_sym_info {
  string sym;
  string mod;
  string file;
  int line;
  unsigned long funcoff;
  unsigned long funcsize;
  dwfl_sym_info(): line(0), funcoff(0), funcsize(0) {}
};


static void
dwfl_lookup(Dwfl* dwfl, Address ra, dwfl_sym_info& info)
{
  if (!dwfl)
    return;

  Dwfl_Module* mod = dwfl_addrmodule(dwfl, (Dwarf_Addr)ra);
  if (!mod)
    return;

  const char* modname = dwfl_module_info(mod, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL);
  if (modname)
    info.mod = basename_path(modname);

  GElf_Sym gsym;
  GElf_Word shndxp = 0;
  const char* sname = dwfl_module_addrinfo(mod, (Dwarf_Addr)ra,
                                           &info.funcoff, &gsym,
                                           &shndxp, NULL, NULL);
  if (sname)
    {
      info.sym = sname;
      if (gsym.st_size)
        info.funcsize = (unsigned long)gsym.st_size;
    }

  Dwfl_Line* dline = dwfl_module_getsrc(mod, (Dwarf_Addr)ra);
  if (dline)
    {
      int lineno = 0, col = 0;
      Dwarf_Addr addr = 0;
      const char* src = dwfl_lineinfo(dline, &addr, &lineno, &col, NULL, NULL);
      if (src)
        {
          info.file = basename_path(src);
          info.line = lineno;
        }
    }
}


// Dyninst may abort() on internal SymtabAPI asserts during stackwalk.
// Contain that so a bad walk cannot kill stapdyn mid-callback.
static sigjmp_buf g_ubt_jmp;
static struct sigaction g_ubt_old_abrt;
static struct sigaction g_ubt_old_segv;
static volatile sig_atomic_t g_ubt_guarding = 0;

static void
ubt_crash_handler(int)
{
  if (g_ubt_guarding)
    siglongjmp(g_ubt_jmp, 1);
}

struct ubt_crash_guard {
  bool armed;
  ubt_crash_guard(): armed(false)
  {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ubt_crash_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGABRT, &sa, &g_ubt_old_abrt) == 0
        && sigaction(SIGSEGV, &sa, &g_ubt_old_segv) == 0)
      {
        g_ubt_guarding = 1;
        armed = true;
      }
  }
  ~ubt_crash_guard()
  {
    if (armed)
      {
        g_ubt_guarding = 0;
        sigaction(SIGABRT, &g_ubt_old_abrt, NULL);
        sigaction(SIGSEGV, &g_ubt_old_segv, NULL);
      }
  }
};


static Dwfl*
open_mutatee_dwfl(pid_t pid)
{
  static char* debuginfo_path;
  static const Dwfl_Callbacks callbacks = {
    .find_elf = dwfl_linux_proc_find_elf,
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .section_address = dwfl_offline_section_address,
    .debuginfo_path = &debuginfo_path,
  };
  Dwfl* dwfl = dwfl_begin(&callbacks);
  if (!dwfl)
    return NULL;
  if (dwfl_linux_proc_report(dwfl, pid) < 0
      || dwfl_report_end(dwfl, NULL, NULL) != 0)
    {
      dwfl_end(dwfl);
      return NULL;
    }
  return dwfl;
}


static string
format_frames(const vector<Address>& pcs, Dwfl* dwfl)
{
  ostringstream oss;
  for (size_t i = 0; i < pcs.size(); ++i)
    {
      Address ra = pcs[i];
      dwfl_sym_info di;
      dwfl_lookup(dwfl, ra, di);

      // Leading space matches linux _stp_print_addr FULLER layout
      // expected by user-hw-breakpoint-addr.exp.
      oss << " " << (void*)(uintptr_t)ra << " : ";
      if (!di.sym.empty())
        {
          oss << di.sym << "+0x" << hex << di.funcoff;
          if (di.funcsize)
            oss << "/0x" << di.funcsize;
          oss << dec;
        }
      else if (!di.mod.empty())
        oss << "[" << di.mod << "]";
      else
        oss << (void*)(uintptr_t)ra;

      if (!di.file.empty())
        {
          oss << " at " << di.file;
          if (di.line > 0)
            oss << ":" << di.line;
        }
      if (!di.mod.empty())
        oss << " [" << di.mod << "]";
      oss << "\n";
    }
  return oss.str();
}


// Prefer BPatch getCallStack; fall back to StackwalkerAPI.
static bool
collect_pcs(Process::ptr proc, THR_ID tid, BPatch_process* bp_process,
            vector<Address>& pcs)
{
  pcs.clear();

  if (bp_process)
    {
      BPatch_Vector<BPatch_thread*> threads;
      bp_process->getThreads(threads);
      BPatch_thread* thr = NULL;
      for (size_t i = 0; i < threads.size(); ++i)
        {
          if (!threads[i])
            continue;
          if (tid == NULL_THR_ID || threads[i]->getTid() == tid
              || (Dyninst::THR_ID)threads[i]->getLWP() == tid)
            {
              thr = threads[i];
              break;
            }
        }
      if (!thr && !threads.empty())
        thr = threads[0];
      if (thr)
        {
          BPatch_Vector<BPatch_frame> stack;
          if (thr->getCallStack(stack))
            {
              for (size_t i = 0; i < stack.size(); ++i)
                {
                  if (stack[i].getFrameType() == BPatch_frameTrampoline)
                    continue;
                  void* pc = stack[i].getPC();
                  if (pc)
                    pcs.push_back((Address)(uintptr_t)pc);
                }
              if (!pcs.empty())
                return true;
            }
        }
    }

  Walker* walker = Walker::newWalker(proc);
  if (!walker)
    return false;
  vector<Frame> frames;
  bool ok = walker->walkStack(frames, tid);
  if ((!ok || frames.empty()) && tid != NULL_THR_ID)
    ok = walker->walkStack(frames, NULL_THR_ID);
  if (ok)
    {
      for (size_t i = 0; i < frames.size(); ++i)
        {
          if (frames[i].nonCall())
            continue;
          pcs.push_back(frames[i].getRA());
        }
    }
  delete walker;
  return !pcs.empty();
}


// Third-party walk while the mutatee is stopped, then format for
// print_ubacktrace*.  Frame PCs come from BPatch getCallStack /
// StackwalkerAPI (Dyninst's natural APIs).  Address->symbol/file:line
// does *not*: BPatch findFunction/getSourceLines and related SymtabAPI
// paths have aborted stapdyn from the hwbkpt callback via
// assert(px != 0) on SymtabAPI::Type (not a C++ exception), including
// for probes that never call print_ubacktrace.  Elfutils dwfl against
// the mutatee PID is the workaround.  If a future Dyninst is safe for
// that lookup in-callback, prefer Frame::getName / BPatch / Symtab
// again and drop the dwfl dependency.  The SIGABRT/SIGSEGV guard below
// remains for the walk itself.
string
stapdyn_capture_ubacktrace(Process::ptr proc, THR_ID tid,
                           BPatch_process* bp_process)
{
  if (!proc)
    return "";

  ubt_crash_guard guard;
  if (!guard.armed)
    return "";

  if (sigsetjmp(g_ubt_jmp, 1) != 0)
    {
      staplog(2) << "ubacktrace capture aborted by Dyninst/crash" << endl;
      return "";
    }

  try
    {
      vector<Address> pcs;
      if (!collect_pcs(proc, tid, bp_process, pcs))
        {
          staplog(2) << "ubacktrace: no frames collected" << endl;
          return "";
        }

      // See comment on stapdyn_capture_ubacktrace: dwfl, not Dyninst, for
      // symbolization until Symtab/BPatch stop asserting here.
      Dwfl* dwfl = open_mutatee_dwfl(proc->getPid());
      string result = format_frames(pcs, dwfl);
      if (dwfl)
        dwfl_end(dwfl);

      staplog(3) << "captured ubacktrace (" << pcs.size()
                 << " frames):\n" << result;
      return result;
    }
  catch (const exception& e)
    {
      stapwarn() << "ubacktrace capture failed: " << e.what() << endl;
      return "";
    }
  catch (...)
    {
      stapwarn() << "ubacktrace capture failed" << endl;
      return "";
    }
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
