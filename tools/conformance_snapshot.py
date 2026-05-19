#!/usr/bin/env python3
"""
Capture a single point-in-time snapshot of ts-aot conformance metrics.

Appends one JSON object (one line) to docs/conformance-dashboard/snapshots.jsonl.
Designed to be re-runnable; idempotent if no inputs have changed.

Reads (in order of priority, all best-effort):
  - tests/test262/.test262_results.jsonl        — latest test262 sweep
  - tests/golden_ir/.golden_ir_baseline.json    — latest golden_ir suite
  - tests/node/.node_test_baseline.json         — latest node suite
  - tests/refdiff/*                              — last refdiff result (if any)
  - docs/conformance/typescript-features.md     — parsed for ✅/⚠️/❌/🔬/N/A
  - docs/conformance/ecmascript-features.md     — parsed for ✅/⚠️/❌
  - docs/conformance/nodejs-features.md         — parsed for module-level percentages

If a source is missing or partial, the corresponding field is omitted from the
snapshot rather than zero-filled — this keeps the time series interpretable
("no data" is distinct from "zero").

Usage:
    python tools/conformance_snapshot.py              # capture now
    python tools/conformance_snapshot.py --dry-run    # print to stdout only
    python tools/conformance_snapshot.py --label X    # tag this row (e.g. for backfill)
"""
from __future__ import annotations
import argparse
import collections
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DASHBOARD_DIR = REPO_ROOT / "docs" / "conformance-dashboard"
SNAPSHOTS_PATH = DASHBOARD_DIR / "snapshots.jsonl"


def git_info() -> dict:
    """Capture commit hash, short hash, branch, and the latest commit subject."""
    def run(args: list[str]) -> str:
        try:
            return subprocess.check_output(
                ["git"] + args, cwd=REPO_ROOT, text=True,
                stderr=subprocess.DEVNULL).strip()
        except Exception:
            return ""

    return {
        "commit": run(["rev-parse", "HEAD"]),
        "short": run(["rev-parse", "--short", "HEAD"]),
        "branch": run(["rev-parse", "--abbrev-ref", "HEAD"]),
        "subject": run(["log", "-1", "--pretty=%s"]),
    }


def count_test262() -> dict | None:
    path = REPO_ROOT / "tests" / "test262" / ".test262_results.jsonl"
    if not path.exists():
        return None
    counts = collections.Counter()
    for raw in path.read_bytes().splitlines():
        try:
            r = json.loads(raw.decode("utf-8", "ignore"))
            counts[r.get("status", "other")] += 1
        except Exception:
            pass
    if not counts:
        return None
    out = {k: counts[k] for k in (
        "pass", "fail", "skip", "timeout", "compile_error", "crash", "other")
        if counts.get(k)}
    out["total"] = sum(counts.values())
    return out


def count_baseline(path: Path) -> dict | None:
    """A baseline file maps test path -> 'pass'|'fail'|'skip'..."""
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    counts = collections.Counter(data.values())
    out = {k: counts[k] for k in counts}
    out["total"] = sum(counts.values())
    return out


def count_refdiff() -> dict | None:
    """Refdiff has no persistent baseline today; do a fresh run and parse stdout."""
    # Skip if the refdiff runner isn't present or node isn't on PATH.
    runner = REPO_ROOT / "tests" / "refdiff" / "runner.py"
    if not runner.exists():
        return None
    try:
        out = subprocess.run(
            [sys.executable, str(runner)],
            cwd=REPO_ROOT, text=True, capture_output=True, timeout=300)
    except Exception:
        return None
    # Parse the summary lines: "  Match: 19", "  Diff: 7", etc.
    # The runner emits ANSI color codes around the numbers, so strip them first.
    ansi_re = re.compile(r"\x1b\[[0-9;]*m")
    counts: dict = {}
    for raw_line in out.stdout.splitlines():
        line = ansi_re.sub("", raw_line)
        m = re.search(r"^\s+(Match|Diff|Crash|Compile error|Timeout|No-node skip|Total):\s+(\d+)", line)
        if m:
            key = m.group(1).lower().replace(" ", "_")
            counts[key] = int(m.group(2))
    return counts or None


