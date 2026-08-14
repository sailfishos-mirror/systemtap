// C++ interface to dwfl
// Copyright (C) 2005-2019 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008 James.Bottomley@HansenPartnership.com
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef DWFLPP_H
#define DWFLPP_H

#include "config.h"
#include "dwarf_wrappers.h"
#include "elaborate.h"
#include "session.h"
#include "setupdwfl.h"
#include "stringtable.h"

#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include "afteryou.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// High-verbosity / STAP_DWARF_TIMING=1: CPU-sum ns across fanout workers.
struct dwarf_fanout_timing
{
  std::atomic<bool> enabled;
  std::atomic<uint64_t> n;
  std::atomic<uint64_t> preamble_ns;
  std::atomic<uint64_t> iterate_ns;
  std::atomic<uint64_t> cudie_ns;
  std::atomic<uint64_t> getscopes_ns;
  std::atomic<uint64_t> prologue_ns;
  std::atomic<uint64_t> expand_ns;
  std::atomic<uint64_t> ctor_rest_ns;
  std::atomic<uint64_t> sess_wait_ns;
  std::atomic<uint64_t> sess_hold_ns;
  std::atomic<uint64_t> sess_n;
  std::atomic<uint64_t> cache_wait_ns;
  std::atomic<uint64_t> cache_hold_ns;
  std::atomic<uint64_t> cache_n;

  dwarf_fanout_timing () : enabled (false) { reset (); }

  void reset ()
  {
    n = preamble_ns = iterate_ns = cudie_ns = getscopes_ns =
      prologue_ns = expand_ns = ctor_rest_ns = 0;
    sess_wait_ns = sess_hold_ns = sess_n = 0;
    cache_wait_ns = cache_hold_ns = cache_n = 0;
  }

  static double ms (uint64_t ns) { return ns / 1e6; }

  void dump (std::ostream& o) const
  {
    uint64_t nn = n.load ();
    o << "dwarf fanout timing (cpu-sum over " << nn << " rebuilds):"
      << " preamble=" << ms (preamble_ns) << "ms"
      << " iterate=" << ms (iterate_ns) << "ms"
      << " (cudie=" << ms (cudie_ns) << "ms"
      << " getscopes=" << ms (getscopes_ns) << "ms"
      << " prologue=" << ms (prologue_ns) << "ms)"
      << " expand=" << ms (expand_ns) << "ms"
      << std::endl;
    o << "dwarf fanout locks (cpu-sum):"
      << " sess wait=" << ms (sess_wait_ns) << "ms"
      << " hold=" << ms (sess_hold_ns) << "ms"
      << " n=" << sess_n.load ()
      << " | cache wait=" << ms (cache_wait_ns) << "ms"
      << " hold=" << ms (cache_hold_ns) << "ms"
      << " n=" << cache_n.load ()
      << std::endl;
  }
};

extern dwarf_fanout_timing stap_dwarf_timing;

inline uint64_t
dwarf_timing_now_ns ()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds> (
           std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}

inline bool
dwarf_timing_wanted (const systemtap_session& sess)
{
  if (sess.verbose > 2)
    return true;
  const char* e = getenv ("STAP_DWARF_TIMING");
  return e && e[0] && e[0] != '0';
}

template<typename Mutex>
struct timed_lock_guard
{
  Mutex& m;
  uint64_t t_acquired;
  bool timed;
  std::atomic<uint64_t>* wait_ns;
  std::atomic<uint64_t>* hold_ns;
  std::atomic<uint64_t>* count;

  timed_lock_guard (Mutex& m,
                    std::atomic<uint64_t>& wait,
                    std::atomic<uint64_t>& hold,
                    std::atomic<uint64_t>& n)
    : m (m), t_acquired (0),
      timed (stap_dwarf_timing.enabled.load (std::memory_order_relaxed)),
      wait_ns (&wait), hold_ns (&hold), count (&n)
  {
    if (timed)
      {
        uint64_t t0 = dwarf_timing_now_ns ();
        m.lock ();
        t_acquired = dwarf_timing_now_ns ();
        *wait_ns += t_acquired - t0;
        (*count)++;
      }
    else
      m.lock ();
  }

  ~timed_lock_guard ()
  {
    if (timed)
      *hold_ns += dwarf_timing_now_ns () - t_acquired;
    m.unlock ();
  }

  timed_lock_guard (const timed_lock_guard&) = delete;
  timed_lock_guard& operator= (const timed_lock_guard&) = delete;
};

using timed_recursive_lock = timed_lock_guard<std::recursive_mutex>;

struct cache_fill_key
{
  int kind;
  uintptr_t a;
  uintptr_t b;
  cache_fill_key (int kind, uintptr_t a, uintptr_t b = 0)
    : kind (kind), a (a), b (b) {}
  bool operator< (const cache_fill_key& o) const
  {
    if (kind != o.kind) return kind < o.kind;
    if (a != o.a) return a < o.a;
    return b < o.b;
  }
};

// Old elf.h doesn't know about this machine type.
#ifndef EM_AARCH64
#define EM_AARCH64 183
#endif

