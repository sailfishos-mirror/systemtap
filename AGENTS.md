# SystemTap Agent Guide

This document covers SystemTap-specific peculiarities for AI agents.

## Build System Quirk

Unlike most projects, SystemTap **commits autoconf/automake generated files to git**:
- `configure` (from `configure.ac`)
- `Makefile.in` (from `Makefile.am`)
- `aclocal.m4`

**When you modify build configuration:**
```bash
vim Makefile.am          # Edit source
autoreconf -i            # Regenerate
git add Makefile.am Makefile.in configure   # Commit BOTH
git commit
```

Some installed text files are produced from `*.in` templates by
`config.status` (path/`$prefix` substitution), not checked into git as
the final form.  Edit the template, then regenerate:

```bash
vim man/stap.1.in        # source of truth
make -C man stap.1       # or: ./config.status man/stap.1
```

Examples: `man/stap.1.in` → `man/stap.1`, `man/stappaths.7.in`,
`man/stap-onboot.8.in`.  Do **not** hand-edit the generated file alone.

## Compatibility targets

Floor for active support/CI is roughly **RHEL 8 and later** (kernel
**4.18** through current upstream / Fedora rawhide).  Host builds use
both **GCC and Clang**; some builders enable `-Werror`, so do not
hardcode compiler-specific `-Wno-*` flags in `Makefile.am` — probe
with `AX_CHECK_COMPILE_FLAG` in `configure.ac` (pass `[-Werror]` as
extra flags) and `AC_SUBST`, same pattern as `ALIGNEDNEW`.

## Installation Quirk

Development builds may install to a local prefix for convenience:
```bash
configure --prefix=`pwd`/INST   # Convenient for local build
make all install                # Installs to ./INST/
```

After a `make install`, either ./INST/bin/stap or ./stap may be used.

**Always use `make install` to refresh `$prefix` (e.g. `./INST/`).** Do not
`cp` individual tapset, runtime, or binary files into the install tree.
`installcheck` and installed `stap` load tapsets, runtime headers, and
auxiliary tools from `$prefix` together; a one-off copy can leave mixed
old/new trees and produce misleading results.

## Testing Quirk

SystemTap has two different test modes:

```bash
make check                  # Non-root: compilation/semantic tests only, uses build tree only
sudo -E make installcheck   # Root: full runtime tests, takes many hours, uses $prefix/install tree
```

The `-E` flag on sudo is needed to preserve environment variables
related to debuginfod usage.

**Important:** Always run test commands from the top-level directory,
not from the `testsuite/` subdirectory. The build system handles
entering the testsuite directory automatically.

Run specific tests:

```bash
make check RUNTESTFLAGS="test_name.exp"
sudo -E make installcheck RUNTESTFLAGS="test_name.exp"
```

To run individual subtests within an `.exp` file (like `check.exp` or
`buildok.exp`), use the `CHECK_ONLY` variable along with `RUNTESTFLAGS`:

```bash
make check RUNTESTFLAGS="buildok.exp" CHECK_ONLY="nfs-detailed nfs-fop.check_flags"
make check RUNTESTFLAGS="check.exp" CHECK_ONLY="badname fntimes"
```

**Do not declare victory until dejagnu is green.** A manual `stap -l`,
`-p4`, or ad-hoc `-p5` run is not enough when you add or change tests.

- Run the new `.exp` under dejagnu: `make check RUNTESTFLAGS="your.exp"`.
- If the test has runtime cases (`installtest_p`), also run
  `make install` then `sudo -E make installcheck RUNTESTFLAGS="your.exp"`.
  `make check` alone marks those subtests UNTESTED, not PASS.
- Run those tests sequentially, not concurrently, as they'd scribble over
  each other's files and logs.
- Read `testsuite/systemtap.log` (or the per-test `.log`) before
  finishing; stderr from kbuild/gcc can appear in captured output and
  break `.exp` matchers even when a hand-run looks fine.
- After changing the translator, tapsets, runtime, or helper binaries,
  installcheck uses `$prefix`; run `make install` (not ad-hoc `cp`) so
  the install tree stays consistent and exercises the code you changed.
