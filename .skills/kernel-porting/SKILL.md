---
name: kernel-porting
description: >-
  Port SystemTap's kernel-module runtime and related tapsets across Linux
  kernel versions using STAPCONF autoconf probes, exportconf, and script-level
  fallbacks. Use when adapting to a new kernel, fixing buildok/semok failures
  from API or struct drift, adding runtime/linux/autoconf-*.c probes, replacing
  KERNEL_VERSION checks, or debugging module compile errors against kernel
  headers.
---

# Kernel porting (SystemTap runtime)

Adapt SystemTap so the **kernel-module runtime** builds and runs against a
target kernel (new upstream, enterprise backports, longterm). Prefer feature
detection over version numbers.

General build/test quirks live in `AGENTS.md` (**Testing Quirk**,
compilation phases). This skill owns kernel-porting detail.

## Layers

| Layer | Where | Detection |
|-------|--------|-----------|
| Runtime C | `runtime/linux/*.c`, `runtime/linux/*.h`, `runtime/*.h` | `#ifdef STAPCONF_*` from autoconf / exportconf |
| Embedded C in scripts | `%{ ... %}` in `.stp` / tapsets | same `STAPCONF_*` |
| Tapset / probes | `tapset/linux/*.stp`, examples, tests | `@defined`, `@choose_defined`, `@type_member_defined`, probe `!` chains |

This skill focuses on **runtime** porting. Tapset/DWARF fixes often ride along
when `buildok`/`semok` fail; use the script-level patterns below when the
breakage is probe symbols or context variables, not C API shape.

## Diagnose first

1. **Existence-check in Bunsen** when the report is from CI or “new kernel”
   breakage. Bunsen holds months of Sourceware buildbot testruns across
   kernels and architectures. Query it (MCP or
   `https://builder.sourceware.org/testrun/<hash>`) to see whether the
   FAIL is new, limited to one kernel/arch, or already present elsewhere —
   that frames whether you need a fresh port vs a latent bug. Useful MCP
   moves: find testruns by kernel/`uname` metadata, list FAIL cases for a
   run, diff two testruns, open the per-case log. See also `AGENTS.md`
   (**Sourceware upstream CI and Bunsen**).
2. Reproduce with the same pass the failure uses:
   - semantic / DWARF: `stap -p2 ...` or `make check RUNTESTFLAGS="semok.exp" CHECK_ONLY="..."`
   - module compile: `stap -p4 ...` or `make check RUNTESTFLAGS="buildok.exp" CHECK_ONLY="..."`
3. Keep temps: `stap -k -p4 ...` and inspect `/tmp/stap*/` generated C plus
   kbuild errors.
4. **Identify the kernel patch(es)** that changed the type, field, function,
   or macro you must work around (see below). Do not guess from version
   numbers alone.
5. Prefer **fixing** the code path over disabling tests. Before adding a
   kfail, read the comments above the `switch` in
   `testsuite/systemtap.pass1-4/buildok.exp`.

Do not declare victory on a hand-run alone; run the relevant dejagnu `.exp`
(and `make install` + `sudo -E make installcheck` when the case is runtime).

### Find the responsible kernel commit

Before writing a shim, learn *what* upstream changed and *why*:

1. Need a **kernel git tree** (full history preferred). Common locals:
   `/home/*/linux`, nearby checkouts, or the running build's source under
   `/lib/modules/$(uname -r)/source` or `.../build` (often not a full git
   repo). If none is available or history is too shallow/grafted, **ask the
   user** for a path to a usable kernel git checkout.
2. Search for the rename or removal, e.g.:
   ```bash
   git -C "$KERNEL_TREE" log -S'old_ident' --oneline -- include/linux/foo.h
   git -C "$KERNEL_TREE" log -G'relevant_pattern' --oneline -- path
   git -C "$KERNEL_TREE" log -p -1 <commit> -- path
   ```
   Compare tags around the breakage (`v6.12` vs `v7.0`) when bisecting by
   content is awkward.
3. Record commit subject/hash (and new vs old names) in the compatibility
   comment next to the shim. Lore/bunsen/buildbot logs help when git history
   is unavailable, but prefer the actual patch when possible.

## Prefer STAPCONF over KERNEL_VERSION

Autoconf probes compile tiny snippets against the **target kernel headers** at
module build time. That correctly detects backported APIs on older
`LINUX_VERSION_CODE` values (common on RHEL/enterprise kernels).

Reserve `KERNEL_VERSION()` / `LINUX_VERSION_CODE` only when there is no
reliable compile-time hook (rare legacy cases).

### Add an autoconf probe

1. Create `runtime/linux/autoconf-<name>.c` — minimal C that uses the new API,
   type, or header. It must compile under the kernel build's `-Werror`.
2. Register it in `buildrun.cxx`:
   ```c
   output_autoconf(s, o, cs, "autoconf-<name>.c",
                   "STAPCONF_FOO", NULL);
   ```
3. Guard runtime / embedded C:
   ```c
   #ifdef STAPCONF_FOO
     /* new API */
   #else
     /* old API */
   #endif
   ```

