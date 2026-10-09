---
name: security-band-aid
description: >-
  Author and land SystemTap emergency security band-aid examples under
  testsuite/systemtap.examples/security-band-aids/, for the Linux kernel,
  system libraries, and applications/daemons. Takes the hand-off bundle
  from the security-band-aid-triage skill (CVE id, upstream patch, minimal
  reproducer) and covers deriving a mitigation from that patch (correct
  suspect data vs. inducing the error path), choosing probe points and context
  variables, testing the script against a live reproducer, then importing /
  canonicalizing a raw oss-sec style script against
  security-bandaid-template.stp / livepatch.stp, adding a .meta file, and
  regenerating the examples index.
---

# Security band-aids (examples)

Emergency / educational SystemTap mitigations live in:

`testsuite/systemtap.examples/security-band-aids/`

(`EXAMPLES` at the tree root is a symlink to `testsuite/systemtap.examples`.)

These are **not** production patches. Mark them experimental / for
reference and education unless experts have vetted them. Prefer preserving
the original mitigation semantics; canonicalize structure, do not invent a
“better” fix without domain review.

A band-aid is a **data patch**, not a code patch: instead of replacing the
vulnerable code, intercept it and mutate the data flowing through it, or
short-circuit it into its own error path. Code that does not run cannot be
exploited; a data patch moves a system from `vulnerable` to `safe`, which is
often enough. See the FOSDEM 2016 talk
`https://archive.fosdem.org/2016/schedule/event/systemtap/` and the
Red Hat Security blog post linked from `security-bandaid-template.stp` for
the underlying reasoning.

## Workflow checklist

```
Security band-aid:
- [ ] Start from the triage hand-off bundle (security-band-aid-triage skill)
- [ ] Confirm the upstream fix's condition maps onto an available variable
- [ ] Find probe points / context variables: stap -L, readelf --debug-dump
- [ ] Decide mitigation style: correct data, or induce the error path
- [ ] Stand up a matching environment (container for userspace, VM for kernel)
      and note the affected / fixed package NVRs
- [ ] Draft the script; test against a real reproducer with stap -g -c CMD
- [ ] Embed the advisory link, upstream commit id, and the hunk being mirrored
- [ ] Confirm the fix is off/on toggleable and counts metrics
- [ ] Add cve-YYYY-NNNN.stp under security-band-aids/
- [ ] Templatize against security-bandaid-template.stp + peers
- [ ] Add matching cve-YYYY-NNNN.meta
- [ ] stap -gp1 (and -p2 when probes exist on the host)
- [ ] Rerun examples-index-gen.pl; commit regenerated indexes
- [ ] Commit; credit original author with --author when appropriate
```

If a ready-made script already exists (oss-sec post, vendor advisory), skip to
"Canonical script shape" and only verify the payload against a reproducer.

## Authoring from scratch

### Candidate criteria

Screening happens in the **security-band-aid-triage** skill
(`.skills/security-band-aid-triage/`), whose hand-off bundle supplies the CVE
id, upstream fix excerpt, minimal reproducer, and the debuginfo situation on
the target hosts. Short version: the bug should be localized, with simple
control flow, and depend on local data visible at a nearby probe point. Bugs
that need a code path the original code lacks still want a real code patch —
say so rather than half-fixing.

### Read the upstream patch as a spec

The upstream fix usually encodes the mitigation compactly (see the two shapes
listed in the triage skill). Translate whichever one applies into handler code:

- **Bound / normalization** → reproduce the expression on the context variable
  at the same point in the flow: `$len = $len % 4096`, mask off high bits,
  clamp with `min`/`max`.
- **Early-return / error branch** → set `$return` to the errno the patch would
  set, or narrow the parameters so the function takes its own error path.

