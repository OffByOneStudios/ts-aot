#!/usr/bin/env bash
# Resilient / resumable test262 full sweep — the canonical background-safe gate.
#
# Reliability (why the sweep used to drop the Claude session mid-run): the
# runner now spawns every child BELOW-normal priority (TS262_LOW_PRIORITY,
# default on) so the sweep yields CPU to interactive work, and this wrapper
# caps concurrency below full saturation (TS262_GATE_JOBS, default 16 vs the
# --fast default 24) to leave CPU + RAM headroom. Together they stop the sweep
# from starving the machine and killing the session.
#
# Resume: --fast enables --resume, which reads .test262_results.jsonl and skips
# completed tests, so re-running WITHOUT --fresh continues from where a run
# stopped (valid as long as the build is unchanged). A long per-window budget
# (TS262_GATE_BUDGET, default 90 min) lets a full sweep finish in one window;
# this wrapper still retries-to-resume as a backstop.
#
# Usage:
#   scripts/sweep_resilient.sh            # resume an existing partial (or start)
#   scripts/sweep_resilient.sh --fresh    # clean start for a NEW build, then resume
#   TS262_GATE_JOBS=24 scripts/sweep_resilient.sh   # full speed (dedicated host)
#
# Run a `cmake --build` BEFORE calling with --fresh; on a build change you MUST
# --fresh once (the old partial reflects the old binary).
set -u
cd "$(dirname "$0")/.."
fresh="${1:-}"
jobs="${TS262_GATE_JOBS:-16}"
budget="${TS262_GATE_BUDGET:-90}"
log=tmp/sweep_resilient.log
mkdir -p tmp
for attempt in $(seq 1 10); do
  mode=$([ -n "$fresh" ] && echo fresh || echo resume)
  echo "[sweep_resilient] attempt $attempt ($mode, jobs=$jobs, low-priority)..."
  TS262_SHARED_RUNTIME=1 python tests/test262/run_test262.py --fast \
    --jobs "$jobs" --time-budget-min "$budget" $fresh > "$log" 2>&1
  fresh=""  # only attempt 1 may be fresh; every retry resumes
  if grep -qE "Pass Rate:" "$log"; then
    echo "[sweep_resilient] COMPLETE on attempt $attempt"
    grep -E "Total Run:|Pass Rate:|Passed:" "$log" | tail -4
    exit 0
  fi
  last=$(grep -oE '\[[0-9]+/[0-9]+\]' "$log" | tail -1)
  echo "[sweep_resilient] incomplete (died at ${last:-?}); resuming..."
done
echo "[sweep_resilient] gave up after 10 attempts; see $log"
exit 1