#ifndef EM_RISCV
#define EM_RISCV 243
#endif


#define write_uleb128(ptr,val) ({	\
  uint32_t valv = (val);		\
  do					\
    {					\
      unsigned char c = valv & 0x7f;	\
      valv >>= 7;			\
      if (valv)				\
	c |= 0x80;			\
      *(ptr)++ = c;			\
    }					\
  while (valv);				\
})


extern "C" {
#include <elfutils/libdwfl.h>
#include <regex.h>
}


struct base_func_info;
struct func_info;
struct inline_instance_info;
struct symbol_table;
struct base_query;
struct external_function_query;
struct expression;

enum lineno_t { ABSOLUTE, RELATIVE, WILDCARD, ENUMERATED };
enum info_status { info_unknown, info_present, info_absent };

// module -> cu die[]
typedef std::unordered_map<Dwarf*, std::vector<Dwarf_Die>*> module_cu_cache_t;

// module -> type-unit die[] (filled separately so CU lists stay immutable)
typedef std::unordered_map<Dwarf*, std::vector<Dwarf_Die>*> module_tu_cache_t;

// typename -> die
typedef std::unordered_map<std::string, Dwarf_Die> cu_type_cache_t;

// cu die -> (typename -> die)
typedef std::unordered_map<void*, cu_type_cache_t*> mod_cu_type_cache_t;

// function -> die
typedef std::unordered_multimap<interned_string, Dwarf_Die> cu_function_cache_t;

// cu die -> (function -> die)
typedef std::unordered_map<void*, cu_function_cache_t*> mod_cu_function_cache_t;

// module -> (function -> die)
typedef std::unordered_map<Dwarf*, cu_function_cache_t*> mod_function_cache_t;

// inline function die -> instance die[]
typedef std::unordered_map<void*, std::vector<Dwarf_Die>*> cu_inl_function_cache_t;

// function die -> [call site die, call site function die]
typedef std::pair<Dwarf_Die, Dwarf_Die> call_site_cache_t;
typedef std::unordered_map<void*, std::vector<call_site_cache_t>*> cu_call_sites_cache_t;

// die -> parent die
typedef std::unordered_map<void*, Dwarf_Die> cu_die_parent_cache_t;

// cu die -> (die -> parent die)
typedef std::unordered_map<void*, cu_die_parent_cache_t*> mod_cu_die_parent_cache_t;

// Dwarf_Line[] (sorted by lineno)
typedef std::vector<Dwarf_Line*> lines_t;
typedef std::pair<lines_t::iterator,
                  lines_t::iterator>
        lines_range_t;

// srcfile -> Dwarf_Line[]
typedef std::unordered_map<std::string, lines_t*> srcfile_lines_cache_t;

// cu die -> (srcfile -> Dwarf_Line[])
typedef std::unordered_map<void*, srcfile_lines_cache_t*> cu_lines_cache_t;

// cu die -> {entry pcs}
typedef std::unordered_set<Dwarf_Addr> entry_pc_cache_t;
typedef std::unordered_map<void*, entry_pc_cache_t*> cu_entry_pc_cache_t;

// pc (CU/DWARF address space) -> scopes from dwarf_getscopes / DIE walk
typedef std::unordered_map<Dwarf_Addr, std::vector<Dwarf_Die>> pc_scopes_cache_t;
// cu die -> (pc -> scopes)
typedef std::unordered_map<void*, pc_scopes_cache_t*> cu_pc_scopes_cache_t;

typedef std::vector<base_func_info> base_func_info_map_t;
typedef std::vector<func_info> func_info_map_t;
typedef std::vector<inline_instance_info> inline_instance_map_t;


struct
module_info
{
  Dwfl_Module* mod;
  const char* name;
  std::string elf_path;
  Dwarf_Addr addr;
  Dwarf_Addr bias;
  symbol_table *sym_table;
  info_status dwarf_status;     // module has dwarf info?
  info_status symtab_status;    // symbol table cached?

  std::set<interned_string> inlined_funcs;
  std::set<interned_string> plt_funcs;
  std::set<std::pair<std::string,std::string> > marks; /* <provider,name> */

  // Serializes get_symtab / update_symtab against concurrent dwarf builds.
  std::recursive_mutex symtab_mutex;

  void get_symtab();
  void update_symtab(cu_function_cache_t *funcs);

  module_info(const char *name) :
    mod(NULL),
    name(name),
    addr(0),
    bias(0),
    sym_table(NULL),
    dwarf_status(info_unknown),
    symtab_status(info_unknown)
  {}

  ~module_info();
};


struct
module_cache
{
  std::map<std::string, module_info*> cache;
  bool paths_collected;
  bool dwarf_collected;

  module_cache() : paths_collected(false), dwarf_collected(false) {}
  ~module_cache();
};


struct base_func_info
{
  base_func_info()
  : decl_line(-1), entrypc(0)
  {
    std::memset(&die, 0, sizeof(die));
  }
  interned_string name;
  interned_string decl_file;
  int decl_line;
  Dwarf_Die die;
  Dwarf_Addr entrypc;
};