Mirror the patch's *semantics*, not its phrasing. Keep magic numbers explained
by a comment (`-95` // `EOPNOTSUPP`).

### Find probe points and variables

Prefer the narrowest point that sees the suspect value, in this order:

1. `kernel.function("...").call` / `.return` for kernel-side bugs.
2. `process("/usr/bin/foo").function("...")` for userspace, or
   `process("...").statement("*@file:LINE")` when inlining hides the
   statement boundaries. Shared system libraries use the same form with the
   library path: `process("/lib64/libc.so.6").function("_int_malloc")`.
3. `syscall.*` / `nd_syscall.*` only when the condition is expressible from
   arguments alone.

That covers the three target classes from triage: kernel, system library,
application. Add a trailing `?` to a probe point when the path or symbol may
legitimately be absent on some hosts: an unmatched `?` probe point is skipped
instead of failing elaboration. Keep at least one always-available probe (for
example a `timer.s` status line) so the script still has something to attach
to.

Discover what is actually available on the target host — the deployed binary
is fully optimized, so inlining, reordering, register allocation and DWARF
location lists decide what is visible:

```bash
stap -L 'kernel.function("xfs_file_remap_range")'
stap -L 'process("/usr/bin/qemu-system-x86_64").function("fdctrl_write")'
readelf --debug-dump=info BIN | less          # is there usable debuginfo at all?
```

Poor or missing debuginfo shrinks the choice to `$parms` / `$return` at
function boundaries; that is usually still enough for a bound-or-error
mitigation. Note in the script comment which variables the mitigation needs.

If the condition is expressible from syscall/LSM-level arguments alone, prefer
the BPF runtime (`#!/usr/bin/env -S stap -g --runtime=bpf`, `probe lsm.*`) so the
band-aid works on hosts without the module-loading toolchain — see
`cve-2026-31431bpf.stp` next to the module-runtime twin.

### Pick the mitigation style

| Situation | Style |
|---|---|
| Suspect value is cheap to clamp / mask / normalize | Correct the data, let normal control flow continue |
| Hostile input is likely, or correction is not expressible | Induce the error path: set `$return`, or redirect to the function's own error handling |
| Value must survive the call unchanged by the fix | Save on `.call`, restore on `.return` |
| Only some callers/probes are suspect | Gate on `execname()` / `pid()` / a flag in the predicate |

Save/restore must be per-thread, since handlers run atomically and
concurrently:

```stp
global saved
probe process("/usr/bin/qemu-system-*").function("fdctrl_*spec*_command").call {
  saved[tid()] = $fdctrl->data_pos
  $fdctrl->data_pos = $fdctrl->data_pos % 512
}
probe process("/usr/bin/qemu-system-*").function("fdctrl_*spec*_command").return {
  if (tid() in saved) {
    $fdctrl->data_pos = saved[tid()]
    delete saved[tid()]
  }
}
```

Keep the mutation itself in the `.call` handler: writes to context variables
are accepted there, while `.return` handlers read entry-time values (plain
`$var` there is captured as `@entry($var)`) and `$return` is only usable inside
the body, not in the probe predicate. `stap -L POINT.return` lists what each
site actually offers, including the type it gives `$return`.

Writing handlers, in the order the translator complains:

- Test `$variables` in the handler body, not in the probe predicate: a predicate
  is evaluated near entry and may report `unresolved target-symbol expression`
  for a name the body can read. Keep the predicate for `cve_enabled_p` and
  similar globals.
- Target-variable writes accept only plain `=`. For a read-modify-write, put the
  read in the same expression: `...->flags = ...->flags | 2`.
- Dereference `@cast(...)` inline (`@cast(addr, "type", "kernel")->flags`).
  Assigning the `@cast` result to a local first can trip
  `'struct ...' is being accessed instead of a member such as '->field'`.
- Recompute the address expression at each use rather than caching it in a
  local; handlers run atomically and these expressions are cheap.
- Check whether the running kernel already carries the upstream commit
  (`git tag --contains HASH`) before concluding the handler is broken: on a
  fixed kernel the marking branch should stay quiet, which is the right answer.

Worth considering on top of the fix: `printf` the suspicious input while
triaging (`printf("check buffer %s\n", $buf)`), `system("logger ...")` to leave
a record in the system logs, or `raise(9)` to kill a repeatedly abusive victim
process. Keep these behind `cve_notify_p` / `cve_trace_p` so the steady-state
cost stays minimal.

### Build a test environment

Match the environment to the target class from triage.

Userspace targets (libraries, daemons, CLIs) test well in a container: the
shared host kernel is enough, and `--runtime=dyninst` avoids loading anything.

```bash
podman run -i --rm -v $PWD:/w -w /w registry.fedoraproject.org/fedora:latest \
  stap --runtime=dyninst -g -c "./gedank1 hello" cve-YYYY-NNNN.stp
```

Under the dyninst runtime, name the binary explicitly in each probe and confirm
resolution before writing the payload:

```bash
stap --runtime=dyninst -L 'process("/lib64/libc.so.6").function("malloc")'
stap --runtime=dyninst -L 'process("/usr/bin/python3").function("Py_Initialize")'
```

Shared libraries usually resolve richly (their debuginfo is packaged and matched);
an application binary can come back empty if its debuginfo is thin or its build
id mismatches, in which case widen to `.statement("*")` or rebind to a libc
function near the suspect call. Two things `-L` makes obvious: a launcher such as
`/usr/bin/python3` is often a thin wrapper whose real code lives in a library
(`libpython3.15.so`), so bind probes to the library that holds the function; and
the launcher's own `main` lives at the interpreted-entry file, where
`.statement(...)` still resolves.

Kernel targets need a VM: a container inherits the host kernel, so the guest's
own build is what is actually under test. Take a cloud image whose shipped
kernel predates the fix, then compare against the updated one. Record both NVRs.

Record the version boundary while setting this up, since it belongs in the
commit message and in the `.meta` description:

```bash
rpm -q kernel-core                 # what the guest boots with today
rpm -q --changelog kernel-core | grep -m3 -i "CVE-2026-46300"   # backport present?
```

Sources for the affected/fixed pair: the distro advisory for the CVE
(`RHSA-…`, `LSA-…`), and the matching `kernel-NVR` names in its package list.
Typical shape for a RHEL 10-family guest: affected `6.12.0-124.45.1.el10_1`,
fixed `6.12.0-124.56.3.el10_1` or later; RHEL 10.0 EUS fixes at
`6.12.0-55.75.1.el10_0`, RHEL 10.2 at `6.12.0-211.16.1.el10_2`. Other streams
carry their own backports with their own numbering (RHEL 9, RHEL 8 E4S, and the
kpatch live-patch series each have separate errata), so read the CVE page's
errata list per stream rather than assuming one NVR pattern covers all. Note the
pair in the commit message so a later rebase can tell whether the band-aid is
still needed.

Compiling for the guest does not have to happen on the guest. Build where the
debuginfo lives and ship the artifact:

```bash
stap -g -m CVE_YYYY_NNNN -p4 cve-YYYY-NNNN.stp        # MODULE.ko on the build box
scp MODULE.ko guest: && ssh guest staprun MODULE.ko cve_fix_p=1
stap -g --remote guest cve-YYYY-NNNN.stp              # or: passes 1-4 here, run there
```

Otherwise install `kernel-debuginfo` matching `uname -r` in the guest itself and
check what the probe sees: `stap -L 'kernel.function("skb_try_coalesce")'`.

### Test against a reproducer

Write the smallest thing that shows the bug, then compare before/after. Use
`-c` so the script and its victim share a lifetime — stap runs the command, and
detaches when it finishes — and `-g` for context-variable writes:

```bash
./gedank1 "$(python3 -c 'print("x"*40)')"     # baseline: crash / wrong rc
stap -g cve-YYYY-NNNN.stp -c "./gedank1 hello-world-input"
```

Give every test run a bounded lifetime. `-c COMMAND` is the usual form;
otherwise add `-T SECONDS`, an explicit `exit()` in a handler, or
`-x PID`/`-L`. Without one of these, stap keeps the module attached and keeps
running even when the script only has `begin`/`end` probes, which stalls
non-interactive test loops.

Check all four quadrants: hostile input is neutralized, ordinary input still
works, `cve_fix_p=0` leaves the original behavior visible, and
`cve_enabled_p=0` costs about nothing. Iterate trace-only first (`cve_fix_p=0`)
to confirm the probe fires on realistic traffic and sees the expected values,
then turn the payload on. Watch `stap -p2` output for unresolved probe points
or `$` variables before claiming success, and check
`/proc/systemtap/MODULE_NAME/__prometheus` counts rise only on real hits.

For repeated toggling during testing, drive the procfs knobs rather than
reloading:

```bash
echo 0 > /proc/systemtap/CVE_YYYY_NNNN/cve_fix_p
```

### Carry the upstream reference

Every band-aid should make itself auditable against the real fix, from the file
alone:

- Cite the advisory *and* the upstream change: distro/advisory url, plus the
  upstream commit id (`a664bf3d603d`) or lore.kernel.org message id, and the
  first tag or package release that carries it. That is what tells a later
  reader whether the band-aid can be dropped once hosts are on that build.
- Paste the relevant hunk into a block comment under the explanation, trimmed to
  the condition the handler reproduces. Keep the commit message's one-line
  summary too.
- When a variant is needed per kernel/library version, hold the alternatives in
  conditional annotations instead of duplicated handlers. The form is
  `%( COND %? TOKENS %)` — tokens are kept when the condition holds, dropped
  otherwise — so a fallback block can sit right beside the primary one:

  ```
  %( kernel_v >= "6.12" %?
  probe kernel.function("skb_try_coalesce").call { $to->pp_recycle = 1 }
  %)
  ```

Auditing then means: read the pasted hunk, compare its condition with the probe
predicate and payload, and confirm `cve_count_metric` counters move on the real
reproducer. Mention in the `.meta` `description` when the band-aid is only a
stopgap until the fixed package is available.

## Canonical script shape

Models: `security-bandaid-template.stp`, `cve-2018-6485-templatized.stp`,
`cve-2026-31431.stp`, `cve-2026-64600.stp`.

1. **Shebang / module name** (guru mode; stable `/proc/systemtap` name):

   ```
   #!/usr/bin/env -S stap -g -m CVE_YYYY_NNNN
   ```

   `env -S` is what splits the trailing flags into separate arguments on every
   `exec`-style interpreter; a plain `#! /usr/bin/stap -g -m NAME` may arrive as
   one glued argument. See `info coreutils 'env invocation'`. Give the file the
   executable bit so the shebang is used at all, and remember stap itself
   ignores this line when invoked as `stap SCRIPT`.

2. **Short comment** explaining the CVE, what the payload does, and the
   references described above: advisory link, upstream commit id, first fixed
   release (e.g. seclists post + `a664bf3d603d` + `v6.13`).

3. **Do not reimplement `probe begin` / `probe end` load messages.**
   The `tapset/livepatch.stp` tapset already prints
   `"%s mitigation loaded/unloaded\n"` when `cve_notify_p` is set. Raw
   oss-sec scripts often duplicate that; drop those probes when templatizing.

4. **Gate every actionable probe** with `if (cve_enabled_p)` (probe
   predicate or body). Apply the actual mutation only when `cve_fix_p`.
   Print per-hit diagnostics only when `cve_notify_p`.

5. **Metrics:** call `cve_count_metric("hit")` for each suspect event
   (and `"miss"` when the script distinguishes clean vs bad paths). Optional
   `probe timer.s(60)` status line matching peer band-aids.

6. **Procfs / prometheus** come from `livepatch.stp` automatically when
   those symbols are used. End with a note like:

   ```
   # Take a look at /proc/systemtap/CVE_YYYY_NNNN/* for parameters and prometheus metrics
   ```

7. **Globals from livepatch.stp** (defaults are all enabled / notifying):
   `cve_notify_p`, `cve_fix_p`, `cve_trace_p`, `cve_enabled_p`,
   `cve_tmpdisabled_s`, plus `cve_count_metric` / `cve_record_metric` /
   `cve_tmpdisable`. Runtime knobs appear under
   `/proc/systemtap/CVE_YYYY_NNNN/`.

Keep the original payload (parameter overwrites, `$return` errno, `raise`,
etc.) unless correcting an obvious transcription error. Document magic
constants (e.g. `-95` // `EOPNOTSUPP`).

## `.meta` file

Companion `cve-YYYY-NNNN.meta` beside the `.stp`. Peer style for current
experimental band-aids:

```
title: cve-YYYY-NNNN security band-aid
name: cve-YYYY-NNNN.stp
keywords: security guru
description: EXPERIMENTAL emergency security band-aid, for reference/education only
test_check: stap -gp1 cve-YYYY-NNNN.stp
```

Use `historical` instead of `EXPERIMENTAL` when the band-aid is old and
kept only for education. See `testsuite/systemtap.examples/README` for the
full `.meta` vocabulary.

## Regenerate the examples index (required)

Adding or changing `.meta` / example listing text is incomplete until the
generated indexes are refreshed. **Rerun:**

```bash
cd testsuite/systemtap.examples && perl examples-index-gen.pl
```

That regenerates (at least) `index.txt`, `keyword-index.txt`, and the HTML
counterparts from all `.meta` files. Requires Perl `DBI`.

**Do not** hand-edit `index.txt` / `keyword-index.txt` alone. Commit the
regenerated index files in the same change as the new `.stp` / `.meta`
(or immediately after). Skipping this leaves the web/example catalogs
stale — recent `cve-2026-*` band-aids were easy to miss for that reason.

The pre-release skill also regenerates this index before a release; still
do it when landing the band-aid so master stays consistent.

## Verify

- `stap -gp1 path/to/cve-YYYY-NNNN.stp` — always (also what `test_check` runs
  via `check.exp`).
- `stap -g -m CVE_YYYY_NNNN -p2 path/to/cve-YYYY-NNNN.stp` when the probed
  module/function exists on the build host (e.g. `xfs` loaded). Guru `-g`
  is required for context-variable writes; the `#!/usr/bin/env -S stap ...`
  line only takes effect when the script is executed directly, so repeat the
  flags when invoking `stap SCRIPT`. Direct execution resolves `stap` from
  `PATH`, which may be an older installed build than the tree being tested —
  its runtime headers can then fail pass 4 where `./stap` succeeds.
- Prefer build-tree tapsets if a stale `$prefix` install causes
  `livepatch.stp` / procfs mismatches:
  `SYSTEMTAP_TAPSET=$PWD/tapset ./stap ...`

General install/test quirks: `AGENTS.md`.

## Commit authorship

When the band-aid originates from a named author (oss-sec poster, etc.),
credit them as the git author:

```bash
git commit --author="Name <email@example.com>" -m "$(cat <<'EOF'
Add experimental CVE-YYYY-NNNN security band-aid.

Short why / source link.
EOF
)"
```

Keep the message focused on why it is in-tree (education / emergency
reference), link the advisory, and state the affected / fixed package pair the
band-aid was checked against, so a later reader can tell when it became
redundant.
