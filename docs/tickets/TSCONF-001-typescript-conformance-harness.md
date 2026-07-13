# TSCONF-001: TypeScript Conformance Harness (test262-grade rigor for TS)

**Status:** Phases 0-2 COMPLETE (2026-07-13) -- runner `tests/tsconf/run_tsconf.py`.
**FIRST MEASURED NUMBERS (TypeScript v6.0.3 conformance corpus):**
- **Acceptance: 1,359/2,088 = 65.1%** (the hand matrix self-reported 99%;
  the honest number is, as predicted, far below).
- Negative family: 1,382 neg_reject / 923 neg_accept (permissive-by-design) /
  **9 neg_crash + 17 crash (must-be-0 gate metric: currently 26)**.
- Runtime oracle (enums/constEnums/decorators/classes/asyncGenerators/
  generators; acceptance-passing non-negative only): **102/118 = 86.4%
  runtime_match**; 15 real semantic diffs (e.g. enumBasics throws at
  runtime), 42 oracle-unrunnable ambient-declare tests counted separately.
- Full acceptance sweep 13s; oracle sweep 25s (cache committed).
Top acceptance clusters (the Phase-4 grind map): ~530 parse errors
(object-type literals in annotation positions etc.), this/arguments
scoping (~37), analyzer undefined-variable families.
Phase 0 record: `tests/tsconf/PHASE0.md`.
Parser (`parse_meta.py`), census (`census.json`), data-derived skip policy
(`skip_policy.py`: run 4,402 / acceptance denominator 2,088 / negative 2,314 /
skip 1,505 across 7 counted reasons), twoslash extractor (795 blocks).
User defaults recorded 2026-07-13: permissive negative policy (neg-crash
gates at 0), TC39 decorators = skipped family, compiler/ corpus out until
Phase 4. Next: Phase 1 acceptance sweep (`ts-aot -c` compile-only).
**Created:** 2026-07-05
**Depends on:** test262 harness infrastructure (`tests/test262/`), gate-battery discipline

## Problem

test262 gives us an externally-authored, ~45k-test, baseline-diffed measurement
of ECMAScript conformance (currently ~90%, 39,259 pass). Our TypeScript-specific
measurement has nothing comparable:

- `docs/conformance/typescript-features.md` is a hand-maintained matrix
  self-reporting 99% (118/119). It is graded by the same agents that implement
  the features, with one ad-hoc probe per row. It is **not a reliable
  measurement** and the project owner has said so explicitly.
- `tests/golden_ir/typescript/` has 151 `.ts` files — regression tests we wrote
  ourselves, not a conformance corpus.

We need a TS measurement with the same properties that made test262 work here:
externally authored, large, machine-scored, baselined as JSONL, clusterable by
failure reason, and gateable per-merge.

## The corpus: microsoft/TypeScript's own test suite

There is no official "ts262", but the TypeScript compiler repo ships the de
facto one:

| Corpus | Size (measured at v6.0.3) | What it exercises |
|--------|---------------------------|-------------------|
| `tests/cases/conformance/` | 5,908 files (1,373 multi-file) | Organized by language area (classes, enums, generics, decorators, es6, jsx, ...) — mirrors our matrix categories |
| `tests/cases/compiler/` | 6,537 files | Historical bug repros, messier metadata |
| Checked-in baselines (`tests/baselines/reference/`) | 9,055 `.errors.txt`, 13,806 `.js` | `.js` (expected emit), `.errors.txt` (expected diagnostics), `.types`, `.symbols` |

(Earlier drafts of this ticket estimated ~12k/~18k from memory; the table now
shows counts measured from the vendored v6.0.3 checkout.)

Vendored 2026-07-05 under `tests/tsconf/upstream/` (gitignored, refetchable —
see `tests/tsconf/UPSTREAM.md` for pins, sparse-checkout commands, and full
inventory). Start with `conformance/` only; `compiler/` is a later expansion.

**Important caveat vs test262:** these tests check *type-checking and emit*,
not runtime behavior. Most have no runtime assertions. The harness must derive
pass criteria per category rather than "run and expect no error" — see below.

## Second corpus: the TypeScript website (Handbook via twoslash)

