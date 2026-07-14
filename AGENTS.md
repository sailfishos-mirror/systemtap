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

## Installation Quirk

Development builds may install to a local prefix for convenience:
```bash
configure --prefix=`pwd`/INST   # Convenient for local build
make all install                # Installs to ./INST/
```

After a `make install`, either ./INST/bin/stap or ./stap may be used.

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
- After changing the translator/runtime, installcheck uses `$prefix`;
  rebuild with `make install` so it exercises the code you actually changed.

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

## Sourceware upstream CI

[builder.sourceware.org](https://builder.sourceware.org) runs fedrawhide
on slow emulated non-x86_64 VMs, so most arches get a stripped smoke
`make check` only (`cu-decl.exp`, `warnings.exp`, etc.) — not
`buildok.exp`. Full `buildok`/`check` coverage is on the native x86_64
installcheck builder. Bunsen stores those testrun logs if you have MCP
access; otherwise use local `make check`.
