# TSCONF-001 Phase 0 — census + skip policy (COMPLETE 2026-07-13)

Corpora pinned 2026-07-05 (see `UPSTREAM.md`); this file records the second
half of Phase 0: the metadata parser, the measured census, and the
data-derived skip policy for the Phase-1 acceptance sweep.

## Tooling (this directory)

| File | Role |
|------|------|
| `parse_meta.py` | `// @option:` header parser + `@filename` multi-file splitter + variant counter. Library for the Phase-1 runner; `census` CLI mode. Verified: multi-file split and comma-variant expansion checked against real tests. |
| `skip_policy.py` | `classify(tc) -> (run\|skip, reason)`. Data-derived, every skip counted. `dryrun` CLI mode. |
| `extract_twoslash.py` | Website corpus extractor → `twoslash.json` (795 blocks). |
| `census.json` | Committed census snapshot (per-directory options/multifile/tsx/variants). |
| `twoslash.json` | Generated (regenerate with the extractor; gitignored). |

## Census headlines (5,907 conformance tests, TypeScript v6.0.3)

- Options by frequency: `@target` 5,415 · `@strict` 1,481 · `@module` 860 ·
  `@allowjs` 555 · `@checkjs` 536 · `@declaration` 530 · `@noemit` 469 ·
  `@lib` 327 · `@jsx` 227 · `@experimentaldecorators` 134 · long tail ≤117.
- 1,373 multi-file (`@filename`); concentrated in jsdoc (332), jsx (178),
  salsa (179), externalModules (162), node (94), moduleResolution (51).
- 656 tests fan out into >1 variant via comma-valued `@target`/`@module`.

## Phase-1 sweep shape (from `skip_policy.py dryrun`)

| Bucket | Count | Notes |
|--------|-------|-------|
| **run** | **4,402** | attempted by the initial sweep |
| — acceptance denominator | **2,088** | no `.errors.txt` → must compile (headline %) |
| — negative family | 2,314 | has `.errors.txt` → `neg-accept`/`neg-reject`/`neg-crash`; permissive policy, **neg-crash must be 0** |
| skip: jsdoc-checkjs | 532 | JS-with-JSDoc checker tests (jsdoc/ + salsa/ + @allowjs/@checkjs) |
| skip: multifile | 404 | parser already splits them; runner grows virtual-fs + link later |
| skip: jsx-mode | 218 | react/preserve modes need React lib types |
| skip: module-resolution | 165 | node/ + moduleResolution/ + @typeroots/@types etc. |
| skip: tc39-decorators | 110 | **user default 2026-07-13**: separate family (we implement legacy `@experimentalDecorators`, which is honored, not skipped) |
| skip: allowjs | 53 | @allowjs outside the jsdoc dirs |
| skip: declaration-emit | 23 | declarationEmit/ |

Simplifications recorded (not skips): comma-variant tests compile ONCE with
the first value; strictness/emit-layout options are IGNORED for acceptance
(validity under the test's own options is already encoded in `.errors.txt`).

## Recorded user defaults (2026-07-13)

1. Negative policy: **permissive-by-design** — `neg-accept` is fine,
   `neg-crash` gates at 0. False-accept counts still reported (miscompile
   risk: ts-aot's codegen trusts inferred types).
2. TC39 decorators: skipped-with-reason family.
3. `tests/cases/compiler/` (6,537): out of the headline until Phase 4.

## Website corpus (twoslash) inventory

795 blocks (737 ts/tsx): 466 positive (must-compile), 215 negative
(`@errors: NNNN`), 128 type-assertion (`^?`, aspirational axis).

## Phase 1 notes

- Compile-only path: `ts-aot -c` (emits .obj, no link) — the cheap
  clean-exit acceptance check.
- Runner clones `tests/test262/run_test262.py` structure: JSONL results
  `{path, status, axis, reason}`, `.tsconf_baseline.json`, same clustering
  + gate-battery integration.