struct func_info : base_func_info
{
  func_info()
    : addr(0), prologue_end(0), weak(false), descriptor(false) {}
  Dwarf_Addr addr;
  Dwarf_Addr prologue_end;
  bool weak, descriptor;
};


struct inline_instance_info : base_func_info
{
  inline_instance_info() {}
  bool operator<(const inline_instance_info& other) const;
};

struct location;
class location_context;

// Per-query (or per-nested-walk) DWARF cursor.  Concurrent builds share one
// dwflpp (Dwfl + caches) and each keep focus here / on base_query::focus.
struct dwflpp_focus
{
  Dwfl_Module * module;
  Dwarf_Addr module_bias;
  module_info * mod_info;
  Dwarf_Addr module_start;
  Dwarf_Addr module_end;
  Dwarf_Die * cu;
  std::string module_name;
  std::string function_name;
  Dwarf * module_dwarf;
  Dwarf_Die * function;

  dwflpp_focus()
    : module(NULL), module_bias(0), mod_info(NULL),
      module_start(0), module_end(0), cu(NULL),
      module_dwarf(NULL), function(NULL)
  {}
};

// Bind a focus for the current thread while calling into dwflpp.  Nested
// binders push/pop so temporary walks do not clobber the query cursor.
struct dwflpp_focus_binder
{
  dwflpp_focus *prev;
  explicit dwflpp_focus_binder (dwflpp_focus& f);
  ~dwflpp_focus_binder ();
  dwflpp_focus_binder (const dwflpp_focus_binder&) = delete;
  dwflpp_focus_binder& operator= (const dwflpp_focus_binder&) = delete;
};

struct dwflpp
{
  systemtap_session & sess;

  // Focus accessors — require an active dwflpp_focus_binder on this thread.
  Dwfl_Module * module() const { return foc().module; }
  Dwarf_Addr module_bias() const { return foc().module_bias; }
  module_info * mod_info() const { return foc().mod_info; }
  Dwarf_Addr module_start() const { return foc().module_start; }
  Dwarf_Addr module_end() const { return foc().module_end; }
  Dwarf_Die * cu() const { return foc().cu; }
  const std::string& module_name() const { return foc().module_name; }
  const std::string& function_name() const { return foc().function_name; }

  dwflpp(systemtap_session & session, const std::string& user_module, bool kernel_p, bool debuginfo_needed = true);
  dwflpp(systemtap_session & session, const std::vector<std::string>& user_modules, bool kernel_p);
  ~dwflpp();

  void get_module_dwarf(bool required = false, bool report = true);
  // Finish lazy per-module DWARF/ELF/symtab bring-up under the caller
  // lock so concurrent builds do not race first-time dwfl_module_get*.
  void prepare_modules_for_parallel_use();

  // After-you module-wide function index.  The leader fills every
  // compile unit (parallel dwarf_getfuncs iff HAVE_ELFUTILS_THREAD_SAFETY,
  // else serial) and publishes both cu_function_cache and
  // mod_function_cache.  Waiters block then hash-lookup.  Returns NULL
  // if this module has no DWARF.
  cu_function_cache_t *ensure_module_function_cache();

  void focus_on_module(Dwfl_Module * m, module_info * mi);
  void focus_on_cu(Dwarf_Die * c);
  void focus_on_function(Dwarf_Die * f);

  std::string cu_name(void);

  Dwarf_Die *query_cu_containing_address(Dwarf_Addr a);

  bool module_name_matches(const std::string& pattern);
  static bool name_has_wildcard(const std::string& pattern);
  bool module_name_final_match(const std::string& pattern);

  bool function_name_matches_pattern(const std::string& name, const std::string& pattern);
  bool function_name_matches(const std::string& pattern);
  bool function_scope_matches(const std::vector<std::string>& scopes);

  template<typename T>
  void iterate_over_modules(int (* callback)(Dwfl_Module*,
                                             void**,
                                             const char*,
                                             Dwarf_Addr,
                                             T*),
                            T *data)
    {
      /* We're using templates here to enforce type-safety between the data arg
       * we're requested to pass to callback, and the data arg that the callback
       * actually takes. Rather than putting the implementation here, we simply
       * call the <void> specialization, which does the real work.
       *    As a result, we need to cast the data arg in the callback signature
       * and the one passed to void* (which is what elfutils also works with).
       * */
      iterate_over_modules<void>((int (*)(Dwfl_Module*,
                                          void**,
                                          const char*,
                                          Dwarf_Addr,
                                          void *))callback,
                                  (void*)data);
    }

  template<typename T>
  void iterate_over_cus(int (* callback)(Dwarf_Die*, T*),
                        T *data,
                        bool want_types)
    {
      // See comment block in iterate_over_modules()
      iterate_over_cus<void>((int (*)(Dwarf_Die*, void*))callback,
                             (void*)data,
                             want_types);
    }

  bool func_is_inline();

  bool func_is_exported();