- For load-sensitive flakes (stdout ordering, timing races), check whether
  `stress-ng` (or `stress`) is on `PATH` and re-run the test under CPU
  load, e.g. `stress-ng --cpu 0 --timeout 60s` in the background.

### Valgrind on new translator code

When adding or changing translator / DWARF / parse paths, briefly
exercise the new code under valgrind memcheck during development (and
prefer a cheap `-p2`/`-p4` grind in the related `.exp` when practical):

```bash
valgrind --tool=memcheck --error-exitcode=1 --trace-children=no \
  ./stap -p2 path/to/script.stp
```

`--trace-children=no` keeps gcc/kbuild out of the grind so you are
checking `stap` itself. Treat new definite leaks or invalid
reads/writes in the translator as blockers. For locking/parallelism
changes, see the optional helgrind coverage in
`testsuite/systemtap.base/parallelism-helgrind.exp`.

## Debugging Quirks

### Compilation Phases

Stop at specific compilation phases to debug issues:

```bash
stap -p1 script.stp    # Parse only
stap -p2 script.stp    # Elaboration/semantic analysis (most script errors appear here)
stap -p3 script.stp    # Translate to C (shows generated C file path)
stap -p4 script.stp    # Compile C to kernel module .ko
stap -p5 script.stp    # Run (default)
```

### Keep Temporary Files

```bash
stap -k script.stp     # Keep /tmp/stap*/ directory after compilation
```

This preserves the generated C code and intermediate files for inspection.
The directory path is printed during compilation (look for "Keeping temporary directory").

Generated C files can be 100k+ lines for complex scripts with many probes.

### Verbose Output and Test Suites

Using general verbosity flags like `-vv` prints the SystemTap version header to `stdout`, which can break exact `stdout` expectations in `.exp` test cases. To get compiler debug messages (e.g., optimization logs from pass 2) without polluting `stdout`, use per-pass verbosity instead:
```bash
stap --vp 02 script.stp
```

**Do not add new `clog`/`cerr` diagnostics at `verbose < 2`.** Most of
the testsuite runs `stap -v` (`sess.verbose == 1`). Extra lines there
show up as unexpected output: dejagnu matchers fail, and runtime cases
often sit until the ~3 minute timeout. Gate new timing / debug dumps at
`sess.verbose > 2` (`-vvv`, or `--vp` for that pass), or behind an
explicit env var (e.g. `STAP_DWARF_TIMING`). Do not print “just for `-v`”
(`verbose > 0`).

## Script Language Quirks

### Field access (`->` vs `.`)

In stap script (not C embedded in `%{ ... %}`), **`->` is polymorphic**:
use it for both struct-pointer member access and struct-value member
access. Do **not** use C-style `.` for fields.

**`.` is for string concatenation only**, e.g. `"foo" . "bar"`.

Wrong (`.count` may parse as a local identifier, not a member):

```stp
val = mm->rss_stat[member].count
```

Right:

```stp
val = @cast(&mm->rss_stat[member], "percpu_counter", "kernel")->count
```

### `--compatible` gating for new language features

`--compatible=VERSION` exists so **old scripts keep working on newer
SystemTap**.  Gate new syntax / `@operators` at the *dispatch* site with
`input.has_version("X.Y")` (see `@enum` / `@enumname` / `@kregister` in
`parse.cxx`).  Below that floor the name should stay unrecognized — the
same “unknown operator” path as before the feature existed — **not** a
new parse error saying “requires --compatible=X.Y or higher”.  Throwing
that error would break the whole point of the option.  Register the
at-word in `lexer::atwords` under the same `has_version` check.

### Unprivileged / `stapusr` language limits

See **UNPRIVILEGED USERS** in `man stap` (`man/stap.1.in`).  Short version:

- `--unprivileged` ≡ `--privilege=stapusr`.
- Untagged embedded-C (`%{ ... %}` and tapset helpers without
  `/* unprivileged */` or `/* myproc-unprivileged */`) is **rejected at
  pass 2** under `stapusr`/`stapsys`.  That rejection is the security
  mechanism, not a translator bug.
- `/* myproc-unprivileged */` helpers may only run after `is_myproc()` is
  true; otherwise the script exits.