The user's position is that typescriptlang.org is the source of truth for the
language. That source is mechanically extractable: the Handbook lives in
`microsoft/TypeScript-Website` as markdown, and every code block is compiled by
tsc in the website's own CI via **twoslash** annotations:

- `// @errors: NNNN` — expected diagnostic codes (normative negative tests)
- `// ^?` — asserts the exact type tsc infers at that position
- `// @filename`, compiler-option flags — same metadata style as the compiler suite

Vendor the repo pinned to a release, parse blocks with the existing `twoslash`
npm package, and generate three test kinds: positive (must compile), negative
(expected error codes — curated, authoritative complement to axis 3), and
**type-assertion** (compare `--dump-types` output at the query position against
tsc's expected type string; display-string normalization makes this axis
aspirational, not day-one). The corpus is small (low thousands of blocks) but
*normative and curated* — complementary to the compiler suite's exhaustive but
characterization-style 12k. The same repo's "What's New" release-notes pages
give a per-version feature enumeration to use as the external denominator when
auditing/retiring the hand matrix.

**Why false-accepts matter more here than in tsc:** ts-aot makes codegen
decisions (monomorphization, representation) from inferred types. Accepting
ill-typed code is not mere lint laxity — it can be a miscompile. Even under a
permissive-by-design policy, false-accept counts (differential vs
`tsc --noEmit`) should be measured and reported, and the type-only features
currently marked 🔬 (conditional/mapped/template-literal types, `infer`) are
only safely "erased" while no codegen path depends on evaluating them.

## Vendoring mechanics

Both upstream repos are large; do NOT vendor them whole or as full-history
submodules:

- `microsoft/TypeScript`: multi-GB history. Take a shallow snapshot of one
  release tag, sparse to `tests/cases/conformance/` (+ `tests/cases/compiler/`
  later) and `tests/baselines/reference/` filtered to the matching `.errors.txt`
  / `.js` baselines only (skip `.types`/`.symbols` until the type-assertion
  axis is real). Land under `tests/tsconf/upstream/` with a `VERSION` file
  recording tag + commit, mirroring how `tests/test262/` is vendored.
- `microsoft/TypeScript-Website`: sparse to
  `packages/documentation/copy/en/` (handbook, reference, release-notes
  pages). Small once sparse.

**Pin policy (user decision 2026-07-05):** pin both to the last JS/Node-based
TypeScript release line (6.x) — Microsoft is porting the compiler to Go for
TS 7 (`microsoft/typescript-go`), so 6.x is the stable long-lived anchor. This
also keeps the axis-2 oracle dependency-light: `npm install typescript@<pin>`
+ node, no Go toolchain. Verify at Phase 0 which exact 6.x release is latest
and that its docs snapshot matches. Bump both repos together, deliberately, as
a baselined event — a version bump is a "sweep + retriage" occasion exactly
like a test262 snapshot update. Revisit only if the Go-era suite diverges in
ways that matter (new language features we choose to chase).

## What "conformance" means for an AOT compiler (three axes)

1. **Acceptance (must-compile):** every test whose baseline has an empty/absent
   `.errors.txt` is valid TS. We must parse + analyze + compile it without
   error. This is the biggest and cheapest axis (~pure corpus sweep) and
   directly measures the false-reject rate that blocks real-world code.
2. **Runtime semantics:** the subset of TS features with runtime-observable
   behavior (enums incl. const/reverse-map, parameter properties, legacy
   decorators, class field emit order, `accessor`, namespaces-if-ever, async
   lowering interactions). For tests in these directories: compile with ts-aot,
   run, and compare stdout/exit-code against **node executing tsc's emitted
   JS** (the oracle run, cached). Tests with no observable output score on
   "compiles + runs + exit 0".
3. **Rejection (negative tests):** tests with a non-empty `.errors.txt`
   baseline are invalid TS. Scoring policy decision needed (see Open
   Questions): full diagnostic parity is a type-checker project we do NOT
   want; the useful invariant is *"ts-aot does not crash, and ideally rejects"*.
   Propose scoring these as a separate family (`neg-accept` / `neg-reject` /
   `neg-crash`) so crashes are visible but permissive acceptance doesn't drag
   the headline number.

## Harness design (mirror the test262 runner)

TypeScript test files use inline metadata the harness must honor:

