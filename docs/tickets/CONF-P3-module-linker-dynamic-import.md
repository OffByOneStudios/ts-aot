# CONF-P3: Compile-Time Module Linker — lazy init, dynamic import(), module-goal execution

**Status:** Phase 1 COMPLETE + Phase 2a COMPLETE (2026-07-03, merges 83-86).
Registry + dynamic import() delivered: literal AND computed specifiers resolve
over the closed world (relative .js string literals are bundle candidates);
misses reject; module evaluation errors are memoized on the record and import()
rejects with the SAME error object (eager init + error capture = behaviorally
equivalent to lazy init for the error-observability tests); specifier ToString
runs user hooks guarded (abrupt -> rejection); all export forms land on the
namespace (default expr/fn/class, `export {a as b}`, named re-exports incl.
self). Per-file parse goal (TS_SCRIPT_GOAL = entry only; imports always module
goal). Runner batch mode now compiles module-goal tests correctly (+251).
dynamic-import family 217 -> ~390/942; overall p36 = 37,722 (86.0%).

**Remaining (follow-on scope):** live bindings through namespaces (blocked in
part on the shared-closure-environment task), namespace exotic object
semantics (non-extensible, @@toStringTag, sorted keys, iee-err link
validation ~45), TLA init ordering, module-goal residue.

**Created:** 2026-07-02
**Scale:** Architectural (multi-session). This is the "module linker decision"
flagged in `docs/conformance/test262-roadmap.md` Stage C.

## Thesis

Treat `import()` as **lazy initialization over a closed-world module registry**
— the code-splitting pattern every bundler uses. `HostLoadImportedModule` is
host-defined, so "only modules bundled at compile time are resolvable" is a
legitimate host policy: a hit lazily evaluates and returns the memoized
namespace; a miss returns a **rejected Promise**. This moves ~1,400 tests
currently written off as structural into implementable territory (practical
ceiling ~95% → ~98%).

## Baseline (sweep tmp_p23, 2026-07-02)

| Family | Pass | Fails | Fail character |
|---|--:|--:|---|
| expressions/dynamic-import | 200/938 | 738 | see breakdown below |
| language/module-code | 1/583 | 582 | 568 = runner "unsupported flag: module" |
| expressions/import.meta | 1/22 | 21 | 17 = module flag |

dynamic-import fail breakdown:
- **405 = separate proposals, OUT OF SCOPE**: source-phase-imports 216,
  import-defer 125, import-attributes 64. Do not count these in estimates.
- **601 (across all 3 families) = "unsupported flag: module"**: the test262
  runner refuses tests whose metadata declares `flags: [module]`. Unlocking
  them needs BOTH a runner change (compile with module goal) and real module
  semantics (bindings, namespaces, strictness).
- **118 = "Dynamic import() is not supported at runtime"**: the actual
  registry/lazy-init gap (current impl resolves only string literals at
  compile time and inlines eagerly).
- **~78 = semantic residue**: rejection-not-throw (30 expect SyntaxError as
  rejection), `.then` on undefined (22), asyncTest harness wiring (26).

**Realistic yield: ~700–800** (118 + 78 + a large share of the 601), on top of
Stage B — do not start before the Stage B volume veins unless prioritized.

## Current state (verify before implementing)

- Static imports: modules statically linked; per-file `__module_init`
  functions run eagerly at startup; imports compile to direct symbol refs.
- Dynamic `import("./literal")`: resolved at compile time, evaluated eagerly,
  returns a pre-resolved Promise. Non-literal specifiers: runtime error.
- No namespace exotic objects; `export * as ns` builds a plain object.
- Runner (`tests/test262/run_test262.py`): skips `flags: [module]` tests.

## Design

### Phase 1 — Specifier registry + per-module lazy init (est. ~120–150 tests)

1. **Registry emission (codegen/linker):** table of
   `{canonical specifier utf8 → ModuleRecord*}` where
   `ModuleRecord = {init_fn, namespace_slot, state, error_slot}`.
   States: UNLINKED → EVALUATING → EVALUATED → ERRORED. Emitted as a static
   array + `ts_module_registry_register()` calls in a pre-main ctor, or one
   generated registration function.
2. **Per-module init instead of monolithic startup:** each module's init runs
   its static dependencies' inits first (memoized; EVALUATING state breaks
   cycles), then its own top-level code. Program entry eagerly inits the
   entry module's static graph — startup semantics unchanged for programs
   without dynamic import.
3. **Runtime `ts_dynamic_import(TsValue* specifier)`:**
   ToString(specifier) — a throwing coercion becomes a REJECTION, not a sync
   throw — canonicalize relative to the importing module's dir (import.meta
   machinery already knows it), registry lookup:
   - hit: if EVALUATED → resolved Promise(namespace). If ERRORED → rejected
     Promise(memoized error — the SAME error object every time). Else run
     init inside a handler; capture throw → memoize in error_slot + reject.
   - miss: rejected Promise (TypeError, message naming the specifier).
   Resolution must go through the microtask queue (tests observe timing).
