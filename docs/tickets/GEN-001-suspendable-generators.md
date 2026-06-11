# GEN-001: Suspendable Async Generators (rearchitecture of the eager-body model)

Status: PLANNED
Baseline at planning time: test262 26,220 pass / 14,021 fail / 5 ce / 46 timeout.
Gates for every stage: golden-ir 267/279, node 295/297, 2k regression sample
(tests/test262/regression_sample.txt, zero-flip floor), full-sweep arbiter
(keep only if net >= -6).

## 0. Verified code anchors (re-verify before each stage; line numbers drift)

Compiler — src/compiler/hir/HIRToLLVM.cpp (~11,039 lines):
- `lowerFunction` async-gen EAGER branch: `if (fn->isAsync && fn->isGenerator)`
  at :982 — calls `ts_async_generator_create()`, sets `generatorObject_`, then
  falls through to normal (non-state-machine) block lowering.
- `lowerFunction` SYNC state-machine branch: `else if (fn->isGenerator)` at
  :1009. Pieces:
  - yield/alloca counting :1017-1031
  - cross-yield SSA liveness pre-pass :1033-1145 (`crossYieldSpillIds_`,
    `crossYieldSlotOf_`, within-block-yield-crossing + cross-block-use union)
  - impl function creation `fn->mangledName + "$impl"`, signature
    `void(AsyncContext*)`, `setGC("ts-aot-gc")` :1147-1163
  - wrapper: `ts_async_context_create`, `ts_async_context_set_resume_fn`,
    `ts_get_call_this`/`ts_async_context_set_this`, param buffer via
    `ts_alloc((params+allocas+spills)*8)` + `ts_async_context_set_data`,
    `ts_generator_create(ctx)`, immediate `ret` :1165-1268
  - impl entry: `ts_async_context_get_state`, `ts_async_context_get_data`,
    param reloads, pre-created local-slot GEPs (`generatorLocalSlots_`) and
    spill-slot GEPs (`crossYieldSlotGEPs_`), state switch into
    `yieldResumeBlocks_` / `generatorDoneBlock_` :1276-1394
  - early `return` at :1422 (skips the shared tail of lowerFunction)
- Async-fn promise prologue setjmp barrier (async.reject) :1439-1501.
- Async-gen EAGER setjmp barrier (agen.reject / agen.record / agen.rethrow,
  `ts_agen_should_reject`, `ts_async_generator_set_exception`,
  `ts_async_generator_abort`) :1502-1598.
- `lowerAlloca` generator heap-slot path :3811-3826.
- `lowerReturn`: agen-eager branch :9736-9769 (calls
  `ts_async_generator_return` + pops prologue handler + returns gen object);
  sync-state-machine branch :9772-9798 (`ts_generator_return_via_ctx`,
  `ret void`). `lowerReturnVoid` mirrors at :9920-9965.
