# Conformance dashboard

A static HTML dashboard that tracks ts-aot's conformance metrics over time.
Reads `snapshots.jsonl` and renders charts with Chart.js (loaded from CDN).
Open `index.html` in any browser — no server required.

## Captured metrics

Each row in `snapshots.jsonl` records:

| Field | Source |
|---|---|
| `test262.{pass,fail,skip,timeout,compile_error}` | `tests/test262/.test262_results.jsonl` |
| `golden_ir.{pass,fail}` | `tests/golden_ir/.golden_ir_baseline.json` |
| `node.{pass,fail}` | `tests/node/.node_test_baseline.json` |
| `refdiff.{match,diff,crash}` | freshly re-runs `tests/refdiff/runner.py` |
| `typescript_features.{implemented,...}` | parses `docs/conformance/typescript-features.md` TOTAL row |
| `ecmascript_features.{implemented,...}` | parses `docs/conformance/ecmascript-features.md` TOTAL row |
| `nodejs_apis.{implemented,total}` | parses `docs/conformance/nodejs-features.md` summary table |
| `git.{commit,short,branch,subject}` | git rev-parse + log |
| `timestamp` | UTC ISO-8601 |

If a source is missing or unreadable, that field is **omitted** rather than
zero-filled — distinguishing "no data" from "zero".

## Capturing a snapshot

```bash
# Capture now (also re-runs refdiff, ~30 seconds):
python tools/conformance_snapshot.py

# Skip refdiff (uses cached test262/golden_ir/node baselines only):
python tools/conformance_snapshot.py --no-refdiff

# Dry-run to stdout without appending:
python tools/conformance_snapshot.py --dry-run

# Backfill with a label:
python tools/conformance_snapshot.py --label "Phase A baseline"
```

A snapshot is automatically captured at the end of every full
`python tests/run_all.py` run.

## Viewing the dashboard

Open `docs/conformance-dashboard/index.html` in a browser. Chrome/Firefox/Edge
all work. The page loads `snapshots.jsonl` via fetch — most browsers allow this
for local files; if not, run a tiny static server:

```bash
python -m http.server 8000 -d docs/conformance-dashboard
# then open http://localhost:8000/
```

## Notes

- **JSONL, not JSON.** Append-only. New schema fields don't break old rows.
- **Backfill is fine.** Just append rows manually for historical commits;
  sort by timestamp client-side.
- **Refdiff is the only metric that's actively re-measured.** Everything else
  is read from the most-recent test run's baselines, so capture timing
  matters: take snapshots right after `run_all.py`.
