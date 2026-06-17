#!/usr/bin/env bash
# Resilient / resumable test262 full sweep.
#
# The sweep process has been dying mid-run (environment). The runner already
# supports resume: --fast enables --resume, which reads .test262_results.jsonl
# and skips already-completed tests, so re-running WITHOUT --fresh continues
# from where a crash left off (valid as long as the build is unchanged).
#
# This wrapper retries until the runner prints its completion summary, so a
# crashed sweep finishes by itself instead of losing all progress.
#
# Usage:
#   scripts/sweep_resilient.sh            # resume an existing partial (or start)
#   scripts/sweep_resilient.sh --fresh    # clean start for a NEW build, then resume on crash
#
# Run a `cmake --build` BEFORE calling with --fresh; on a build change you MUST
# --fresh once (the old partial reflects the old binary).
set -u
cd "$(dirname "$0")/.."
fresh="${1:-}"
log=tmp/sweep_resilient.log
mkdir -p tmp
for attempt in $(seq 1 10); do
  mode=$([ -n "$fresh" ] && echo fresh || echo resume)
  echo "[sweep_resilient] attempt $attempt ($mode)..."
  TS262_SHARED_RUNTIME=1 python tests/test262/run_test262.py --fast $fresh > "$log" 2>&1
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