  template<typename T>
  void iterate_over_inline_instances(int (* callback)(Dwarf_Die*, T*),
                                     T *data)
    {
      // See comment block in iterate_over_modules()
      iterate_over_inline_instances<void>((int (*)(Dwarf_Die*, void*))callback,
                                          (void*)data);
    }

  template<typename T>
  void iterate_over_call_sites(int (* callback)(Dwarf_Die*, Dwarf_Die*, T*),
                               T *data)
    {
      // See comment block in iterate_over_modules()
      iterate_over_call_sites<void>((int (*)(Dwarf_Die*, Dwarf_Die*, void*))callback,
                                           (void*)data);
    }

  std::vector<Dwarf_Die> getscopes_die(Dwarf_Die* die);
  std::vector<Dwarf_Die> getscopes(Dwarf_Die* die);
  std::vector<Dwarf_Die> getscopes(Dwarf_Addr pc);
  // Record scopes for pc from an already-known DIE (parent-cache walk),
  // so later getscopes(pc) skips dwarf_getscopes.  pc is in the same
  // address space query_addr uses after elf/module bias adjustment.
  void cache_scopes_at_pc(Dwarf_Addr pc, Dwarf_Die* die);

  Dwarf_Die *declaration_resolve(Dwarf_Die *type);
  Dwarf_Die *declaration_resolve(const std::string& name);
  Dwarf_Die *declaration_resolve_other_cus(const std::string& name);

  template<typename T>
  int iterate_over_functions (int (* callback)(Dwarf_Die*, T*),
                              T *data, const std::string& function)
    {
      // See comment block in iterate_over_modules()
      return iterate_over_functions<void>((int (*)(Dwarf_Die*, void*))callback,
                                          (void*)data, function);
    }

  template<typename T>
  int iterate_single_function (int (* callback)(Dwarf_Die*, T*),
                               T *data, const std::string& function)
    {
      // See comment block in iterate_over_modules()
      return iterate_single_function<void>((int (*)(Dwarf_Die*, void*))callback,
                                           (void*)data, function);
    }

  template<typename T>
  int iterate_over_notes (T *object,
                          void (* callback)(T*, const std::string&,
                                                const std::string&,
                                                int, const char*, size_t))
    {
      // See comment block in iterate_over_modules()
      return iterate_over_notes<void>((void*)object,
                                      (void (*)(void*,
                                                const std::string&,
                                                const std::string&,
                                                int,
                                                const char*,
                                                size_t))callback);
    }

  template<typename T>
  void iterate_over_libraries (void (*callback)(T*, const char*), T *data)
    {
      // See comment block in iterate_over_modules()
      iterate_over_libraries<void>((void (*)(void*,
                                             const char*))callback,
                                   (void*)data);
    }

  template<typename T>
  int iterate_over_plt (T *object, void (*callback)(T*, const char*, size_t))
    {
      // See comment block in iterate_over_modules()
      return iterate_over_plt<void>((void*)object,
                                    (void (*)(void*,
                                              const char*,
                                              size_t))callback);
    }

  template<typename T>
  void iterate_over_srcfile_lines (char const * srcfile,
                                   const std::vector<int>& linenos,
                                   enum lineno_t lineno_type,
                                   base_func_info_map_t& funcs,
                                   void (*callback) (Dwarf_Addr,
                                                     int, T*),
                                   bool has_nearest,
                                   T *data)
    {
      // See comment block in iterate_over_modules()
      iterate_over_srcfile_lines<void>(srcfile,
                                       linenos,
                                       lineno_type,
                                       funcs,
                                       (void (*)(Dwarf_Addr,
                                                 int, void*))callback,
                                       has_nearest,
                                       (void*)data);
    }

  template<typename T>
  void iterate_over_labels (Dwarf_Die *begin_die,
                            const std::string& sym,
                            const base_func_info& function,
                            const std::vector<int>& linenos,
                            enum lineno_t lineno_type,
                            T *data,
                            void (* callback)(const base_func_info&,
                                              const char*,
                                              const char*,
                                              int,
                                              Dwarf_Die*,
                                              Dwarf_Addr,
                                              T*))
    {
      // See comment block in iterate_over_modules()
      iterate_over_labels<void>(begin_die,
                                sym,
                                function,
                                linenos,
                                lineno_type,
                                (void*)data,
                                (void (*)(const base_func_info&,
                                          const char*,
                                          const char*,
                                          int,
                                          Dwarf_Die*,
                                          Dwarf_Addr,
                                          void*))callback);
    }

  template<typename T>
  void iterate_over_callees (Dwarf_Die *begin_die,
                             const std::string& sym,
                             int64_t recursion_depth,
                             T *data,
                             void (* callback)(base_func_info&,
                                               base_func_info&,
                                               std::stack<Dwarf_Addr>*,
                                               T*),
                             base_func_info& caller,
                             std::stack<Dwarf_Addr>*callers=NULL)
    {
      // See comment block in iterate_over_modules()
      iterate_over_callees<void>(begin_die,
                                 sym,
                                 recursion_depth,
                                 (void*)data,
                                 (void (*)(base_func_info&,
                                           base_func_info&,
                                           std::stack<Dwarf_Addr>*,
                                           void*))callback,
                                 caller,
                                 callers);
    }

