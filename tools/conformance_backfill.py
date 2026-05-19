#!/usr/bin/env python3
"""
One-shot backfill of conformance snapshots from documented milestone numbers.

Reads MILESTONES below (hardcoded from memory + commit messages), looks up the
commit date via git, and appends one row per milestone to snapshots.jsonl.

The current docs/conformance/*.md totals are used for typescript_features,
ecmascript_features, nodejs_apis — those are slow-moving and recent commits
don't change them much, so a slight retroactive smoothing is acceptable.

Run once after creating the dashboard. Don't re-run; it appends duplicates.
"""
from __future__ import annotations
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SNAPSHOTS = REPO_ROOT / "docs" / "conformance-dashboard" / "snapshots.jsonl"

# Each entry: (short_hash, label, test262_pass, test262_fail, test262_ce,
#              golden_ir_pass, node_pass, refdiff_match, refdiff_total)
# Drawn from MEMORY.md and recent commit messages. Values are the
# best-attested snapshots from sessions that documented them.
MILESTONES = [
    # 2026-05-11 - end of parser early-error spike + Intl polish session
    ("17.137 baseline (post-Intl polish)", "5cd0939",
     17137, None, None, 265, 295, None, 27),
    # 2026-05-17 - Groups F-O refdiff-driven session end
    ("17.907 — Groups F-O", "299cf0f",
     17907, None, None, 265, 295, 17, 27),
    # 2026-05-18 - this session's starting point (pre-Phase A)
    ("17.920 — session start", "81e26f7",
     17920, 16894, 0, 265, 295, 17, 27),
    # 2026-05-18 - after Phase A async try/catch
    ("Phase A: async try/catch", "42143cf",
     None, None, None, 265, 295, None, 27),
    # 2026-05-18 - after Phase B await reject
    ("Phase B: await reject", "4ce88d5",
     None, None, None, 265, 295, 18, 27),
    # 2026-05-18 - after Boehm purge (no test262 movement)
    ("Boehm GC purge", "c669067",
     None, None, None, 265, 295, None, 27),
    # 2026-05-18 - after flat default (refdiff 18->19)
    ("ts_array_flat default", "20d5019",
     None, None, None, 265, 295, 19, 27),
    # 2026-05-18 - after closure-of-const fix
    ("closure-const module global", "d0261ec",
     17919, 16792, 3, 265, 295, 19, 27),
]


def git_commit_date(hash_short: str) -> str | None:
    """Return ISO-8601 UTC timestamp of the commit, or None."""
    try:
        ts = subprocess.check_output(
            ["git", "log", "-1", "--format=%cI", hash_short],
            cwd=REPO_ROOT, text=True, stderr=subprocess.DEVNULL).strip()
        if not ts:
            return None
        # Normalize to "Z" form for sortability.
        return ts.replace("+00:00", "Z")
    except Exception:
        return None


def git_subject(hash_short: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "log", "-1", "--format=%s", hash_short],
            cwd=REPO_ROOT, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return ""


def main() -> int:
    # Use the docs/conformance/*.md TOTAL rows for the conformance matrices.
    # These are mostly stable; backfilled rows show the same values as the
    # latest snapshot. For finer-grained matrix history, track the .md TOTAL
    # row diffs through git log separately (out of scope for backfill).
    from conformance_snapshot import (
        parse_conformance_md, parse_nodejs_summary)

    ts_feat = parse_conformance_md(REPO_ROOT / "docs" / "conformance" / "typescript-features.md")
    es_feat = parse_conformance_md(REPO_ROOT / "docs" / "conformance" / "ecmascript-features.md")
    node_apis = parse_nodejs_summary(REPO_ROOT / "docs" / "conformance" / "nodejs-features.md")

    SNAPSHOTS.parent.mkdir(parents=True, exist_ok=True)

    appended = 0
    with SNAPSHOTS.open("a", encoding="utf-8") as out:
        for label, hsh, t262_p, t262_f, t262_ce, gpass, npass, rmatch, rtotal in MILESTONES:
            ts = git_commit_date(hsh)
            if not ts:
                print(f"[skip] {hsh}: no commit date", file=sys.stderr)
                continue
            row = {
                "timestamp": ts,
                "git": {"commit": hsh, "short": hsh[:7], "branch": "master",
                        "subject": git_subject(hsh)},
                "label": label,
                "backfilled": True,
            }
            if t262_p is not None:
                # Approximate total from known constituents; the exact total
                # depends on the corpus and skip rules at that commit.
                # Real numbers from the most recent sweep give us the
                # baseline of ~50,500. Backfilled rows are best-effort.
                t262 = {"pass": t262_p}
                if t262_f is not None: t262["fail"] = t262_f
                if t262_ce is not None: t262["compile_error"] = t262_ce
                # Skip count is consistent across sessions: ~15,756 from the
                # most recent measurement. Use it as a stand-in.
                t262["skip"] = 15756
                t262["total"] = t262_p + (t262_f or 0) + (t262_ce or 0) + 15756
                row["test262"] = t262
            if gpass is not None:
                row["golden_ir"] = {"pass": gpass, "fail": 277 - gpass, "total": 277}
            if npass is not None:
                row["node"] = {"pass": npass, "fail": 297 - npass, "total": 297}
            if rmatch is not None and rtotal is not None:
                row["refdiff"] = {"match": rmatch, "diff": rtotal - rmatch - 1,
                                  "crash": 1, "total": rtotal}
            if ts_feat: row["typescript_features"] = ts_feat
            if es_feat: row["ecmascript_features"] = es_feat
            if node_apis: row["nodejs_apis"] = node_apis

            out.write(json.dumps(row, separators=(",", ":"), ensure_ascii=False) + "\n")
            appended += 1
            print(f"[+] {ts}  {hsh}  {label}")

    print(f"\nBackfilled {appended} milestone rows.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