- `lowerAwait` :10323-10362 — calls BLOCKING `ts_promise_await`.
- `lowerYield` :10386 — agen branch :10414-10419 (`ts_async_generator_yield`,
  yield expr value = its return, i.e. undefined); sync state-machine branch
  :10420-10448 (`ts_async_context_yield` + `ts_async_context_set_state` +
  `ret void` + insert-point relocation into `yieldResumeBlocks_[n]` +
  `ts_async_context_get_resumed_value` as the yield expression's value).
- `lowerYieldStar` :10458 — sync state-machine inline delegation loop
  :10483-10562 (delegate iterator persisted via
  `ts_async_context_set_delegate_iterator`, ONE state per yield* covering all
  its iterations); agen branch :10563-10569 (`ts_async_generator_yield_star`
  eager drain).
- `lowerSetupTry` :10202-10258 — per-try `ts_push_exception_handler` +
  `_setjmp(jmpBuf, frameaddress)` (Win64 2-arg form), `ReturnsTwice`,
  `NoInline` on containing fn. `lowerThrow` :10260 (uniform `ts_throw`).
- Return type forced to ptr for async/gen fns :820, :907.
- State fields: src/compiler/hir/HIRToLLVM.h :225-255 (`currentYieldState_`,
  `yieldResumeBlocks_`, `generatorImplFunc_`, `generatorDataBuf_`,
  `generatorLocalSlots_`, `crossYield*`).

Compiler — src/compiler/hir/ASTToHIR.cpp:
- `visitYieldExpression` :9785-9812 — emits HIR Yield/YieldStar; NO Await is
  emitted for the yield operand in async gens (spec requires the operand of
  `yield` in an async generator to be awaited).
- `ts_async_generator_body_started` marker emitted right after the parameter
  prologue at FOUR function-lowering sites: :2926, :9969, :10321, :10708.

Compiler — src/compiler/hir/handlers/GeneratorHandler.cpp:
- Claims `next`/`return`/`throw` for className "Generator", "" and
  "AsyncGenerator" (:42-58). Emits `AsyncGenerator_next` /
  `AsyncGenerator_return` / `AsyncGenerator_throw` for typed receivers
  (:150, :183, :214). NOTE: `AsyncGenerator_return` and `AsyncGenerator_throw`
  HAVE NO RUNTIME DEFINITION (grep over src/ finds only these two compiler
  references). Untyped receivers route to `Generator_next` /
  `Generator_return` / `Generator_throw` (TsPromise.cpp :189/:407/:449);
  `Generator_next` detects AGEN via magic and forwards (:198-200), but
  `Generator_return`/`Generator_throw` do NOT — an untyped
  `agen.return()/throw()` falls into the "forward to own property" path and
  synthesizes a plain (non-promise) result.

Runtime — src/runtime/include/TsPromise.h:
- `AsyncContext` :18-38 (state, yielded, yieldedValue, promise,
  pendingNextPromise, generator, resumeFn, data, resumedValue,
  delegateIterator, syncGenerator, thisValue).
- `TsGenerator` :40-47, `TsAsyncGenerator` :49-67 (eager fields:
  pendingYields/yieldCursor/returnValue/pendingException/hasException/
  bodyStarted).

Runtime — src/runtime/src/TsPromise.cpp (2,157 lines):
- `TsGenerator::next` :74-100 — clears `yielded`, sets `resumedValue`,
  restores captured `this`, calls `ctx->resumeFn(ctx)`, builds
  `{value, done}` from `ctx->yielded`/`yieldedValue`.
- `TsAsyncGenerator::next` :116-160 — eager queue drain :122-127, pending
  exception :131-137, done :138-145, DORMANT resumeFn path :147-155
  ("keep it for a future suspendable implementation"), fallthrough done.
- `ts_async_yield` :164-172 — sets yielded + resolves `pendingNextPromise`
  with `{value, done:false}` (existing resume-completion hook).
- `ts_async_generator_resolve` :743-751 — resolves `pendingNextPromise`
  with arbitrary done flag (existing).
- Eager machinery: `g_asyncgen_stack` + GC scanner/fixup :495-510, create
  :512, yield :527, return :553, set_exception :571, body_started :585,
  abort :594, `ts_agen_should_reject` :271, `ts_async_generator_yield_star`
  :308-399 (eager drain, 1,000,000-step hang guard).
- `ts_promise_await` :1321-1359 — BLOCKING: pumps `ts_run_microtasks()` +
  `uv_run(loop, UV_RUN_ONCE)` until settled; `ts_throw`s rejections.
- `ts_async_resume` :1080, `ts_async_await`/`then_async` :1361-1369 —
  callback-driven resumption machinery (used by nothing today for gens).
- AsyncContext accessors :2084-2155.
- Win64 `((_JUMP_BUFFER*)env)->Frame = 0` examples :1426, :1474, :1766, :1845.

## 1. Assessment: extend the sync state machine vs fresh coroutine lowering

Decision: EXTEND THE SYNC STATE MACHINE. Do not write a new coroutine-style
lowering (LLVM coro intrinsics or CPS).

Why:
- The hard compiler problems — cross-suspension SSA liveness spilling into a
  GC-visible buffer, heap-allocated locals (`lowerAlloca` :3811), resume-block
  insert-point relocation, the `this` capture, statepoint GC attribution on
  the impl function — are already solved and battle-tested by the sync path
  across 26k passing tests. LLVM coro intrinsics are a non-starter with the
  custom moving GC: coro frames are opaque to our statepoint stack maps and
  the GC cannot trace/forward pointers inside them.
- The runtime already has the suspension half built and dormant:
  `TsAsyncGenerator::next` :149-155 drives `ctx->resumeFn`, and
  `ts_async_yield` :164 / `ts_async_generator_resolve` :743 already resolve
  `pendingNextPromise`. The compiler just never emits a resumeFn for agens.
- Crucially, `lowerAwait` is a blocking pump (`ts_promise_await`), and async
  FUNCTIONS already work this way. Therefore awaits inside an async-gen impl
  function are NOT suspension states — the impl frame stays live across the
  pump, registers stay valid, statepoints cover the frame. Only Yield /
  YieldStar (and the initial param-prologue boundary) are suspension points.
  This collapses the delta to "sync state machine + awaited yield operand +
  promise-wrapped results + request queue".

Concrete deltas the async case needs on top of the sync machine:

D1. Promise-wrapped protocol. `next(v)` must return a promise. The dormant
    path in `TsAsyncGenerator::next` already does this: create p, set
    `ctx->pendingNextPromise = p`, set `resumedValue`, call `resumeFn`. The
    impl resolves p at the next suspension via `ts_async_yield`-style helper
    or at completion via `ts_async_generator_resolve(ctx, v, true)`.
    Because awaits are blocking, `resumeFn` does not return until the body
    reaches a yield/return/throw, so p is resolved (not pending) when next()
    returns — observable ordering risk, see Q1.

D2. Awaited yield operand (spec AsyncGeneratorYield step 1: Await(value)).
    In `lowerYield`'s new suspendable-agen branch, before storing the yielded
    value: call a new runtime helper `ts_agen_await_operand(value)` that
    awaits promise-shaped values via the `ts_promise_await` pump. A rejected
    operand `ts_throw`s INSIDE the impl frame at the yield site — a user
    try/catch around the yield (whose `lowerSetupTry` handler was pushed
    earlier in the SAME impl invocation) catches it. This delivers item (c)
    of the unlock list (yield Promise.reject(x) catchable).

D3. Resume modes. `gen.throw(e)` / `gen.return(v)` must resume the impl at
    the current suspension point with a throw/return completion. Add
    `int resumeMode` + `TsValue* resumeArg` to AsyncContext (or reuse
    `resumedValue` + a mode enum). Each `yield_resume_N` block (and the
    yield* resume block) prepends a mode dispatch:
      - NEXT: yield expression evaluates to `ts_async_context_get_resumed_value`.
      - THROW: `ts_throw(resumeArg)` — caught by enclosing user try in the
        CURRENT impl invocation if the handler is live (see Q2 — handlers
        pushed in a PREVIOUS invocation are stale; this is the hardest open
        problem and is staged separately).
      - RETURN: branch to a per-function "forced return" path that runs
        `ts_async_generator_resolve(ctx, arg, true)` (finally-block execution
        is a later refinement, same as sync gens today).
    This replaces the missing `AsyncGenerator_return`/`AsyncGenerator_throw`
    runtime symbols (currently undefined — fix regardless).

D4. AsyncGeneratorEnqueue / re-entrancy queue. Because awaits inside the impl
    pump the uv loop and run microtasks, a microtask can call `gen.next()`
    re-entrantly while the impl frame is live-but-awaiting. Add to
    TsAsyncGenerator: `bool executing` and a GC-visible request queue
    (TsArray of [promise, value, mode] triples — TsArray fields are scanned;
    do NOT use std::vector per .claude/rules/runtime-safety.md). next/throw/
    return: create p; if executing or queue non-empty, enqueue and return p;
    else drive. After each resumeFn return, drain queued requests in order.

D5. Param-prologue split (preserves the dstr/dflt-params family that the
    eager model satisfies). The HIR call to `ts_async_generator_body_started`
    (already emitted at the right spot, ASTToHIR :2926 etc.) becomes
    SUSPENSION POINT 0 in suspendable mode: lower it as
    set_state(1) + `ret void`. The wrapper, after building ctx/gen, invokes
    `resumeFn(ctx)` ONCE synchronously at gen() time: state 0 runs the
    parameter prologue and suspends at the marker. A param-binding throw
    happens inside that synchronous invocation and propagates out of gen()
    via the normal handler stack — no `ts_agen_should_reject` machinery
    needed. The body proper starts at state 1 on the first next().
    (Sync gens do NOT do this today — their params run on first next();
    out of scope here, but the mechanism is reusable later.)

D6. Per-invocation exception barrier. The eager model's setjmp barrier lives
    in the wrapper (:1502-1598) and dies with this rearchitecture. Instead,
    the IMPL function pushes a catch-all handler at impl_entry on EVERY
    invocation and pops it on every suspend/return edge (yield, marker,
    done). On catch: `ts_async_generator_resolve_reject(ctx, exc)` → rejects
    the CURRENT request's promise, marks gen done, clears the exception slot,
    `ret void`. For the state-0 (param prologue) invocation the barrier is
    NOT pushed (or rethrows), so param errors stay synchronous. Win64: the
    compiled `_setjmp(jmpBuf, frameaddress)` form at :10222-10238 is the
    pattern to copy; the runtime-side `Frame = 0` zeroing only applies to
    runtime-owned `setjmp(*env)` buffers (TsPromise.cpp :1426) — verify which
    side needs zeroing with the auto-debug skill if 0xc00000ff appears.

D7. Suspendable async yield*. Replace the eager `ts_async_generator_yield_star`
    drain with the sync-style inline delegation loop (:10483-10562) using ONE
    suspension state per yield*, plus:
    - a new runtime helper `ts_agen_get_async_iterator(iterable)` extracted
      from :308-364 (GetIterator(value, async) protocol checks, sync-iterator
      fallback) that returns the iterator or ts_throws the protocol
      TypeErrors — now caught by the impl barrier (D6) instead of the
      agen.reject wrapper pad, preserving the iter-close cluster.
    - per-step `ts_promise_await` of the step result and of `value` when
      promise-shaped (matches the eager drain's await points :374-390).
    - resume-mode forwarding to the inner iterator (`throw`/`return` methods)
      as a later refinement (Stage 7).

Generator object identity/API stays: `TsAsyncGenerator` struct, `next()`
returning TsPromise*, GeneratorHandler dispatch, `Generator_next` magic
sniffing — all untouched. The J-series fields (pendingYields, yieldCursor,
hasException, bodyStarted) remain for the eager fallback and become dead only
at Stage 9.

## 2. Stages

Every stage is one commit, independently gated (golden-ir, node, 2k sample;
full sweep for stages marked SWEEP). Rollback story is per stage.

### Stage 1 — Zero-behavior refactor: extract the sync state-machine builder
- HIRToLLVM.cpp: extract :1009-1423 into private helpers on HIRToLLVM:
  - `collectGeneratorCounts(fn)` (yield/alloca counting),
  - `computeCrossYieldSpills(fn)` (:1033-1145),
  - `emitGeneratorWrapper(fn, llvmFunc, opts)` (:1165-1268),
  - `emitGeneratorImplPrologue(fn, opts)` (:1276-1394),
  parameterized by an options struct `{ bool isAsyncGen; const char*
  createGenFn; ... }` currently only instantiated with sync values.
  `lowerFunction` body becomes a call sequence; behavior byte-identical.
- HIRToLLVM.h: declare helpers; no field changes.
- Test signal: NONE expected. golden-ir must stay 267/279 with identical IR
  (this is the point of the stage). SWEEP optional sanity.
- Risk: accidental IR drift (block naming/order) flips golden tests. Keep
  block names identical ("wrapper_entry", "impl_entry", "yield_resume_N",
  "generator_done").
- Rollback: revert the commit; no runtime or HIR changes involved.

### Stage 2 — Runtime groundwork (additive, dead code until used)
- TsPromise.h / TsPromise.cpp:
  - `AsyncContext`: add `int resumeMode` (0=next,1=throw,2=return). Reuse
    `resumedValue` as the argument carrier.
  - `TsAsyncGenerator`: add `bool executing`, `TsArray* requestQueue`
    (GC-visible; entries are 3-element TsArrays or a small TsMap), and
    `bool suspendable` (set by the new create entry point).
  - New entry points:
    - `TsAsyncGenerator* ts_async_generator_create_suspendable(void)` —
      like :512 but does NOT push `g_asyncgen_stack`, sets `suspendable`.
    - `void ts_agen_suspend_yield(AsyncContext* ctx, TsValue* v)` — sets
      yielded + resolves `pendingNextPromise` with `{v, done:false}`
      (factor of :164-172).
    - `void ts_agen_complete(AsyncContext* ctx, TsValue* v)` /
      `void ts_agen_complete_reject(AsyncContext* ctx, TsValue* exc)` —
      done-side of :743-751 + rejection twin.
    - `TsValue* ts_agen_await_operand(TsValue* v)` — pump-await iff
      promise-shaped (factor of the :374/:389 pattern).
    - `TsValue* ts_agen_get_async_iterator(TsValue* iterable)` — protocol
      extraction from :308-364, ts_throws TypeErrors.
    - `int ts_async_context_get_resume_mode(AsyncContext*)` + setter.
  - `TsAsyncGenerator::next(value)`: route through a new
    `enqueueRequest(mode, value)` that implements D4; when `suspendable`
    and not executing, drive `resumeFn` (the dormant :149-155 path,
    now setting `executing` around the call and draining the queue after).
    Eager-model generators (suspendable==false) keep the existing drain
    path verbatim.
  - Define the MISSING `AsyncGenerator_return` / `AsyncGenerator_throw`
    symbols: for eager gens, mimic current observable behavior (resolve
    `{value, done:true}` promise / reject with exc); for suspendable gens,
    enqueue with mode RETURN/THROW. Also teach `Generator_return` /
    `Generator_throw` (:407/:449) the AGEN-magic forward that
    `Generator_next` :198-200 already has, so UNTYPED receivers reach the
    same code (this is the common path; className is usually empty).
- Test signal: agen `.return()`/`.throw()` promise-shape tests may flip a
  handful of fails->passes even before suspension (they currently get
  non-promise results). SWEEP required (arbiter net >= -6).
- Risk: the AGEN forward in Generator_return/throw changes behavior for
  tests passing by accident today. The 2k sample catches flips early.
- Rollback: all additions are behind `suspendable==false` defaults except
  the Generator_return/throw forward — keep that forward as its own
  mini-commit inside the stage so it can be reverted independently.

### Stage 3 — Feature flag + suspendable lowering skeleton (flag OFF default)
- Flag: `TSAOT_SUSPEND_AGEN=1` read once in HIRToLLVM (pattern:
  `std::getenv("TSAOT_DEBUG_IR")`, CodeGenerator.cpp :657); store as
  `suspendAsyncGen_` member.
- HIRToLLVM.cpp `lowerFunction`: when flag ON and `fn->isAsync &&
  fn->isGenerator`, route into the Stage-1 helpers with
  `opts.isAsyncGen=true`:
  - wrapper: `ts_async_context_create` + set_resume_fn + set_this + data
    buffer (identical to sync) + `ts_async_generator_create_suspendable` +
    bind `ctx->generator` + ONE synchronous `resumeFn(ctx)` call (the
    param-prologue invocation, D5) + `ret gen`.
  - impl: same prologue; state count = yieldCount + 1 (the body_started
    marker consumes state transition 0->1; yields are states 2..N+1).
    Simplest accounting: treat the marker as yield #0 in the counting loop.
  - In suspendable-agen mode, intercept the HIR Call to
    `ts_async_generator_body_started` (match by callee name in lowerCall)
    and lower it as: pop barrier-if-pushed, set_state(1), `ret void`,
    relocate insert point to resume block 1. Do NOT change ASTToHIR.
  - `lowerYield` suspendable-agen branch: `ts_agen_await_operand` (D2) →
    `ts_agen_suspend_yield(ctx, v)` → set_state(n) → pop impl barrier →
    `ret void` → resume block: mode dispatch (D3; THROW/RETURN minimal:
    THROW = ts_throw(arg), RETURN = branch to forced-return block calling
    `ts_agen_complete`).
  - `lowerReturn`/`lowerReturnVoid`: new branch `isAsyncFunction_ &&
    isGeneratorFunction_ && asyncContext_` → box → `ts_agen_complete(ctx,
    v)` → pop barrier → `ret void` (NOT `ret generatorObject_` — the impl
    is void; the wrapper already returned the gen).
  - Impl-entry barrier per D6 (skip rejection for state 0; in state 0 the
    barrier is not pushed at all so param throws unwind to gen()'s caller).
  - `lowerYieldStar` suspendable-agen branch: TEMPORARY fallback — keep
    calling eager `ts_async_generator_yield_star`? NO: that depends on
    `g_asyncgen_stack`. Instead Stage 3 lowers yield* via
    `ts_agen_get_async_iterator` + the sync-style delegation loop
    (:10483-10562) with `ts_promise_await` on step results — i.e. D7 core,
    minus throw/return forwarding.
- Validation in this stage: hand-written smoke tests (compile+run a dozen
  async-gen samples with the flag on: send-back values, yield rejected
  promise inside try/catch, infinite generator + early break, for await of
  agen, return value surfacing) — added under the project's smoke/golden
  dirs as flag-conditional, NOT into default gates.
- Test signal (flag OFF): zero change everywhere; SWEEP not required but
  run the 2k sample to prove the flag-off path is untouched.
- Risk: insert-point bookkeeping with the extra marker state; impl barrier
  push/pop balance across all suspend edges (a missed pop = stale jmp_buf =
  0xc00000ff on Win64).
- Rollback: flag stays default-off; revert is a no-op for users.

### Stage 4 — Experimental flag-on sweep + ordering probes
- No production code changes intended; this stage is measurement plus small
  fixes it provokes.
- Run the FULL test262 sweep with TSAOT_SUSPEND_AGEN=1 (not the arbiter —
  a parallel data point). Expected movement (from the unlock analysis):
  - "log.length" lazy-ordering cluster (~72) — partial/most
  - "iter-close" cluster (~72) — partial (full needs Stage 7)
  - next(v) send-back and yield-rejection clusters
  - a slice of the 507 "$DONE not called" family
  Expected NEW failures to triage: ordering-sensitive tests where blocking
  next() resolves the promise before returning (Q1), try/yield/throw()
  staleness (Q2), GC stress under pump (Q3).
- Test signal: the flag-on sweep report drives Stage 5-7 scoping. Flag-off
  arbiter must be unchanged.
- Risk: none to mainline (flag off).
- Rollback: n/a (measurement).

### Stage 5 — Microtask-deferred resumption (only if Q1 probes demand it)
- If ordering clusters fail because next() resolves its promise
  synchronously before returning: change the drive in
  `TsAsyncGenerator::next` to defer `resumeFn` via the microtask queue
  (EventLoop.cpp already roots its queue) instead of calling inline, OR
  resolve `pendingNextPromise` through a microtask hop. Decide from probe
  data; prefer the smallest hop that fixes observed orderings.
- Files: TsPromise.cpp (next/enqueue/drain), EventLoop.cpp (enqueue API).
- Test signal: log.length / interleave clusters under flag-on sweep.
- Risk: deferring resumption re-opens "test exits without pumping" — the
  top-level must pump until the queue drains (check how `$DONE`-style
  programs terminate; node gate covers some of this).
- Rollback: revert to inline drive (Stage 3 behavior); flag still off.

### Stage 6 — gen.throw()/gen.return() resume delivery + try interaction
- Minimal correct semantics WITHOUT cross-suspension handler re-arming:
  - THROW at a suspension point: handlers pushed in a previous impl
    invocation are stale (dead frames). Stage 6 implements: resume-mode
    dispatch raises the exception via `ts_throw` AFTER re-arming enclosing
    user handlers. Mechanism: compiler records, per yield state, the stack
    of enclosing HIR SetupTry blocks; the resume block re-executes their
    `lowerSetupTry` sequence (push+setjmp targeting the SAME catch blocks)
    before the mode dispatch. This is experiment E2 below — if it proves
    too invasive, fall back to "uncaught" semantics (complete generator,
    reject promise) which is what sync gens do today (Generator_throw :449)
    and gate the cluster delta.
  - RETURN: branch to forced-return; running `finally` blocks requires the
    same re-arming infrastructure — same decision point.
- Files: HIRToLLVM.cpp (resume-block emission, per-state try-scope table),
  TsPromise.cpp (`AsyncGenerator_throw/return` enqueue modes from Stage 2).
- Test signal: async-generator-prototype throw/return clusters; iter-close
  remainder.
- Risk: highest-complexity stage; keep it last before flip and severable.
- Rollback: ship flip (Stage 8) without Stage 6 if its flag-on delta is
  net-negative; the mode dispatch falls back to complete-and-settle.

### Stage 7 — yield* completion forwarding
- Forward THROW/RETURN resume modes to the delegate iterator's
  `throw`/`return` methods per spec 27.6.3.7 steps; close the iterator on
  abrupt completion (IteratorClose / AsyncIteratorClose with awaited
  result). Runtime helpers preferred over inline IR (one
  `ts_agen_delegate_resume(ctx, mode, arg)` that performs the protocol and
  either returns a step result or ts_throws).
- Files: TsPromise.cpp, HIRToLLVM.cpp `lowerYieldStar` agen branch.
- Test signal: remaining iter-close cluster; yield-star throw/return tests.
- Risk: protocol corner cases (return missing on inner iterator, etc.) —
  copy assertions from the eager drain's protocol code which already
  passes these checks.
- Rollback: severable; yield* keeps Stage-3 next-only delegation.

### Stage 8 — FLAG FLIP (default ON) — its own commit
- Change the default of `suspendAsyncGen_` to true; `TSAOT_SUSPEND_AGEN=0`
  selects the eager fallback.
- Gates: golden-ir (expect agen golden tests to need re-blessing — re-bless
  in this commit, count must stay >= 267/279), node 295/297, 2k sample
  zero-flip floor (any flip must be individually justified as a
  spec-correctness improvement and the sample re-baselined in the same
  commit), FULL SWEEP ARBITER net >= -6 vs 26,220.
- Rollback: single-line default revert; the eager path is fully intact
  (g_asyncgen_stack, ts_agen_should_reject, agen.reject pad all still
  emitted under flag-off).

### Stage 9 — Eager-model removal (only after >= 1 full conformance loop on
  the flipped default)
- Delete: HIRToLLVM eager agen branch (:982-994 remnant + :1502-1598
  barrier), runtime `g_asyncgen_stack` + scanner/fixup, `ts_agen_should_
  reject`, `ts_async_generator_set_exception/abort/body_started` (marker
  emission in ASTToHIR stays — it is the state-0 split point), eager fields
  in TsAsyncGenerator (pendingYields/yieldCursor/hasException/bodyStarted).
- Test signal: none expected; SWEEP arbiter.
- Rollback: revert; this is why it is a separate, delayed commit.

## 3. Open design questions — probe with small experiments BEFORE Stage 3

Q1. Synchronous-drive ordering vs spec microtask ordering.
    With blocking awaits, `next()` returns an ALREADY-RESOLVED promise and
    all body side effects up to the next yield happen before `next()`
    returns. Spec runs the body between microtask ticks. Experiment E1:
    take 5-10 tests from the log.length cluster, hand-trace expected vs
    blocking-drive orderings. If most need deferral, Stage 5 is mandatory
    before the flip; if only `assert(p instanceof Promise && pending)`
    style checks fail, consider resolving `pendingNextPromise` via a
    microtask while still driving inline.

Q2. setjmp handler staleness across suspension (affects D3/Stage 6 and a
    latent sync-gen bug). A try-block handler pushed in impl invocation #1
    is a dead frame in invocation #2; a throw delivered after resume
    (gen.throw, or a rejected awaited value AFTER an interleaved suspension)
    longjmps into a returned frame. Experiment E2: sync generator
    `function*(){ try { yield 1; throw new Error("x"); } catch(e){ yield 2 } }`
    — call next(); next(); today. Does the second next() crash, mis-route,
    or work by accident (handler stack popped on suspend?). Read
    ts_push/pop_exception_handler in the runtime (Core.cpp) to see whether
    suspension leaks handler-stack entries — if the impl returns mid-try
    without popping, EVERY sync-gen yield-inside-try leaks a stale handler
    TODAY, and the re-arming design must also fix the pop balance. This
    determines whether Stage 6 re-arming is feasible or the fallback
    semantics ship first.

Q3. GC visibility of suspended state under uv pump. Sync gens never pump
    the event loop between suspensions; suspendable agens will (await
    inside impl, and idle time between next() calls while OTHER promises
    run). Verify: (a) is the `ts_alloc`'d data buffer (ctx->data) scanned
    AND its slots forwarded on moving GC? (b) is AsyncContext itself (a
    TsObject with raw `TsPromise*`/`void*`/`TsValue*` fields :18-38)
    traced — who keeps `pendingNextPromise` and `thisValue` alive and
    un-stale across a minor GC? Experiment E3: flag-on smoke test that
    yields in a loop with `TS_GC_NURSERY=0` and forced collections
    (TS_GC_VERIFY=2) between next() calls.

Q4. Re-entrancy depth. ts_promise_await pumps microtasks which can call
    next() on the SAME generator (enqueue per D4) or on ANOTHER generator
    whose impl then pumps again — nested uv_run(UV_RUN_ONCE) frames.
    Experiment E4: two interleaved async gens awaiting each other's values;
    check for pump re-entrancy hazards in EventLoop.cpp (libuv forbids
    re-entrant uv_run on the same loop — async fns must already hit this;
    find how it survives today before assuming it is safe).

Q5. Where do typed-vs-untyped receivers actually flow? Confirm with a probe
    .ts file + IR dump (TSAOT_DEBUG_IR=1) whether `AsyncGenerator_return/
    throw` (undefined symbols) are ever emitted today — if yes, what
    resolves them, because Stage 2 defining them could CHANGE behavior of
    tests that currently "pass" via the untyped Generator_return path.

Q6. Golden IR churn budget. The Stage-1 refactor must be IR-identical;
    confirm the golden-ir harness diffs full IR or just pass/fail of
    execution, to know how strict block-name preservation must be.

## 4. Test signal map (clusters -> stages)

- next(v) send-back values, yield-expr value: Stage 3/4.
- yield Promise.reject caught by try/catch around yield: Stage 3/4 (D2).
- Infinite inner iterators / partial consumption (eager-drain guard hangs):
  Stage 3/4 (suspension makes them lazy).
- log.length exact-ordering: Stage 4 measurement -> Stage 5.
- iter-close (72+72): Stage 3 (protocol TypeErrors via impl barrier) +
  Stage 7 (close-on-abrupt-completion remainder).
- "$DONE not called" (507, partial): Stages 3-5.
- AsyncGenerator throw()/return() semantics: Stages 2 (shape), 6 (delivery).

## 5. Constraint compliance checklist (every stage)

- [ ] Win64 setjmp: any NEW runtime-side `setjmp(*env)` zeroes
      `((_JUMP_BUFFER*)env)->Frame` (TsPromise.cpp :1426 pattern); compiled
      handlers keep the 2-arg `_setjmp(buf, frameaddress)` form
      (HIRToLLVM :10222-10238); every push has a pop on EVERY suspend/return
      edge.
- [ ] GC: no new std::vector/std::function holding GC pointers without
      scanner + minor fixup (runtime-safety rule); cross-suspension values
      only in the ctx->data buffer or TsArray-backed queues; impl functions
      get setGC("ts-aot-gc").
- [ ] Public API shape preserved: TsAsyncGenerator, next() -> TsPromise*,
      GeneratorHandler method names, Generator_next AGEN sniffing.
- [ ] Eager model untouched while flag is off (Stages 3-7).