- `// @filename: a.ts` — multi-file virtual filesystem → split into a temp dir,
  compile the entry.
- `// @target`, `// @module`, `// @strict`, etc. — compiler options. Map the
  handful we care about; tests using unsupported options (e.g. `@declaration`,
  `@jsx: react` variants we don't do) get skipped with a counted reason, never
  silently.
- Tests exercising type-only machinery with zero runtime surface (conditional
  types, mapped types, infer) score on axis 1 only.

Runner: clone `tests/test262/` runner structure — same JSONL result format
(`{path, status, axis, reason}`), same baseline file (`.tsconf_baseline.json`),
same clustering scripts (`analyze.py`/`cluster.py` should port with minor
changes), same gate-battery integration so a runtime change gates against
*both* baselines before merge.

## Phases

1. **Phase 0 — Vendor + inventory: DONE (vendor 2026-07-05, census+policy 2026-07-13).** Both
   corpora pinned and checked out under `tests/tsconf/upstream/` (gitignored):
   microsoft/TypeScript **v6.0.3** (npm `latest`; 7.0.1-rc is the Go compiler,
   confirming the 6.x pin policy) and TypeScript-Website **1e398d64**.
   Measured inventory in `tests/tsconf/UPSTREAM.md`: 5,908 conformance tests
   (1,373 multi-file), 9,055 `.errors.txt` + 13,806 `.js` baselines, ~101 MB;
   website: 741 twoslash blocks, 219 `@errors`, 241 `^?` queries, 48
   release-notes pages. Remaining Phase 0 work: metadata/twoslash parser +
   per-directory option/JSX breakdown → skip-lists from data.
2. **Phase 1 — Acceptance sweep: DONE 2026-07-13.** Original scope: axis-1 runner over
   `conformance/`, first baseline commit. This alone replaces the matrix's
   self-reported number with a measured one. Expect the honest number to be
   **well below 99%** — that is the point.
3. **Phase 2 — Runtime oracle: DONE 2026-07-13** (tsc 6.0.3 pinned under tests/tsconf/oracle/, cache committed). Original scope: tsc+node oracle runner with
   cached expected outputs (needs tsc + node on the dev machine, build-time
   only; nothing ships). Score axis 2 for the runtime-feature directories.
4. **Phase 3 — Negative-family + dashboards (1 session):** neg-* scoring,
   wire into `docs/conformance-dashboard/snapshots.jsonl`, retire or footnote
   the hand matrix ("measured by TSCONF, see baseline").
5. **Phase 4+ — Grind:** measure-first-drill / cluster / fix cycles, identical
   to the test262 workflow. Expand to `tests/cases/compiler/` when
   `conformance/` plateaus.

## Success criteria

- A single command produces a measured TS conformance number from ~12k
  externally-authored tests, with JSONL baseline and 0-regression gating.
- The hand matrix is no longer the source of truth for TS status.
- Headline metric proposal: **axis-1 acceptance %** (primary),
  axis-2 runtime-match % (secondary), neg-crash count (must be 0).

## Risks / gotchas

- **Baseline drift:** TypeScript's suite tracks tsc HEAD, including features we
  will never do (declaration emit, project references). Pin one release tag
  (suggest the version matching our parser's grammar support) and only bump
  deliberately.
- **Oracle cost:** running node per test for axis 2 is slow; cache oracle
  outputs keyed by (test hash, tsc version) as a committed artifact, like
  test262 family JSONLs.
- **Skip-list honesty:** every skip must carry a counted reason in the JSONL.
  Silent skips would recreate the self-grading problem this ticket exists to
  kill.
- **Multi-file tests** exercise our module linker (CONF-P3 territory) — expect
  an early cluster there.

## Open questions (user decisions)

1. **Negative-test policy:** is "accepts invalid TS" acceptable-by-design
   (ts-aot as permissive compiler, like `tsc --noCheck` + emit), or do we
   eventually want diagnostic parity? Recommendation: permissive by design,
   track neg-crash only.
2. **Decorators dialect:** TS suite covers both legacy (`experimentalDecorators`)
   and TC39 decorators. We implement legacy. Score TC39-decorator tests as a
   separate family or skip?
3. **Scope of `compiler/` (18k messier tests):** in or out of the headline
   number? Recommendation: out until Phase 4.
