#!/usr/bin/env python3
"""Coarse novelty / redundancy signatures for SystemTap tests.

Operates on .exp drivers and/or .stp scripts. Features are intentionally
cheap string/regex heuristics for clustering; they are not a full
semantic model.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

PROBE_FAM_RE = re.compile(
    r"\bprobe\s+([a-zA-Z_][\w.]*)",
)
RUNTIME_FLAGS = (
    ("bpf", re.compile(r"--bpf\b")),
    ("dyninst", re.compile(r"--dyninst\b")),
    ("interactive", re.compile(r"stapi_start|readline_p|\(interactive\)")),
    ("unprivileged", re.compile(r"unprivileged|--privilege")),
)
PASS_RE = re.compile(r"-p\s*([1-5])\b|--vp\s*\d*([1-5])")
BUG_RE = re.compile(r"\b(?:bz|pr|CVE-)\d+", re.I)
CORPUS_RE = re.compile(
    r"pass14_stap_files\s+\$srcdir/(?:\$self|buildok|semok|parseok|transok)"
)


def stap_features(text: str) -> dict:
    probes = sorted(set(PROBE_FAM_RE.findall(text)))
    fams = sorted({p.split(".")[0] for p in probes})
    return {
        "probe_fams": fams,
        "probe_count": len(probes),
        "has_embedded_c": "%{" in text,
        "has_guru": bool(re.search(r"\b-g\b|#!\s*stap.*\s-g", text)),
        "bugish": sorted(set(BUG_RE.findall(text))),
        "n_lines": text.count("\n") + 1,
    }


def exp_features(text: str, path: Path) -> dict:
    runtimes = [name for name, rx in RUNTIME_FLAGS if rx.search(text)]
    passes = sorted(set(PASS_RE.findall(text)))
    # PASS_RE returns tuples; flatten
    flat_passes = sorted({a or b for a, b in passes if a or b})
    return {
        "path": str(path),
        "runtimes": runtimes,
        "passes": flat_passes,
        "iterates_pass14_corpus": bool(CORPUS_RE.search(text)),
        "installtest_gated": "installtest_p" in text,
        "bugish": sorted(set(BUG_RE.findall(text))),
        "n_lines": text.count("\n") + 1,
        "mentions_buildok_dir": "buildok" in text and "pass14" in text,
    }


def signature_hash(feat: dict) -> str:
    blob = json.dumps(feat, sort_keys=True, default=str)
    return hashlib.sha1(blob.encode()).hexdigest()[:12]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    out = []
    for p in args.paths:
        text = p.read_text(errors="replace")
        if p.suffix == ".exp":
            feat = exp_features(text, p)
        else:
            feat = stap_features(text)
            feat["path"] = str(p)
        feat["sig"] = signature_hash(feat)
        out.append(feat)
    if args.json:
        json.dump(out, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        for feat in out:
            print(f"{feat['sig']}  {feat['path']}  {feat}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