  template<typename T>
  static int iterate_over_globals (Dwarf_Die *cu_die,
                                   int (* callback)(Dwarf_Die*,
                                                    bool,
                                                    const std::string&,
                                                    T*),
                                   T *data)
    {
      // See comment block in iterate_over_modules()
      return iterate_over_globals<void>(cu_die,
                                        (int (*)(Dwarf_Die*,
                                                 bool,
                                                 const std::string&,
                                                 void*))callback,
                                        (void*)data);
    }

  GElf_Shdr * get_section(std::string section_name, GElf_Shdr *shdr_mem,
                          Elf **elf_ret=NULL);

  void collect_srcfiles_matching (std::string const & pattern,
                                  std::set<std::string> & filtered_srcfiles);

  void resolve_prologue_endings (func_info_map_t & funcs);

  bool function_entrypc (Dwarf_Addr * addr) __attribute__((warn_unused_result));
  bool die_entrypc (Dwarf_Die * die, Dwarf_Addr * addr) __attribute__((warn_unused_result));

  void function_die (Dwarf_Die *d);
  void function_file (char const ** c);
  void function_line (int *linep);

  bool die_has_pc (Dwarf_Die & die, Dwarf_Addr pc);
  bool inner_die_containing_pc(Dwarf_Die& scope, Dwarf_Addr addr,
                               Dwarf_Die& result);

  bool literal_stmt_for_local (location_context &ctx,
			       std::vector<Dwarf_Die>& scopes,
			       std::string const & local,
			       const target_symbol *e,
			       bool lvalue,
			       Dwarf_Die *die_mem);
  Dwarf_Die* type_die_for_local (std::vector<Dwarf_Die>& scopes,
				 Dwarf_Addr pc,
                                 std::string const & local,
                                 const target_symbol *e,
                                 Dwarf_Die *die_mem,
				 bool lvalue);

  bool literal_stmt_for_return (location_context &ctx,
				Dwarf_Die *scope_die,
				const target_symbol *e,
				bool lvalue,
				Dwarf_Die *die_mem);
  Dwarf_Die* type_die_for_return (Dwarf_Die *scope_die,
                                  Dwarf_Addr pc,
                                  const target_symbol *e,
                                  Dwarf_Die *die_mem,
				  bool lvalue);

  bool literal_stmt_for_pointer (location_context &ctx,
				 Dwarf_Die *type_die,
				 const target_symbol *e,
				 bool lvalue,
				 Dwarf_Die *die_mem);
  Dwarf_Die* type_die_for_pointer (Dwarf_Die *type_die,
                                   const target_symbol *e,
                                   Dwarf_Die *die_mem,
				   bool lvalue);

  enum blocklisted_type
    {  blocklisted_none, // not blocklisted
       blocklisted_section,
       blocklisted_kprobes,
       blocklisted_function,
       blocklisted_function_return,
       blocklisted_file
    };

  blocklisted_type blocklisted_p(interned_string funcname,
                                 interned_string filename,
                                 int line,
                                 interned_string module,
                                 Dwarf_Addr addr,
                                 bool has_return);

  Dwarf_Addr relocate_address(Dwarf_Addr addr, interned_string& reloc_section);

  void resolve_unqualified_inner_typedie (Dwarf_Die *typedie,
                                          Dwarf_Die *innerdie,
                                          const target_symbol *e);

  bool has_gnu_debugdata();
  bool has_valid_locs();

  location *translate_call_site_value (location_context *ctx,
                                       Dwarf_Attribute *attr,
                                       Dwarf_Die *die,
                                       Dwarf_Die *funcdie,
                                       Dwarf_Addr pc);

  int get_enum_value (Dwarf_Die *scopes, int nscopes, const char *name, Dwarf_Sword *value);
  void get_enums(std::vector<Dwarf_Die>& scopes, std::set<std::string>& enums);
  // Collect value→name for one DW_TAG_enumeration_type.  First name wins
  // when multiple enumerators share a value.
  void get_enum_name_map (Dwarf_Die *enum_type,
                          std::map<int64_t, std::string>& lut);

private:
  friend struct dwflpp_focus_binder;
  static thread_local dwflpp_focus *tls_focus;
  dwflpp_focus &foc ();
  const dwflpp_focus &foc () const;

  // Refcounted so prepare-time and later teardown stay simple.
  std::shared_ptr<Dwfl> dwfl;

  // Guards fill-once cache maps under HAVE_ELFUTILS_THREAD_SAFETY concurrent
  // dwarf builds that share this dwflpp.  Holds are short: lookup/publish
  // only.  Long DWARF fills run outside, serialized per key by
  // cache_fill_inflight (debuginfod-style after-you).
  mutable std::mutex cache_mutex;
  mutable after_you_set<cache_fill_key> cache_fill_inflight;

  enum cache_fill_kind
    {
      FILL_MODULE_CU = 1,
      FILL_MODULE_TU,
      FILL_CU_FUNCS,
      FILL_MOD_FUNCS,
      FILL_INL,
      FILL_CALL_SITES,
      FILL_DIE_PARENTS,
      FILL_GLOBAL_ALIAS,
      FILL_CU_LINES,
      FILL_PC_SCOPES,
      FILL_ENTRY_PC
    };

