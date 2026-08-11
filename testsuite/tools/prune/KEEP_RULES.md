# Keep rules (guardrails)

Apply before any deletion or aggressive sampling.

## Hard keep

1. **Bugzilla / PR / CVE / security-band-aid** regressions (`bzNNNN`,
   `prNNNN`, `security-band-aids/`, CVE-named scripts).
2. **Only coverage** for a probe family, runtime (`--bpf`, `--dyninst`),
   or language feature (singleton signature in the corpus).
3. **Arch / ABI / endian / page-size** specific tests — farm full runs
   are x86_64-only; non-x86 builders are smoke.
4. **Privilege / namespace / container** edge cases that are not
   duplicated by a cheaper sibling.
5. Tests that **caught real regressions** in recent Bunsen history
   (FAIL that later flipped PASS after a fix), excluding chronic noise.

## Prefer keep (needs strong justification to prune)

1. Kernel-portability `buildok` / `semok` scripts that fail on older
   distros (RHEL8) even when green on fedrawhide.
2. Runtime execution oracles (installcheck `-p5`) that are the only
   check beyond `-p2`/`-p4` for that behavior.
3. Flaky-but-unique stress / onthefly / helgrind coverage — prefer
   fixing flakes over deleting the only stress signal.

## Prefer prune / sample

1. **Same `.stp` corpus driven multiple ways** with no distinct oracle
   (classic: `buildok.exp` + `buildok-interactive.exp` + `parse-semok.exp`
   all iterating `buildok/*.stp`).
2. **Chronic FAIL noise** that never gates merges (e.g. interactive
   "cached compile: no cache use") — fix or cut; do not treat as signal.
3. **Near-duplicate language microtests** that only differ by identifier
   spelling once a parameterized representative exists.
4. Example scripts whose `-p4` path is already covered by tapset
   `*-detailed.stp` buildok entries **and** whose installcheck run is
   a short `-T 1` smoke with no unique assertion.

## Farm-blindness checklist (ask before deleting)

- Does this test name/arch-gate for non-x86?
- Would a ppc64le/s390x/riscv64 developer lose their only local canary?
- Is the "always UNTESTED" result on x86_64 intentional gating?