- `stapusr` may only use `process.*` against the user’s own processes
  (plus begin/end/timer/…).
- Prefer `if`/`else` over script ternary `?:` in stapusr-safe scripts;
  ternary has shown up as “embedded expression may not be used when
  --privilege=stapusr”.
- Probe-point trailing `?` makes resolution optional (missing symbols do
  not fail the script).  It does **not** waive embedded-C checks for
  forms that *do* resolve.  Under stapusr, avoid co-arming overlapping
  aliases in one script (`process.plt` vs `process.plt("*")`,
  `process.mark` vs `process.provider(...).mark`, bare `process.FOO` vs
  `process("./path").FOO`, …) — shared expansions can pull in untagged
  embedded-C and fail the whole batch.
- When the translator synthesizes `embedded_expr` / `%{…%}` (opt passes,
  probe combining, rewrites of `next`, etc.), give it an appropriate
  safety tag (`/* unprivileged */` or `/* myproc-unprivileged */`) if
  the code is safe for those privilege levels — otherwise stapusr/stapsys
  reject the whole script at pass 2.

## Runtime Options

When running a script directly from the local build tree, always use `sudo -E` to preserve necessary environment variables (like `LD_LIBRARY_PATH` or `DEBUGINFOD_URLS`):
```bash
sudo -E ./stap path/to/script.stp
```

SystemTap supports three different runtimes with different privilege and capability levels:

### Kernel Module Runtime (default)
```bash
stap script.stp        # Full-featured, requires root
```
- Full language and tapset support
- Requires root privileges to load kernel modules
- Uses kernel module (.ko) for probe handlers
- Most powerful but highest privilege requirement

### BPF Runtime
```bash
stap --bpf script.stp  # Limited features, requires root for
```
- Uses eBPF instead of kernel modules
- More restricted language features (no arbitrary loops, limited string operations)
- Some probe types not supported
- Maybe safer than kernel modules (BPF verifier checks)
- Still requires root for most kernel probes

### Dyninst Runtime
```bash
stap --dyninst script.stp   # Userspace only, can run non-privileged
```
- Userspace probes only (process.*, function(), etc.)
- No kernel probe support
- Can run without root privileges (for own processes)
- Dynamic binary instrumentation via dyninst library

**Note:** Not all tapsets and language features work in all
  runtimes. The default kernel module runtime has the most complete
  support.

## Commit Conventions

When writing Git commit messages, wrap the text to approximately 70
characters per line to conform with standard Git formatting practices.

## Kernel Compatibility Portability

For porting the runtime/tapsets across kernel versions (STAPCONF
autoconf, probe fallbacks, folio migration, buildok/semok triage),
use the **kernel-porting** skill (`.skills/kernel-porting/`).

## DWARF / loc2stap challenges

For hard DWARF cases (`DW_OP_entry_value`, pretty-printed `$$parms$` /
`$$vars$`, hunting probe PCs with `readelf --debug-dump=loc`), use the
**dwarf-challenges** skill (`.skills/dwarf-challenges/`).

## Security band-aid examples

For importing or templatizing CVE emergency band-aids under
`testsuite/systemtap.examples/security-band-aids/` (including
regenerating indexes with `examples-index-gen.pl`), use the
**security-band-aid** skill (`.skills/security-band-aid/`).

## Sourceware upstream CI and Bunsen

[builder.sourceware.org](https://builder.sourceware.org) runs fedrawhide
on slow emulated non-x86_64 VMs, so most arches get a stripped smoke
`make check` only (`cu-decl.exp`, `warnings.exp`, etc.) — not
`buildok.exp`. Full `buildok`/`check` coverage is on the native x86_64
installcheck builder.

**Bunsen** archives months of those build/test testruns across kernels
and architectures. Use it for **existence checks** before (or while)
debugging: whether a FAIL is new, kernel-specific, arch-specific, or
already gone on other configs. Web UI testrun pages are
`https://builder.sourceware.org/testrun/<commit-hash>`. With bunsen MCP
tools available, query testruns/metadata, list PASS/FAIL cases, diff two
runs, and pull per-case logs; otherwise fall back to local `make check`.
Detailed triage for kernel-porting failures is in
`.skills/kernel-porting/SKILL.md`.
