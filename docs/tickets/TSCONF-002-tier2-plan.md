# TSCONF-002: Tier 2 — analyzer tail, crash-zero, runtime axis

**Status:** Planned (2026-07-14)
**Position at planning:** acceptance 1,074/1,272 = 84.4% · crashes 5 + neg_crash 18 ·
runtime axis 86.4% (stale — predates Tier 1; re-run first) · node 301/301 · golden 267/279.
**Predecessor:** Tier 1 complete (see memory `tsconf-tier1-complete-2026-07-14`; 11 commits, 71.3%→84.4%).

Tier 2 is the measured tail: 198 acceptance failures decomposing into 2–8-test
mechanism roots, plus the crash debt and the runtime-semantics axis. This is
the zone where the one-test-per-iteration stop rule was written for — every
phase below states its coherent-group size so the rule can be applied honestly.

## Measured decomposition (fresh recluster, 2026-07-14)

| Mechanism | Count | Root(s) |
|---|---|---|
| parse tail | 64 | async-arrow es5/es2017 parameter forms (~15–20 coherent), ambient enums, EOF/binding-identifier singles |
| unknown-property | 60 | **await-unwrap loses Promise type args (~8–15, one root)**; control-flow property tracking (~7); banked private-name tagged-template (4); const-enum members (3); ~30 scattered |
| assignability (checker precision) | 32 | our step-2 checks vs imprecise inference: narrowing interplay, destructuring tuples, static blocks — 3–4 sub-roots then singles |
| undef-var | 13 | Atomics/Float16Array stdlib singles (trivial); `new`/`of` parser-adjacent |
| base-class not found | 12 | **all mixins** (`class X extends someVariable`) — matrix says N/A |
| constraint edges | 6 | transitive constraint inference |
| crashes | 5 (+18 neg) | deterministic; emitDefaultParameters/RestParameters fn-expr ES6 cluster likely one root |

## Phases (ROI order)

### Phase 0 — Crash zero (the gate metric; 1 session)
5 crashes + 18 neg_crashes, all deterministic. CDB each root
(emitDefaultParametersFunctionExpressionES6 3/3 reproducible; likely shared
root across the ES6 fn-expr emit cluster). Includes the long-known
`emitDeferredStaticInits` 0xbaadf00d uninit-heap flake — a real
memory-safety session. **Exit: crash = 0, neg_crash = 0.** This is worth
doing regardless of any acceptance number.

### Phase 1 — Await-unwrap type flow (~10–15 tests, one mechanism; ½ session)
`(await po).fn()` where `po: Promise<{fn(...): void}>` — the analyzer's
await/Promise unwrap drops the Promise type argument, so member lookup on
the awaited value fails. Fix at the await-expression visitor: extract
`typeArguments[0]` from the Promise-shaped ClassType. Highest-confidence
coherent group in the tail.

### Phase 2 — Mixins policy decision (12 tests; USER DECISION, then 10 min)
Every "Base class not found" failure is the mixin pattern
(`class M extends baseClassVariable`). The matrix records mixins as
**N/A — incompatible with AOT monomorphization**. Recommendation: rescore
as a `mixins-na` counted skip family, consistent with namespaces.
Alternative (real work, ~1–2 sessions): accept-and-compile with dynamic
base-class dispatch, forfeiting monomorphization on those classes.

### Phase 3 — Checker precision batch (32 tests; 1 session, stop-rule active)
The step-2 checker firing where inference is imprecise. Sub-roots worth
taking: (a) assignments after narrowing compare against the *narrowed*
type — track the declared type on the symbol and check against that;
(b) destructuring-declaration tuple/object shapes; (c) class static-block
scoping. Expect the last ~10 to be singles — stop there.

### Phase 4 — Parse tail (64 tests; 1 session, stop-rule active)
One coherent group: async-arrow parameter forms in the es5/es2017 dirs
(~15–20). The rest are 1–4-test grammar edges; take the ≥4 groups
(EOF cluster, ambient enum) and stop.

### Phase 5 — Stdlib and template singles (~8 tests; 1 hour)
Atomics + Float16Array value symbols (mirror the SharedArrayBuffer fix);
the 4 `<no-msg>` template-with-embedded-function-expression failures
(diagnose — silent exit is a reporting bug even if the parse gap stays).

### Parallel track (recommended before Phases 3–4) — Runtime axis to ≥95%
The oracle numbers predate Tier 1 — **re-run `oracle` mode first** (new
passes now feed axis 2). Known real diffs from the last run: enumBasics
**throws at runtime** (miscompile-class — arguably the highest-value bug in
the whole TSCONF program), class static blocks (several), quoted
constructors, private-name computed keys. Add `using` dispose emission
(Symbol.dispose try/finally at block exit) — Tier 1 parsed it, axis 2 will
now honestly flag the missing semantics. Runtime diffs are miscompiles;
acceptance failures are only rejections. If forced to choose, do this
before Phases 3–4.

## Projections and exit
- Phases 0–2 + parallel track: acceptance ~87–88%, **crash 0**, runtime ≥95%. ~3 sessions.
- Phases 3–5: acceptance ~90–92%. ~2 sessions, stop rule will trim the tails.
- Beyond ~92%: analyzer-parity singles (real narrowing, contextual typing
  depth) — economics say stop; the baseline records exactly where and why.

## Standing discipline
Gate battery per merge: tsconf accept (13s) diffed vs GIT-committed
baseline, node, golden-ir, targeted test262 family when runtime/parser
surfaces are touched. TSCONF_FORCE_BASELINE=1 only after triage. Class-heavy
+ controlFlow families are where checker changes over-fire. The
asyncAwaitNestedClasses_es5 sweep flake is known — do not chase.