  template<typename Map, typename Key, typename F>
  typename Map::mapped_type
  cache_lookup_or_fill (Map& map, const Key& key,
                        const cache_fill_key& fill_key, F fill);

  template<typename F>
  void cache_fill_once (void *id, cache_fill_kind kind,
                        std::set<void*>& done, F fill);

  void setup_kernel(const std::string& module_name, systemtap_session &s, bool debuginfo_needed = true);
  void setup_kernel(const std::vector<std::string>& modules, bool debuginfo_needed = true);
  void setup_user(const std::vector<std::string>& modules, bool debuginfo_needed = true);

  module_cu_cache_t module_cu_cache;
  module_tu_cache_t module_tu_cache;
  mod_cu_function_cache_t cu_function_cache;
  mod_function_cache_t mod_function_cache;

  std::vector<Dwarf_Die> *module_compile_units();
  cu_function_cache_t *ensure_cu_function_cache(Dwarf_Die *cu);
  cu_function_cache_t *fill_module_function_cache();

  std::set<void*> cu_inl_function_cache_done; // CUs that are already cached
  cu_inl_function_cache_t cu_inl_function_cache;
  void cache_inline_instances (Dwarf_Die* die, cu_inl_function_cache_t& dest);

  std::set<void*> cu_call_sites_cache_done; // CUs that are already cached
  cu_call_sites_cache_t cu_call_sites_cache;
  void cache_call_sites (Dwarf_Die* die, Dwarf_Die *function,
                         cu_call_sites_cache_t& dest);

  mod_cu_die_parent_cache_t cu_die_parent_cache;
  void cache_die_parents(cu_die_parent_cache_t* parents, Dwarf_Die* die);
  cu_die_parent_cache_t *get_die_parents();

  // Cache for cu lines sorted by lineno
  cu_lines_cache_t cu_lines_cache;

  // Cache for all entry_pc in each cu
  cu_entry_pc_cache_t cu_entry_pc_cache;
  bool check_cu_entry_pc(Dwarf_Die *cu, Dwarf_Addr pc);

  // Cache for getscopes(pc); filled on miss and via cache_scopes_at_pc.
  cu_pc_scopes_cache_t cu_pc_scopes_cache;

  Dwarf_Die* get_parent_scope(Dwarf_Die* die);

  /* The global alias cache is used to resolve any DIE found in a
   * module that is stubbed out with DW_AT_declaration with a defining
   * DIE found in a different module.  The current assumption is that
   * this only applies to structures and unions, which have a global
   * namespace (it deliberately only traverses program scope), so this
   * cache is indexed by name.  If other declaration lookups were
   * added to it, it would have to be indexed by name and tag
   */
  mod_cu_type_cache_t global_alias_cache;
  static int global_alias_caching_callback(Dwarf_Die *die, bool has_inner_types,
                                           const std::string& prefix, cu_type_cache_t *cache);
  static int global_alias_caching_callback_cus(Dwarf_Die *die, dwflpp *dw);

  template<typename T>
  static int iterate_over_types (Dwarf_Die *top_die,
                                 bool has_inner_types,
                                 const std::string& prefix,
                                 int (* callback)(Dwarf_Die*,
                                                  bool,
                                                  const std::string&,
                                                  T*),
                                 T *data)
    {
      // See comment block in iterate_over_modules()
      return iterate_over_types<void>(top_die, has_inner_types, prefix,
                                      (int (*)(Dwarf_Die*,
                                               bool,
                                               const std::string&,
                                               void*))callback,
                                      (void*)data);
    }

  static int cu_function_caching_callback (Dwarf_Die* func, cu_function_cache_t *v);

  lines_t* get_cu_lines_sorted_by_lineno(const char *srcfile);

  void collect_lines_for_single_lineno(char const * srcfile,
                                       int lineno,
                                       bool is_relative,
                                       base_func_info_map_t& funcs,
                                       lines_t& matching_lines);
  void collect_all_lines(char const * srcfile,
                         base_func_info_map_t& funcs,
                         lines_t& matching_lines);
  std::pair<int,int> get_nearest_linenos(char const * srcfile,
                                         int lineno,
                                         base_func_info_map_t& funcs);
  int get_nearest_lineno(char const * srcfile,
                         int lineno,
                         base_func_info_map_t& funcs);
  void suggest_alternative_linenos(char const * srcfile,
                                   int lineno,
                                   base_func_info_map_t& funcs);

  static int external_function_cu_callback (Dwarf_Die* cu, external_function_query *efq);
  static int external_function_func_callback (Dwarf_Die* func, external_function_query *efq);

  static void loc2c_error (void *, const char *fmt, ...) __attribute__ ((noreturn));

  // This function generates code used for addressing computations of
  // target variables.
  void emit_address (Dwarf_Addr address);

