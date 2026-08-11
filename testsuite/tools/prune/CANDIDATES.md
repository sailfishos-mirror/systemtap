## Wave 3 status — IMPLEMENTED + installcheck confirmed (2026-08-11)

Skip 43 early `systemtap.base` language microtests by default
(~1260s / ~21min on fedrawhide). Subsumed by `buildok`/`semok`,
`systemtap.printf/`, `systemtap.maps/`, and larger base drivers
(`alias_suffixes`, `cast-scope`, …).

Escape: `BASE_MICROTESTS_FULL=1`

Shared gate: `base_microtest_skip_p` in `lib/systemtap.exp`.

Kept deliberately (not in skip list): `atomic`, `addr_op`, `abort`,
`exit`, `beginenderror`, `timers`, `argv`, bug regressions, SDT, etc.

installcheck: all 43 gated files → UNTESTED in ~seconds;
`BASE_MICROTESTS_FULL=1` `add.exp` → 6 PASS in 34s.

---

## Wave 2 status — IMPLEMENTED + installcheck confirmed (2026-08-11)

Targets (prior fedrawhide cost → local smoke installcheck):

| Driver | Was | Now | Escape hatch |
|--------|----:|----:|--------------|
| `unprivileged_myproc.exp` | 1967s | 157s | `UNPRIVILEGED_MYPROC_FULL=1` |
| `sdt_misc.exp` | 1154s | 279s | `SDT_MISC_FULL=1` |
| `tracepoints_list.exp` | 1190s | 26s | `TRACEPOINTS_LIST_FULL=1` |
| `listing_mode.exp` | 934s | 57s | `LISTING_MODE_FULL=1` |
| **Wave-2 quartet** | **~87 min** | **~9 min** | |

Approach: curated smoke / aggregate census / V3-only SDT; completeness
FAILs demoted to UNTESTED in smoke mode.

installcheck: 198 PASS, 2027 UNTESTED (intentional smoke), 6 XFAIL,
1 FAIL (`sdt_misc wildcard (55) V3_uprobe`) — V3 path unchanged by
sampling; local count drift, not a prune regression.

---

## Wave 1 status — IMPLEMENTED + installcheck confirmed (2026-08-11)

Edits landed (escape hatches preserve old behavior):

1. `systemtap.pass1-4/buildok-interactive.exp` — default smoke list of
   10 scripts; full corpus via `INTERACTIVE_BUILDOK_FULL=1`.
2. `systemtap.pass1-4/parse-semok.exp` — default curated unparser sample
   (12 scripts); full via `PARSE_SEMOK_FULL=1`.
3. `systemtap.examples/check.exp` — still builds all examples; skips
   pure `-T 1` installcheck *runs* (keeps `-c`, longer `-T`, security
   band-aids). Full runs via `EXAMPLES_INSTALLCHECK_FULL=1`.
   Also fixed BPF/dyninst run paths to use the matching
   `test_installcheck_{bpf,dyninst}` tags.

### installcheck (Wave-1 group)

```text
sudo -E make installcheck \
  RUNTESTFLAGS="buildok-interactive.exp parse-semok.exp check.exp"
```

| Driver | Wall | vs prior fedrawhide |
|--------|-----:|--------------------:|
| `check.exp` | 1818s | was 5771s |
| `buildok-interactive.exp` | 71s | was 3750s |
| `parse-semok.exp` | 33s | was 2289s |
| **trio total** | **~32 min** | **was ~3.3h** |

Results: **389 PASS**, **125 UNTESTED** (mostly intentional “sampled out”),
**1 FAIL** — `systemtap.examples/general/py3example run` (HelperSDT
circular-import / `PYTHONPATH` on this host; still selected by `-c`,
unrelated to sampling; passed on recent Bunsen fedrawhide).

No FAILs in interactive or parse-semok.

---

# First-wave prune candidates (research notes)

Snapshot from Bunsen testrun `88aea0c31180440548a2b71db816e03dda9348a6`
(`stap-fedrawhide-x86-64.local`, master `release-5.5-208-g97e0c4514`,
2026-08-11). Per-`.exp` times from `testcase ... completed in N seconds`
(480 completions, **46809s ≈ 13.0h** reported; TIMESTAMP_MS span ≈ 12.2h).

## Cost shape (not what we guessed)

| Directory / driver | Seconds | % of run | Notes |
|--------------------|--------:|---------:|-------|
| `systemtap.base` (309 `.exp`) | 18257 | 39.0% | long tail of many medium tests |
| `systemtap.pass1-4` (14 `.exp`) | 11134 | **23.8%** | dominated by re-driving `buildok/` |
| `systemtap.examples/check.exp` | 5771 | **12.3%** | ~199 metas, mostly build+run |
| `systemtap.unprivileged` | 2696 | 5.8% | `unprivileged_myproc.exp` alone 1967s |
| `systemtap.syscall` | 2214 | **4.7%** | lower than the ~15% intuition; keep for now |
| `systemtap.onthefly` | 1730 | 3.7% | |
| `systemtap.bpf` | 1680 | 3.6% | |

