---
name: differential-probes
description: Find root causes with minimal probe programs that isolate one semantic each, and differential pairs that differ in exactly one variable - pin the behavior BEFORE reading source. Use when a test fails for unclear reasons, a fix "should work" but doesn't, or a subsystem misbehaves only in some configurations. Trigger terms - "probe", "reproduce", "isolate", "why does this fail".
allowed-tools: Bash, Write, Read, Grep
---

# Differential Probes

Reading source to find a bug is O(codebase). Probing behavior is O(1) per
hypothesis. Pin the behavior in behavior-space first; only descend into
code-space once the probe matrix has cornered the mechanism.

## Probe Design Rules

- **One semantic per line.** Each console.log answers exactly one question
  and is labeled (`A:`, `B:`, ...) so output diffs are scannable across
  rebuilds:
  ```js
  console.log("A:", vals.join(","));   // fixed-length view iterates
  console.log("B:", c);                 // empty view iterates zero times
  console.log("C:", f[0], f[3]);        // fill writes through
  ```
- **Keep probes in tmp/ and never delete them.** They become an instant
  regression matrix: after every rebuild, re-run the whole set
  (`./cc2.exe && ./lb1.exe && ./ns1.exe`) to prove earlier fixes survived.
  A probe suite of 5 files re-verifies 5 merges in seconds.
- **Expected values in comments** so a future reader (or you, post-
  compaction) can judge output without re-deriving the spec.

## The Differential Pair

When something fails, construct the MINIMAL pair that differs in one
variable. The delta localizes the fault better than any stack trace:

| Pair | One variable | What it revealed |
|---|---|---|
| `{}` vs `{"a":1}` receiver | literal arity | empty literals lower FLAT, non-empty lower TsMap — the flat inline-slot read shadowed accessors |
| `parseInt("42")` vs `parseFloat("42.5")` | the symbol | same compiler path; only parseFloat's bare symbol mis-binds |
| `x.f` direct vs `Object.getPrototypeOf(g).prototype` | acquisition path | intrinsics reachable via instance chain but not the function side |
| `ed1.js` (self-import) vs `t1.js→t2.js` (cross-module) | import topology | export store missing only for the ENTRY module |
| plain `ArrayBuffer` vs resizable | buffer kind | failure was buffer-independent → arg marshaling, not resize logic |

If both sides of the pair fail, widen; if both pass, your reproduction is
wrong — go re-read the actual failing test (it acquires the feature some
other way).

## The Escalation Ladder

Climb only as far as needed; each rung is cheaper than the next:

1. **Does the artifact exist?** (`typeof ns["__getter_x"]`) — presence vs
   dispatch.
2. **Invoke it manually.** (`g()` returned 2 while `ns.x` returned 1 → the
   cell was shared; only the READ PATH was wrong.)
3. **Environment traces.** Check for existing `TS_*_TRACE` env hooks before
   adding any (`TS_ACC_TRACE`, `TS_EH_TRACE`). Silence from a trace is
   itself data — it means that code path never ran.
4. **Dump the IR** (`--dump-ir -O0`) and grep callee names / constants. The
   IR shows what the compiler ACTUALLY emitted (`ts_value_make_double`
   boxed into an `int64` param; a call to bare `@parseFloat(...)`) — this
   settles compiler-vs-runtime attribution instantly.
5. **Temporary instrumentation** — an env-gated fprintf at the suspect
   decision point (`if (getenv("TS_MONO_DBG"))`). Remove before merge.
6. **Only now read the source path** — and you'll know exactly which
   function, with the failing input in hand.

## Attribution Shortcuts

- Output identical after a rebuild → suspect a STALE artifact (old exe, un-
  deployed DLL, wrong cwd) before suspecting the fix. Recompile the probe.
- Wrong value with no crash → value-flow bug (boxing, snapshot vs live,
  shadowing). Crash → pointer-domain bug (tagged value dereferenced, magic
  misread) — validate nanboxes before magic reads.
- A "fixed" probe but unmoved tests → the tests acquire the feature through
  a different path; go read one test verbatim (see measure-first-drill).
