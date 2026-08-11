#!/usr/bin/env python3
"""Parse DejaGnu systemtap.log for per-.exp wall times.

Accepts a full log file or a Bunsen show_testrun_file dump that still
contains lines like:
  testcase .../systemtap.base/foo.exp completed in 37 seconds
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import defaultdict
from pathlib import Path

COMPLETED_RE = re.compile(
    r"testcase .*/(?P<exp>systemtap(?:\.[^/\s]+)*/[^/\s]+\.exp"
    r"|systemtap/[^/\s]+\.exp) completed in (?P<sec>\d+) seconds"
)


def parse(text: str) -> list[tuple[str, int]]:
    rows: list[tuple[str, int]] = []
    for m in COMPLETED_RE.finditer(text):
        rows.append((m.group("exp"), int(m.group("sec"))))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path, help="systemtap.log or Bunsen dump")
    ap.add_argument(
        "--summary",
        action="store_true",
        help="print directory rollups to stderr",
    )
    args = ap.parse_args()
    rows = parse(args.log.read_text(errors="replace"))
    if not rows:
        print("no completions matched", file=sys.stderr)
        return 1

    w = csv.writer(sys.stdout)
    w.writerow(["exp", "seconds"])
    for exp, sec in rows:
        w.writerow([exp, sec])

    if args.summary:
        total = sum(s for _, s in rows)
        by_dir: dict[str, int] = defaultdict(int)
        n_dir: dict[str, int] = defaultdict(int)
        for exp, sec in rows:
            d = exp.split("/", 1)[0]
            by_dir[d] += sec
            n_dir[d] += 1
        print(
            f"parsed {len(rows)} .exp, total {total}s ({total/3600:.2f}h)",
            file=sys.stderr,
        )
        for d, sec in sorted(by_dir.items(), key=lambda kv: -kv[1]):
            print(
                f"  {sec:7d}s {sec/total*100:5.1f}% n={n_dir[d]:3d}  {d}",
                file=sys.stderr,
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