def parse_conformance_md(path: Path) -> dict | None:
    """Read the doc's own **TOTAL** summary row and any prose totals.

    The conformance matrices each maintain a summary table with a TOTAL row.
    Column schemas differ between docs (TS has type-only/N/A, ES has total/%),
    so we capture the first three core columns and then look for prose totals
    alongside ("Conformance: X/Y", "Overall ECMAScript Conformance: 221/230").
    """
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    out: dict = {}

    # TOTAL row: capture all numeric **bold** cells after **TOTAL**.
    m = re.search(r"\|\s*\*\*TOTAL\*\*\s*\|([^\n]+)", text)
    if m:
        cells = re.findall(r"\*\*(\d+)\*\*", m.group(1))
        if len(cells) >= 3:
            out["implemented"] = int(cells[0])
            out["partial"] = int(cells[1])
            out["not_implemented"] = int(cells[2])
        # 4th numeric cell is "Total" in the ES doc, "Type-Only" in the TS doc.
        # We disambiguate by checking for an "N/A" column in the schema.
        # Header line above TOTAL tells us which:
        hdr = re.search(r"\|\s*Category\s*\|[^\n]+\n\|[-\s|]+\n\|.*\n(?:\|.*\n)*?\|\s*\*\*TOTAL\*\*",
                        text, re.MULTILINE)
        if "Type-Only" in text[:m.start()] and len(cells) >= 5:
            out["type_only"] = int(cells[3])
            out["na"] = int(cells[4])

    # Prose total: "Conformance: 118/119" or "Overall ... Conformance: 221/230"
    pm = re.search(r"Conformance:\s*(\d+)\s*/\s*(\d+)", text)
    if pm:
        out["prose_implemented"] = int(pm.group(1))
        out["total"] = int(pm.group(2))

    return out or None


def parse_nodejs_summary(path: Path) -> dict | None:
    """nodejs-features.md has a summary table with per-module Implemented/Total."""
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    # Look for the "Overall Node.js API Conformance" table — rows of:
    # | Buffer | 68 | 68 | 100% |
    m = re.search(
        r"Overall Node\.js API Conformance.*?\| \*\*Total\*\* \| \*\*(\d+)\*\* \| \*\*(\d+)\*\*",
        text, re.DOTALL)
    if not m:
        return None
    return {"implemented": int(m.group(1)), "total": int(m.group(2))}


def build_snapshot(label: str | None = None) -> dict:
    snap: dict = {
        "timestamp": datetime.datetime.now(datetime.timezone.utc)
                     .strftime("%Y-%m-%dT%H:%M:%SZ"),
        "git": git_info(),
    }
    if label:
        snap["label"] = label

    if (v := count_test262()) is not None:
        snap["test262"] = v
    if (v := count_baseline(REPO_ROOT / "tests" / "golden_ir" / ".golden_ir_baseline.json")) is not None:
        snap["golden_ir"] = v
    if (v := count_baseline(REPO_ROOT / "tests" / "node" / ".node_test_baseline.json")) is not None:
        snap["node"] = v
    if (v := count_refdiff()) is not None:
        snap["refdiff"] = v
    if (v := parse_conformance_md(REPO_ROOT / "docs" / "conformance" / "typescript-features.md")) is not None:
        snap["typescript_features"] = v
    if (v := parse_conformance_md(REPO_ROOT / "docs" / "conformance" / "ecmascript-features.md")) is not None:
        snap["ecmascript_features"] = v
    if (v := parse_nodejs_summary(REPO_ROOT / "docs" / "conformance" / "nodejs-features.md")) is not None:
        snap["nodejs_apis"] = v

    return snap


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dry-run", action="store_true",
                    help="Print snapshot to stdout without appending")
    ap.add_argument("--label", help="Optional label/tag for this row")
    ap.add_argument("--no-refdiff", action="store_true",
                    help="Skip refdiff (which actually re-runs the corpus)")
    args = ap.parse_args()

    if args.no_refdiff:
        # monkey-patch count_refdiff to return None
        global count_refdiff
        count_refdiff = lambda: None  # type: ignore

    snap = build_snapshot(label=args.label)
    line = json.dumps(snap, separators=(",", ":"), ensure_ascii=False)

    if args.dry_run:
        print(line)
        return 0

    DASHBOARD_DIR.mkdir(parents=True, exist_ok=True)
    with SNAPSHOTS_PATH.open("a", encoding="utf-8") as f:
        f.write(line + "\n")

    short = snap.get("git", {}).get("short", "?")
    test262 = snap.get("test262", {}).get("pass", "?")
    refdiff = snap.get("refdiff", {}).get("match", "?")
    print(f"snapshot appended: commit={short} test262.pass={test262} refdiff.match={refdiff}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
