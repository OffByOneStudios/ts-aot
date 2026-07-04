---
name: cycle-checkpoints
description: Terse, state-carrying reporting between autonomous work cycles - one-line status per cycle, one-paragraph memory checkpoints, self-contained loop prompts. Use during multi-cycle autonomous runs (/loop grinds, overnight worksets) or any long session with repeated fix-gate-merge iterations. Trigger terms - "checkpoint", "cycle report", "autonomous run", "loop status".
allowed-tools: Bash, Write, Read
---

# Cycle Checkpoints: Terse Reports, Durable State

During a multi-cycle autonomous run, tokens spent narrating are tokens not
spent fixing — and context gets compacted, so anything not written to a
durable surface is lost. The discipline: **reports are one line, state lives
in three durable places, and each carries exactly what its reader needs.**

## The Three Surfaces

1. **The user-visible cycle report** (end of each cycle) — ONE short block:
   ```
   Cycle 5 complete — merge 114 (+8), baseline 38,333 (87.33%).
   Overnight total: +85. Next: annexB cherry-pick.
   ```
   Lead with the number. Name the merge, the delta, the new topline, the
   running total, and what's next. Nothing else — no method recap, no file
   lists (those live in the commit message).

2. **The memory checkpoint** (appended to the stage log file after every
   merge) — ONE paragraph, written for a future session with zero context:
   - the merge number, delta, and new topline in the header line;
   - the ROOT CAUSE in one clause (not the symptom — "the keep-original-name
     list emitted the bare `parseFloat` symbol which mis-binds", not
     "parseFloat was broken");
   - any NEW gotcha as an imperative rule ("guards must cover BOTH dispatch
     layers — fast-path entries AND *_native wrappers");
   - what was BANKED (deferred with its diagnosis) so the next session
     resumes from the diagnosis, not from scratch;
   - the zero-gain counter and the next queue item.

3. **The loop prompt** (passed to ScheduleWakeup each cycle) — fully
   self-contained: current merge/baseline numbers, the ordered queue with
   per-item diagnostic hints (file:line, probe names), the cycle discipline,
   and the accumulated gotcha list. Update the state numbers every cycle;
   append new gotchas as they're learned. After a context compaction this
   prompt IS the session — it must be executable by a reader with nothing
   else.

## Rules of Terseness

- Between tool calls, status notes are one sentence and only when direction
  changes or something load-bearing appears ("Found it — the getter never
  runs; defineProperty leaves the stale data entry").
- Never restate what the previous surface already carries. The cycle report
  doesn't repeat the commit message; the memory checkpoint doesn't repeat
  the loop prompt's discipline section.
- Numbers over adjectives: "+42 / 0 lost" not "a great improvement".
- Detail budget goes to the COMMIT MESSAGE — that's the durable, greppable
  record. It gets the mechanism, the probe names, the family counts, and
  the known-limits paragraph. If the commit message is thorough, everything
  else can afford to be one line.

## Why This Works

- The morning summary assembles itself: the per-cycle log is just the
  checkpoint headers concatenated.
- Compaction-proof: any turn can be cut off and the next fire reconstructs
  from loop prompt + memory + git log.
- The user scanning a phone screen mid-drive gets the delta and the
  direction in the first line of every report.
