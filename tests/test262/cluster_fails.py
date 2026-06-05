#!/usr/bin/env python3
"""Cluster test262 failures by normalized failure SIGNATURE (cross-path), not by
spec-area path. Path clustering groups tests by what they test; signature
clustering groups them by why they fail -- which is what you need to find a
single root cause that flips many tests.

Usage:
    python cluster_fails.py                 # top failure signatures
    python cluster_fails.py --sig "<text>"  # list tests matching a signature substring
    python cluster_fails.py --crashes       # just the crash subset (for -g re-triage)

Key metric: a signature with a HIGH count concentrated in FEW paths is the best
single-root-cause candidate. A high count spread over many paths is heterogeneous
(many small causes sharing a message) -- harder, lower ROI per fix.
"""
import json, collections, re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
JSONL = os.path.join(HERE, ".test262_results.jsonl")


def load():
    seen = {}
    with open(JSONL, "rb") as f:
        for raw in f:
            try:
                r = json.loads(raw.decode("utf-8", "ignore"))
                seen[r.get("path", "")] = r
            except Exception:
                pass
    return seen


def signature(r):
    """Normalize a failure into a structural signature: drop concrete values
    (quoted strings, numbers, hex, the U+FFFD-delimited SameValue payloads) so
    that '...SameValue(?3?, ?4?)...' and '...SameValue(?7?, ?9?)...' collapse."""
    st = r.get("status")
    if st != "fail":
        return f"[{st}]"
    s = (r.get("reason") or "").strip()
    s = re.sub(r"^Uncaught:?\s*", "", s)
    s = re.sub(r"�.*?�", "V", s)   # SameValue ?X? markers
    s = re.sub(r'"[^"]*"', "'S'", s)
    s = re.sub(r"\b0x[0-9a-fA-F]+\b", "H", s)
    s = re.sub(r"\b\d+\b", "#", s)
    s = re.sub(r"\s+", " ", s).strip()
    return s[:90]


def is_crash(r):
    blob = (r.get("reason") or "") + " " + (r.get("stderr") or "")
    return ("VectoredException" in blob or "CRASH" in blob
            or "access violation" in blob.lower())


def main():
    seen = load()
    if "--sig" in sys.argv:
        needle = sys.argv[sys.argv.index("--sig") + 1]
        for p, r in seen.items():
            if r.get("status") == "fail" and needle in (r.get("reason") or ""):
                print(p.replace("\\", "/"))
        return
    if "--crashes" in sys.argv:
        for p, r in seen.items():
            if r.get("status") == "fail" and is_crash(r):
                print(p.replace("\\", "/"))
        return

    buckets = collections.Counter()
    paths = collections.defaultdict(set)
    for p, r in seen.items():
        if r.get("status") in ("pass", "skip"):
            continue
        g = signature(r)
        buckets[g] += 1
        parts = p.replace("\\", "/").split("/")
        paths[g].add("/".join(parts[:3]))

    print(f"total non-pass/non-skip: {sum(buckets.values())}   distinct signatures: {len(buckets)}")
    print(f"\n{'count':>6} {'#paths':>6} {'conc':>5}  signature")
    print("  (conc = count/#paths; HIGH conc = concentrated = best single-cause candidate)")
    # sort by concentration among reasonably-large buckets
    rows = [(g, n, len(paths[g])) for g, n in buckets.items() if n >= 20]
    rows.sort(key=lambda t: (t[1] / t[2], t[1]), reverse=True)
    for g, n, np_ in rows[:30]:
        print(f"{n:6d} {np_:6d} {n/np_:5.0f}  {g}")


if __name__ == "__main__":
    main()
