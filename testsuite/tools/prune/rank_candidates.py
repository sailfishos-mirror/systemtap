#!/usr/bin/env python3
"""Rank prune candidates from timing CSV + local tree heuristics.

Reads CSV from parse_exp_timings.py and applies cheap keep/prune hints.
Does not delete anything.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

# Structural redundancy: same buildok/*.stp corpus, different drivers.
MULTI_DRIVE_BUILDOK = {
    "systemtap.pass1-4/buildok.exp",
    "systemtap.pass1-4/buildok-interactive.exp",
    "systemtap.pass1-4/parse-semok.exp",
}

# Prefer sampling / demoting before full delete.
SAMPLE_CANDIDATES = {
    "systemtap.examples/check.exp",
    "systemtap.pass1-4/buildok-interactive.exp",
    "systemtap.pass1-4/parse-semok.exp",
}

# Keep by default (signal or unique mode); still show cost.
KEEP_HINT = {
    "systemtap.pass1-4/buildok.exp": "primary -p4 corpus; RHEL8 FAIL signal",
    "systemtap.pass1-4/semok.exp": "primary -p2 corpus",
    "systemtap.syscall/tp_syscall.exp": "syscall family (accept cost)",
    "systemtap.syscall/syscall.exp": "syscall family (accept cost)",
    "systemtap.syscall/nd_syscall.exp": "syscall family (accept cost)",
    "systemtap.base/parallelism-helgrind.exp": "unique helgrind coverage",
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("timings_csv", type=Path)
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args()

    rows = []
    with args.timings_csv.open() as f:
        for r in csv.DictReader(f):
            rows.append((r["exp"], int(r["seconds"])))
    total = sum(s for _, s in rows) or 1
    rows.sort(key=lambda x: -x[1])

    print(f"# ranked by wall seconds (total {total}s / {total/3600:.2f}h)")
    print(f"# columns: seconds pct hint exp")
    for exp, sec in rows[: args.top]:
        hints = []
        if exp in MULTI_DRIVE_BUILDOK:
            hints.append("multi-drive-buildok-corpus")
        if exp in SAMPLE_CANDIDATES:
            hints.append("wave1-sample/prune")
        if exp in KEEP_HINT:
            hints.append("KEEP:" + KEEP_HINT[exp])
        # arch-ish names
        low = exp.lower()
        if any(a in low for a in ("s390", "ppc", "aarch", "riscv", "sparc", "ia64")):
            hints.append("KEEP:arch-ish-name")
        hint = "; ".join(hints) if hints else "-"
        print(f"{sec:6d}  {sec/total*100:5.1f}%  {hint}  {exp}")

    multi = [(e, s) for e, s in rows if e in MULTI_DRIVE_BUILDOK]
    multi_sum = sum(s for _, s in multi)
    print()
    print(
        f"# buildok multi-drive subtotal: {multi_sum}s "
        f"({multi_sum/total*100:.1f}% of run)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