### Top individual `.exp` files

| Seconds | % | File | Wave-1 take |
|--------:|--:|------|-------------|
| 5771 | 12.3 | `examples/check.exp` | sample / demote many to check-only |
| 3750 | 8.0 | `pass1-4/buildok-interactive.exp` | **strong prune/sample** |
| 3013 | 6.4 | `pass1-4/buildok.exp` | KEEP (primary corpus) |
| 2289 | 4.9 | `pass1-4/parse-semok.exp` | **prune/sample** |
| 1967 | 4.2 | `unprivileged/unprivileged_myproc.exp` | investigate later |
| 1245 | 2.7 | `syscall/tp_syscall.exp` | accept cost |
| 1190 | 2.5 | `base/tracepoints_list.exp` | later |
| 1154 | 2.5 | `base/sdt_misc.exp` | later |
| 999 | 2.1 | `base/parallelism-helgrind.exp` | KEEP |

**Structural smoking gun:** `buildok.exp`, `buildok-interactive.exp`, and
`parse-semok.exp` all iterate the same **`buildok/*.stp` (223 scripts)**.
Combined ≈ **9052s ≈ 19% of wall clock** on one corpus.

## Historical signal (Bunsen)

- `buildok.exp` on **RHEL8** still shows real FAILs (tapset/portability:
  `nfs-detailed`, `syscall.stp`, `floatingpoint`, …). Keep as primary.
- `buildok-interactive.exp` on fedrawhide/RHEL8 is crowded with chronic
  `cached compile: no cache use` FAILs — noise, not merge-gating signal.
- `parse-semok.exp`: no FAILs in the sampled fedrawhide run; oracle is
  “`-p1` output still passes `-p2`”, largely implied by successful `-p4`
  in `buildok.exp`.
- `examples/check.exp`: no FAILs in that fedrawhide snapshot; expensive
  happy-path demo runs.

Cross-host note: only **57** case-level diffs between latest fedrawhide
x86_64 and RHEL8 for `*buildok*` groups — enough to prove older distros
still matter; not a reason to triple-drive every script on every host.

## Farm variety (why we cannot prune by “always green” alone)

Full installcheck hosts (x86_64): RHEL 8/9/10, CentOS Stream 9/10,
Fedora 39–44, fedrawhide.

Non-x86 fedrawhide (`aarch64`/`ppc64le`/`s390x`/`riscv64`): **smoke only**
— log ~0.8MB vs ~68MB on x86_64; skips almost the entire suite.

So: do **not** delete arch-gated or rare-ABI tests just because Bunsen
x86_64 never fails them. Prefer cutting **duplicate x86 translator
driving** the farm already hammers.

## Recommended Wave 1 (no code deleted yet)

1. **`buildok-interactive.exp`** — replace full 223-script loop with a
   small interactive smoke set (cache hit/miss, guru, one tapset, one
   userspace). Est. save ~**1.0h** if cut to ≤10 scripts.
2. **`parse-semok.exp`** — keep a dedicated handful of unparser roundtrip
   scripts (or verbose-only expansion); stop re-walking all of `buildok/`
   on every installcheck. Est. save ~**0.5–0.6h**.
3. **`examples/check.exp`** — inventory which examples add assertions
   beyond overlapping tapset `*-detailed` buildok coverage; demote
   pure `-T 1` smokes to `make check` / periodic / sampled installcheck.
   Est. save **0.5–1.5h** depending on aggressiveness.

Combined realistic Wave-1 target: **~2–3 hours** off the ~12h run, with
low risk if KEEP rules are followed.

## Wave 2 (research next)

- `unprivileged_myproc.exp` (1967s): matrix explosion?
- `tracepoints_list.exp` / `listing_mode.exp` / `sdt_misc.exp`
- `systemtap.base` long tail: cluster near-duplicate `.exp` pairs
- `printf/` + `maps/` microtest families (many tiny `.exp`, low total %)
- Optional: TIMESTAMP_MS case-level Pareto (finer than `.exp` rollup)

## Refresh commands

```bash
# After saving a Bunsen systemtap.log:
python3 parse_exp_timings.py --summary /tmp/systemtap.log > exp_timings.csv
python3 rank_candidates.py exp_timings.csv
```

Timing CSV for this snapshot: `exp_timings_88aea0c3.csv`.
