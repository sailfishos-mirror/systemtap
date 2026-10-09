---
name: security-band-aid-triage
description: >-
  Decide whether a CVE or other bug in the Linux kernel, a system library, or
  an application/daemon is a good candidate for a SystemTap emergency
  band-aid, and assemble the inputs an agent needs before writing one. Favors
  high-severity issues on components that keep running through the mitigation
  window, since milder bugs are better served by a routine update. Use when
  picking or screening candidates from oss-sec/seclists, vendor advisories, or
  distro changelogs, when asked whether a given CVE can be mitigated without a
  restart, or when gathering the CVE id, upstream patch, severity, and minimal
  reproducer for the security-band-aid skill.
---

# Security band-aid triage

Turn a pile of advisories into one well-specified band-aid candidate. Output
of this stage is the hand-off bundle at the bottom; writing and landing the
script itself belongs to the **security-band-aid** skill
(`.skills/security-band-aid/`).

Band-aids are a stopgap for systems that cannot take a real update quickly:
an unavailable vendor package, a service that must not restart, a private or
abandoned component. If a plain package update or service restart is
acceptable today, prefer that and skip the band-aid.

## Severity first

Band-aid only what is genuinely urgent. Attaching handlers to hot paths is a
real cost, and it buys nothing when the underlying bug is rarely triggered. In
practice the candidates worth this treatment are the high-severity ones; for
anything milder, the normal update cycle is the better answer and is what most
hosts should use.

Rough screening order for the severity of a candidate:

- Does hostile input reach the component without authentication — an
  externally reachable listener, a parser for network or container image data,
  a setuid helper, a library linked by nearly every process?
- Do the distro advisories or the CVE record rate it high / important /
  critical, or describe remote exploitation?
- Does the buggy path run on essentially every request (so an unpatched window
  is continuously exposed), rather than on rare administration-time operations?

High severity plus a slow update path is exactly the case worth a band-aid. Low
or moderate severity with a routine update pending: document the choice and move
on. Also check whether the distributor already ships a live patch for the same
CVE (kpatch / kGTM style): where one exists and loads cleanly, take it and skip
the script.

## Primary targets

Aim at code that is always resident and always in the data path, in this order:

| Target | Typical shape | Probe family |
|---|---|---|
| Linux kernel (built-in or loadable module) | syscall path, parser, allocator in a hot path | `kernel.function("...")`, `kernel.trace("...")`, `syscall.*` |
| System libraries | `libc`, `libssl`, resolver / name-service paths used by every process | `process("/lib64/libc.so.6").function("...")` |
| Applications / daemons | long-lived server or agent that keeps running through the mitigation window | `process("/usr/sbin/foo").function(...)` or `.statement("*@file:LINE")` |

Long-lived matters: the handler has to stay attached for the mitigation to keep
protecting the host, so a resident daemon or a shared library beats a
short-lived CLI. Kernel-side fixes usually cover every process at once and are
worth preferring when the same condition can be expressed from syscall-level
arguments.

Closest peers in `testsuite/systemtap.examples/security-band-aids/` per class:
kernel — `cve-2013-2094.stp`, `cve-2016-0728.stp`, `cve-2026-31431.stp`,
`cve-2026-64600.stp`; libraries — `cve-2015-0235.stp`; applications —
`cve-2014-7169.stp` (bash), `cve-2015-3456.stp` (qemu).

Lower value: one-shot CLI helpers and fork-per-request workers, where a restart
is cheap enough to be the better answer, and binaries whose debuginfo cannot be
obtained anywhere for the compiling machine (see the checks below).

## Where to look

- `https://seclists.org/oss-sec/` — most in-tree band-aids cite an oss-sec
  post; match the year/quarter index.
- Distro advisories / bodhi / RHSA-style notices for the target hosts. Their
  package lists give the affected-vs-fixed NVR pair directly, which is what the
  test environment and the commit message need.
- Upstream changelogs and the component's own fix commits, to see how small
  the real fix is.
- Existing examples in `testsuite/systemtap.examples/security-band-aids/` —
  check the CVE is not already covered, and reuse the closest peer's shape.