  int  dwarf_get_enum (Dwarf_Die *scopes, int nscopes,
                       const char *name, Dwarf_Die *result, Dwarf_Die *type);
  void get_locals(std::vector<Dwarf_Die>& scopes, std::set<std::string>& locals);
  void get_locals_die(Dwarf_Die &die, std::set<std::string>& locals);
  void get_members(Dwarf_Die *vardie, std::set<std::string>& members,
                     std::set<std::string> &dupes);

  Dwarf_Attribute *find_variable_and_frame_base (std::vector<Dwarf_Die>& scopes,
                                                 Dwarf_Addr pc,
                                                 std::string const & local,
                                                 const target_symbol *e,
                                                 Dwarf_Die *vardie,
                                                 Dwarf_Die *typedie,
                                                 Dwarf_Attribute *fb_attr_mem,
                                                 Dwarf_Die *funcdie);

  std::string die_location_as_string(Dwarf_Die*);
  std::string pc_location_as_function_string(Dwarf_Addr);
  std::string pc_die_line_string(Dwarf_Addr, Dwarf_Die*);

  /* source file name, line and column info for pc in current cu. */
  const char *pc_line (Dwarf_Addr, int *, int *);

  location *translate_location(location_context *ctx,
			       Dwarf_Attribute *attr,
			       Dwarf_Die *die,
			       Dwarf_Addr pc,
			       Dwarf_Attribute *fb_attr,
			       const target_symbol *e,
			       location *input);

  bool find_struct_member(const target_symbol::component& c,
                          Dwarf_Die *parentdie,
                          Dwarf_Die *memberdie,
                          std::vector<Dwarf_Die>& dies,
                          std::vector<Dwarf_Attribute>& locs);

  void translate_components(location_context *ctx,
			    Dwarf_Addr pc,
                            const target_symbol *e,
                            Dwarf_Die *vardie,
                            Dwarf_Die *typedie,
			    bool lvalue,
                            unsigned first=0);

  void translate_base_ref (location_context &ctx, Dwarf_Word byte_size,
			   bool signed_p, bool lvalue_p);
  void translate_bitfield(location_context &ctx, Dwarf_Word byte_size,
                          Dwarf_Word bit_offset, Dwarf_Word bit_size,
                          bool signed_p);
  void translate_final_fetch_or_store (location_context &ctx,
				       Dwarf_Die *vardie,
				       Dwarf_Die *typedie,
				       bool lvalue,
				       Dwarf_Die *enddie);
  void translate_pointer(location_context &ctx, Dwarf_Die *typedie,
			 bool lvalue);

  regex_t blocklist_func; // function/statement probes
  regex_t blocklist_func_ret; // only for .return probes
  regex_t blocklist_file; // file name
  regex_t blocklist_section; // init/exit sections
  bool blocklist_enabled;
  void build_kernel_blocklist();
  void build_user_blocklist();
  std::string get_blocklist_section(Dwarf_Addr addr);

  // Returns the call frame address operations for the given program counter.
  Dwarf_Op *get_cfa_ops (Dwarf_Addr pc);

  Dwarf_Addr vardie_from_symtable(Dwarf_Die *vardie, Dwarf_Addr *addr);

  static int add_module_build_id_to_hash (Dwfl_Module *m,
                 void **userdata __attribute__ ((unused)),
                 const char *name,
                 Dwarf_Addr base,
                 void *arg);

  static bool is_gcc_producer(Dwarf_Die *cudie, std::string& producer,
                                                std::string& version);

public:
  Dwarf_Addr pr15123_retry_addr (Dwarf_Addr pc, Dwarf_Die* var);
};

// Template <void> specializations for iterate_over_* functions

template<> void
dwflpp::iterate_over_modules<void>(int (*callback)(Dwfl_Module*,
                                                   void**,
                                                   const char*,
                                                   Dwarf_Addr,
                                                   void*),
                                   void *data);
template<> void
dwflpp::iterate_over_cus<void>(int (*callback)(Dwarf_Die*, void*),
                               void *data,
                               bool want_types);
template<> void
dwflpp::iterate_over_inline_instances<void>(int (*callback)(Dwarf_Die*, void*),
                                            void *data);
template<> void
dwflpp::iterate_over_call_sites<void>(int (*callback)(Dwarf_Die*, Dwarf_Die*, void*),
                                      void *data);
template<> int
dwflpp::iterate_over_functions<void>(int (*callback)(Dwarf_Die*, void*),
                                     void *data, const std::string& function);
template<> int
dwflpp::iterate_single_function<void>(int (*callback)(Dwarf_Die*, void*),
                                      void *data, const std::string& function);
template<> int
dwflpp::iterate_over_globals<void>(Dwarf_Die *cu_die,
                                   int (*callback)(Dwarf_Die*,
                                                   bool,
                                                   const std::string&,
                                                   void*),
                                   void *data);
template<> int
dwflpp::iterate_over_types<void>(Dwarf_Die *top_die,
                                 bool has_inner_types,
                                 const std::string& prefix,
                                 int (* callback)(Dwarf_Die*,
                                                  bool,
                                                  const std::string&,
                                                  void*),
                                 void *data);