**Header-only existence** can be a one-line include probe (e.g.
`autoconf-linux-filelock-h.c` → `STAPCONF_LINUX_FILELOCK_H`).

**Signature variants:** write a wrapper that only type-checks the desired
prototype (see `autoconf-get_user_pages-no-mm.c`). Chain several
`STAPCONF_*` arms in the runtime header (`access_process_vm.h` style).

### exportconf vs autoconf

| Helper | Use when |
|--------|----------|
| `output_autoconf` | API/type/header must **compile** against kernel headers |
| `output_exportconf` | Symbol must be **exported/linkable** (`/proc/kallsyms` exports), not merely declared |
| `output_dual_exportconf` / `output_either_exportconf` | Need both or either of two exports |

Examples: `STAPCONF_FILES_LOOKUP_FD_RAW`, `STAPCONF_DO_SOCK_GETSOCKOPT`,
`STAPCONF_LINUX_FILELOCK_H`, `STAPCONF_GET_USER_PAGES_NO_MM`.

## Runtime edit patterns

- Keep compatibility shims next to the call sites (`runtime/linux/runtime.h`,
  `runtime/compatdefs.h`, subsystem headers under `runtime/linux/`).
- Prefer `#ifndef old_alias` shims when the kernel only dropped a compatibility
  macro (e.g. `rdmsrl` → `rdmsrq`) over version gates.
- Match existing style: short comments citing the kernel commit/rename, then
  `#ifdef STAPCONF_*` branches.
- After changing runtime used by installcheck, `make install` so `$prefix`
  picks up the new files.

## Script / tapset side (when buildok is not "just C")

- Existence: `stap -l 'kernel.function("*pattern*")'`
- Locals: `stap -L 'kernel.function("fn")'`
- Fallbacks (required final alternative):  
  `kernel.function("old") !, kernel.function("new")`
- Renamed params: `@choose_defined($old, $new)` (e.g. `$__argv`/`$argv`,
  `$page`/`$folio`)
- Missing symbols: `@defined($var)` guards (e.g. `do_mpage_readpage` now
  takes `struct mpage_readpage_args` with `$args->folio`)
- Struct fields: `@type_member_defined("struct foo", bar)`
- Forward-declared pointers:  
  `@cast(ptr, "struct nsproxy", "kernel<linux/nsproxy.h>")->mnt_ns`

### Retain old-kernel compatibility

SystemTap still targets enterprise kernels back to **RHEL8-era 4.18**.
When porting for a new upstream kernel, **keep** the old symbols, fields,
and probe aliases whenever they are harmless on newer kernels:

- Prefer additive fallbacks (`!` / `?` chains, extra optional aliases,
  `@choose_defined`, `@defined`, `@type_member_defined` if/else) over
  deleting the pre-change path.
- Optional probe aliases that simply fail to resolve on modern kernels
  are fine (e.g. keep `scsi_prep_fn` beside `scsi_prepare_cmd`).
- Do not replace an old-only path with a new-only path unless the old
  API is truly gone *and* no optional fallback can express it.
- STAPCONF `#else` arms and legacy `#ifdef` shims exist for the same
  reason in runtime C — extend them, do not strip the old side.

### Folio / VFS migration notes

- Page helpers: prefer `@choose_defined($page, $folio)` with existing
  `__page_*` helpers where possible.
- `vfs.remove_from_page_cache`: add `filemap_remove_folio` fallbacks;
  folio-era kernels removed `__delete_from_page_cache`.
- `nfs.aop.writepage`: add `nfs_do_writepage` fallbacks (`$folio` not
  `$page`).
- Embedded C (`%{ ... %}`): use `#ifdef FOLIO_MAPPING_ANON` for
  `PAGE_MAPPING_ANON` / `PageSwapCache` → `folio_test_*` / `page_folio()`
  in `ioblock.stp`-style helpers.
- `struct mount` iteration: `mnt_instance` + `list_head` `s_mounts` on
  older kernels; walk `sb->s_mounts` via `mnt_next_for_sb` when
  `@type_member_defined("struct mount", mnt_instance)` is false.
## Verify

```bash
# targeted compile smoke
stap -p4 -e 'probe begin { exit() }'

# failing subtests only (from repo top level)
make check RUNTESTFLAGS="buildok.exp" CHECK_ONLY="the-subtest"

# if runtime behavior is involved
make install
sudo -E make installcheck RUNTESTFLAGS="your.exp"
```

Read `testsuite/systemtap.log` (or the per-test `.log`): kbuild/gcc stderr in
captured output can FAIL matchers even when a manual `stap` looks fine.

## Anti-patterns

- Blind `KERNEL_VERSION` gates for APIs that enterprise kernels backport
- Shimming without identifying the kernel patch that forced the change
- Dropping old-kernel fallbacks (RHEL8 / 4.18) when an optional alias or
  `@choose_defined` would keep them working
- Disabling or kfailing an entire tapset test for one probe — extract or fix
  the broken path
- Declaring done after `stap -p4` without the matching dejagnu run
- Huge autoconf probes — keep them minimal so `-Werror` failures stay obvious