## Is this a band-aid candidate?

Band-aids work when the bug is *localized* and depends on data visible at a
nearby probe point. Count how many of these hold; the more, the better the
fit:

| Attribute | Fits a data patch |
|---|---|
| Source available to read (needed to reason about it) | yes |
| Vulnerability already analyzed, minimal reproducer exists | yes |
| Bug is localized to one function / a few statements | yes |
| Control flow around it is simple | yes |
| Bug depends on local data (a length, a flag, an index, a refcount) | yes |
| That data is accessible as a context variable at a probe point | yes |
| Trigger condition is unambiguous / cheap to test in the handler | yes |

Poor fits: bugs whose correct behavior depends on global state spread across
many functions, or that need a code path the original code does not have.
Those still want a real code patch; say so rather than half-fixing.

Two quick practical checks before committing to a candidate:

- Is the affected component actually loaded/running on the target hosts
  (`lsmod`, `rpm -q`, binary present)? A module-level mitigation on a host
  that never loads the module is wasted work — and an easy way to get a
  no-match probe point at pass 2.
- Is matching debuginfo available **where the script gets compiled**? That is
  the machine where `$variables` are read out of DWARF, so it is the one that
  benefits from rich debuginfo. Deployed hosts need no debuginfo at all: they
  only load the finished artifact. Build locally and ship the module with
  `-p4` + `staprun MODULE.ko` (top-level globals become module options, so
  `staprun MODULE.ko cve_fix_p=0` tunes the patch at load time), or let
  `--remote HOST` run passes 1-4 locally and copy/run on the target; a shared
  compile server (`--use-server`, see `stap-server`(8)) works the same way for a
  fleet. `debuginfod` is the usual way to pull matching debuginfo onto the build
  box.

Contrast the easy case with a closed-source application shipping a stripped
binary and no debuginfo: even on a well-provisioned build host there may be
little to bind beyond entry/exit of a few functions, so the mitigation tends to
degrade to coarse `$return` overrides and probe-point conditions. Prefer
components whose debuginfo is packaged (kernel, glibc, and most distro
packages), where the same script can be recompiled later for a new kernel or
library version without re-deriving the whole thing.

## Read the upstream fix

Skim the upstream commit alongside the advisory and classify it, since this
shapes the script:

- **Bound or normalization** (`pos %= SIZE`, added length check, `!= 0`
  guard) → correct suspect data at the same point in the flow.
- **New early-return / error branch** → induce that error path with the same
  errno.

A one-function guard is the ideal shape. Multi-file refactors, cached-state
invalidation, or fixes that only work because the compiler sees them inline
tend to resist faithful reproduction from a probe handler.

Capture provenance while reading, since the next stage embeds it for auditing:
the commit id, the lore.kernel.org or list message id, and which branches or
tags already contain it (`git log -1 --oneline a664bf3d603d`,
`git branch -a --contains`, or the bunsen upstream-commit tools for
stable/backport coverage). Paste the hunk itself into the hand-off notes rather
than paraphrasing it — the point of the exercise is to compare the handler's
condition against what upstream actually wrote.

## Hand-off bundle

Collect these before writing anything; they are what the next stage consumes:

```
Target class: kernel | library | application
CVE / advisory id + link + severity rating (and why: remote input? every-request path?)
Component + package NVR, and which hosts run it
Affected-vs-fixed package pair per stream (e.g. RHEL 10.1: affected
6.12.0-124.45.1.el10_1, fixed 6.12.0-124.56.3.el10_1), taken from the distro
advisory's package list
Where the bug is best reproduced: container for userspace, VM for kernel code
Upstream fix: commit id / lore URL, the diff excerpt of the condition it adds,
and the first release or package NVR that contains it
Minimal reproducer command + expected good-vs-bad observable output
Where compilation happens (local box, --remote host, --use-server) and what
matching debuginfo it can reach
```

Then continue with the **security-band-aid** skill for authoring the `.stp`,
templatizing against `security-bandaid-template.stp` / `livepatch.stp`, adding
the `.meta`, and regenerating the examples index.