template<> int
dwflpp::iterate_over_notes<void>(void *object, void (*callback)(void*,
                                                                const std::string&,
                                                                const std::string&,
                                                                int,
                                                                const char*,
                                                                size_t));
template<> void
dwflpp::iterate_over_libraries<void>(void (*callback)(void*, const char*),
                                     void *data);
template<> int
dwflpp::iterate_over_plt<void>(void *object, void (*callback)(void*,
                                                              const char*,
                                                              size_t));
template<> void
dwflpp::iterate_over_srcfile_lines<void>(char const * srcfile,
                                         const std::vector<int>& linenos,
                                         enum lineno_t lineno_type,
                                         base_func_info_map_t& funcs,
                                         void (* callback) (Dwarf_Addr,
                                                            int, void*),
                                         bool has_nearest,
                                         void *data);
template<> void
dwflpp::iterate_over_labels<void>(Dwarf_Die *begin_die,
                                  const std::string& sym,
                                  const base_func_info& function,
                                  const std::vector<int>& linenos,
                                  enum lineno_t lineno_type,
                                  void *data,
                                  void (* callback)(const base_func_info&,
                                                    const char*,
                                                    const char*,
                                                    int,
                                                    Dwarf_Die*,
                                                    Dwarf_Addr,
                                                    void*));
template<> void
dwflpp::iterate_over_callees<void>(Dwarf_Die *begin_die,
                                   const std::string& sym,
                                   int64_t recursion_depth,
                                   void *data,
                                   void (* callback)(base_func_info&,
                                                     base_func_info&,
                                                     std::stack<Dwarf_Addr>*,
                                                     void*),
                                   base_func_info& caller,
                                   std::stack<Dwarf_Addr> *callers);

template<typename Map, typename Key, typename F>
inline typename Map::mapped_type
dwflpp::cache_lookup_or_fill (Map& map, const Key& key,
                              const cache_fill_key& fill_key, F fill)
{
  typedef typename Map::mapped_type Ptr;
  {
    timed_lock_guard<std::mutex> gl (cache_mutex,
                                     stap_dwarf_timing.cache_wait_ns,
                                     stap_dwarf_timing.cache_hold_ns,
                                     stap_dwarf_timing.cache_n);
    auto it = map.find (key);
    if (it != map.end () && it->second)
      return it->second;
  }

  bool timed = stap_dwarf_timing.enabled.load (std::memory_order_relaxed);
  uint64_t t0 = timed ? dwarf_timing_now_ns () : 0;
  after_you_guard<cache_fill_key> after (cache_fill_inflight, fill_key);
  if (timed)
    {
      stap_dwarf_timing.cache_wait_ns += dwarf_timing_now_ns () - t0;
      stap_dwarf_timing.cache_n++;
    }

  {
    timed_lock_guard<std::mutex> gl (cache_mutex,
                                     stap_dwarf_timing.cache_wait_ns,
                                     stap_dwarf_timing.cache_hold_ns,
                                     stap_dwarf_timing.cache_n);
    auto it = map.find (key);
    if (it != map.end () && it->second)
      return it->second;
  }

  Ptr v = fill ();
  {
    timed_lock_guard<std::mutex> gl (cache_mutex,
                                     stap_dwarf_timing.cache_wait_ns,
                                     stap_dwarf_timing.cache_hold_ns,
                                     stap_dwarf_timing.cache_n);
    Ptr& slot = map[key];
    if (!slot)
      slot = v;
    else if (v && v != slot)
      delete v;
    return slot;
  }
}

template<typename F>
inline void
dwflpp::cache_fill_once (void *id, cache_fill_kind kind,
                         std::set<void*>& done, F fill)
{
  {
    timed_lock_guard<std::mutex> gl (cache_mutex,
                                     stap_dwarf_timing.cache_wait_ns,
                                     stap_dwarf_timing.cache_hold_ns,
                                     stap_dwarf_timing.cache_n);
    if (done.find (id) != done.end ())
      return;
  }

  bool timed = stap_dwarf_timing.enabled.load (std::memory_order_relaxed);
  uint64_t t0 = timed ? dwarf_timing_now_ns () : 0;
  after_you_guard<cache_fill_key> after (cache_fill_inflight,
                                         cache_fill_key (kind, (uintptr_t) id));
  if (timed)
    {
      stap_dwarf_timing.cache_wait_ns += dwarf_timing_now_ns () - t0;
      stap_dwarf_timing.cache_n++;
    }

  {
    timed_lock_guard<std::mutex> gl (cache_mutex,
                                     stap_dwarf_timing.cache_wait_ns,
                                     stap_dwarf_timing.cache_hold_ns,
                                     stap_dwarf_timing.cache_n);
    if (done.find (id) != done.end ())
      return;
  }

  fill ();
  {
    timed_lock_guard<std::mutex> gl (cache_mutex,
                                     stap_dwarf_timing.cache_wait_ns,
                                     stap_dwarf_timing.cache_hold_ns,
                                     stap_dwarf_timing.cache_n);
    done.insert (id);
  }
}

#endif // DWFLPP_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
