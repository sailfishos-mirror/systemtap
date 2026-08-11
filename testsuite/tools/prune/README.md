# Testsuite prune research toolkit

Goal: shrink SystemTap `installcheck` wall clock (~12h on
`stap-fedrawhide-x86-64`) without losing unique bug-finding power.

## Farm coverage caveat

Sourceware builders are **not** a complete distro/arch matrix:

| Role | Hosts | What runs |
|------|-------|-----------|
| Full installcheck | `stap-{rhel8,rhel9,rhel10,c9s,c10s,f39..f44,fedrawhide}-x86-64` | ~10k cases, ~12h |
| Smoke / stripped `make check` | `stap-fedrawhide-{aarch64,ppc64le,s390x,riscv64}` | tiny subset (e.g. `cu-decl`, `warnings`) |

Implications for pruning:

- **"Always PASS on the farm" ≠ universally redundant.** Arch-specific,
  endian, or page-size sensitive tests may only be exercised off-farm.
- Prefer deleting **x86-redundant translator/driver duplication** the
  farm fully exercises, not rare-arch coverage that looks idle in Bunsen.
- When scoring novelty, treat underrepresented arches as keep-by-default
  unless the test is clearly x86-only or pure language/syntax.

## Data sources

1. Bunsen testrun `testsuite/systemtap.log` — `TIMESTAMP_MS` per case and
   `testcase ... completed in N seconds` per `.exp`.
2. Local tree — `.exp` drivers + `buildok/`/`semok/`/`*.stp` inventories.
3. Historical FAIL/XFAIL diffs across recent master testruns (x86_64
   full + note smoke-only non-x86).

## Escape hatches (Wave 1)

| Knob | Effect |
|------|--------|
| `INTERACTIVE_BUILDOK_FULL=1` | Restore full `buildok/*.stp` walk in `buildok-interactive.exp` |
| `PARSE_SEMOK_FULL=1` | Restore full `buildok/` (and with verbose: semok+transok) in `parse-semok.exp` |
| `EXAMPLES_INSTALLCHECK_FULL=1` | Restore all example installcheck *runs* in `check.exp` |
| `CHECK_ONLY=...` | As before; also forces full run selection for the above drivers |

See `CANDIDATES.md` for cost analysis and Wave-1 status.

## Tools

- `parse_exp_timings.py` — turn a saved log (or Bunsen dump) into CSV.
- `exp_signature.py` — coarse novelty features for an `.exp` / `.stp`.
- `rank_candidates.py` — join cost + signature + keep rules → ranked list.
- `KEEP_RULES.md` — hard do-not-delete / prefer-keep policies.
- `CANDIDATES.md` — living first-wave report (refresh from new timings).

## Suggested workflow

```bash
# After downloading/saving a systemtap.log from Bunsen:
python3 parse_exp_timings.py /path/to/systemtap.log > exp_timings.csv
python3 rank_candidates.py exp_timings.csv > ranked.txt
```

Then manually review top cost × low-novelty rows against `KEEP_RULES.md`
and historical FAIL signal before deleting or sampling.
