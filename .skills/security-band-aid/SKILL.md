---
name: security-band-aid
description: >-
  Add or canonicalize SystemTap emergency security band-aid examples under
  testsuite/systemtap.examples/security-band-aids/. Use when importing a CVE
  mitigation from oss-sec or similar, templatizing a raw stap script against
  security-bandaid-template.stp / livepatch.stp, adding a .meta file, or
  regenerating the examples index after new band-aids.
---

# Security band-aids (examples)

Emergency / educational SystemTap mitigations live in:

`testsuite/systemtap.examples/security-band-aids/`

(`EXAMPLES` at the tree root is a symlink to `testsuite/systemtap.examples`.)

These are **not** production patches. Mark them experimental / for
reference and education unless experts have vetted them. Prefer preserving
the original mitigation semantics; canonicalize structure, do not invent a
“better” fix without domain review.

## Workflow checklist

```
Security band-aid:
- [ ] Obtain the original stap script (oss-sec, advisory, author)
- [ ] Add cve-YYYY-NNNN.stp under security-band-aids/
- [ ] Templatize against security-bandaid-template.stp + peers
- [ ] Add matching cve-YYYY-NNNN.meta
- [ ] stap -gp1 (and -p2 when probes exist on the host)
- [ ] Rerun examples-index-gen.pl; commit regenerated indexes
- [ ] Commit; credit original author with --author when appropriate
```

## Canonical script shape

Models: `security-bandaid-template.stp`, `cve-2018-6485-templatized.stp`,
`cve-2026-31431.stp`, `cve-2026-64600.stp`.

1. **Shebang / module name** (guru mode; stable `/proc/systemtap` name):

   ```
   #! /usr/bin/stap -g -m CVE_YYYY_NNNN
   ```

2. **Short comment** explaining the CVE, what the payload does, and a
   pointer to the source advisory (e.g. seclists).

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
  is required for context-variable writes.
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
reference) and link the advisory.