4. **Compiler:** lower `import(expr)` (any expr, not just literals) to
   `ts_dynamic_import`; the Monomorphizer's module discovery keeps bundling
   every literal specifier it can see (unchanged), and ALSO bundles all
   modules reachable from the entry — the registry makes computed specifiers
   over that set work.

GATE: full standard gates + dynamic-import family measure (expect the 118
"not supported at runtime" + part of the 78 residue).

### Phase 2 — Namespace exotic objects + live bindings (est. ~100–200)

1. Namespace object: non-extensible, `@@toStringTag: "Module"`, own keys =
   sorted exported names, throws on set/define in strict, `[[Get]]` reads the
   LIVE binding.
2. Live bindings: exported `let/var` mutations visible through the namespace
   and through named imports. Mechanism: module-level bindings live in a
   per-module slots struct; importer reads go through a slot pointer
   (indirection only for cross-module reads of mutable exports — `const`/
   function exports can stay direct).
3. TDZ: namespace [[Get]] of a not-yet-initialized `let` export throws
   ReferenceError (reuse the NANBOX_TDZ sentinel from phase 5 work).

### Phase 3 — Module-goal execution in the runner (est. ~300–500 of the 601)

1. Runner: compile `flags: [module]` tests with a `--module` compiler flag
   instead of skipping.
2. Compiler `--module`: parse with module goal (strict by default, top-level
   await allowed, import/export legal, `import.meta` legal only here — the 4
   currently-failing "import.meta in FunctionBody must SyntaxError" tests
   fall out of this).
3. Fixture handling: module tests import `*_FIXTURE.js` files — the compiler
   already discovers/bundles relative imports; verify the discovery walks
   from the test file's dir.
4. Top-level-await modules: init_fn is async; import() chains on it; static
   import of a TLA module makes the parent's init await it (spec ordering
   tests will grade this — start with the non-TLA subset).

### Explicitly OUT of scope
- source-phase-imports (216), import-defer (125), import-attributes (64) —
  separate proposals, separate tickets if ever.
- True runtime loading of files not known at compile time (host policy:
  closed world). `eval`-adjacent dynamic module creation.
- Cross-realm module tests.

## Risks / landmines

- **Startup regression**: per-module lazy init must not change observable
  order for static-only programs — the entry graph still evaluates eagerly,
  same order as today's `__module_init` chain. Golden-ir + node suites gate
  this; add a probe comparing top-level side-effect print order on a 3-module
  diamond.
- **Deferred class flush**: class decls currently flush BEFORE
  `__module_init` (see memory `class-roots-diagnosis`); per-module init must
  preserve that relative ordering per module.
- **GC rooting**: ModuleRecord holds namespace/error GC pointers in static
  C++ memory → scanner + minor-fixup registration per
  `.claude/rules/runtime-safety.md` (or store slots via
  `ts_gc_register_root`).
- **longjmp discipline**: init-error capture frame must not hold
  std::string/ICU objects (ts_throw unwinds through it).
- **Registry keys**: canonicalization must match between compile-time
  emission and runtime lookup (case, separators, `./` collapse) on Windows
  paths — single shared canonicalize function, tested both sides.

## Measurement

- Families: `expressions/dynamic-import` (200/938 baseline),
  `language/module-code` (1/583), `expressions/import.meta` (1/22).
- Per-phase family re-measure + standard gates (golden-ir 267/279, node
  295/297, 2k 0-lost) + full-sweep diff before each merge.


## Namespace exotic object — probed slice plan (2026-07-10)

Fresh measure: language/module-code/namespace = 7/36 pass; internals/*
(gOPD/define/delete/get-proto/uninit-TDZ) all fail — no namespace branding
exists. import-star (the test262 self-import shape) binds ns to the module
EXPORTS map via the Monomorphizer self-import path (Monomorphizer.cpp:970-981
makeBinding(namespaceImport, nullptr)).

Slice 1 (cheap, ~8-10 tests): brand the exports map when bound as a
namespace; ladder branches: [[GetPrototypeOf]] -> null (instanceof Object
false follows), [[SetPrototypeOf]] -> only-null-true, [[IsExtensible]] ->
false, [[PreventExtensions]] -> true, @@toStringTag Module (non-writable).
Slice 2: [[Get]] uninit binding -> ReferenceError (TDZ sentinel exists),
[[Delete]] exported -> false, [[DefineOwnProperty]] validation,
[[OwnPropertyKeys]] sorted exports.
Slice 3: dynamic-import/namespace (~30) reuses the brand at the import()
resolution site.
