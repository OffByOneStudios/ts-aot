# TSCONF upstream corpora — pins and provenance

Vendored 2026-07-05 for TSCONF-001 (see `docs/tickets/TSCONF-001-typescript-conformance-harness.md`).
Both checkouts live under `tests/tsconf/upstream/` and are **gitignored** —
re-fetch with the commands below. Pin policy: last JS/Node-based TypeScript
release line (6.x); TS 7 is the Go compiler (`microsoft/typescript-go`).

## microsoft/TypeScript — tag `v6.0.3`

```sh
git clone --depth 1 --branch v6.0.3 --filter=blob:none --sparse \
    https://github.com/microsoft/TypeScript.git tests/tsconf/upstream/TypeScript
cd tests/tsconf/upstream/TypeScript
git sparse-checkout set --no-cone \
    '/tests/cases/conformance/**' \
    '/tests/baselines/reference/*.errors.txt' \
    '/tests/baselines/reference/*.js'
```

Checked-out inventory (measured at vendor time):

| What | Count |
|------|-------|
| `tests/cases/conformance/` files | 5,908 |
| ...of which multi-file (`@filename`) | 1,373 |
| `tests/baselines/reference/*.errors.txt` (top-level) | 9,055 |
| `tests/baselines/reference/*.js` (top-level) | 13,806 |
| Disk (tests/) | ~101 MB |
| `tests/cases/compiler/` (NOT checked out; in-index count) | 6,537 |

Notes:
- Baselines are shared between `conformance/` and `compiler/` tests (flat
  namespace keyed by test basename), so the baseline counts exceed the
  conformance test count. A conformance test is *negative* iff a matching
  `<basename>.errors.txt` exists.
- `.types`/`.symbols` baselines are deliberately excluded until the
  type-assertion axis is real; add patterns to the sparse-checkout then.
- To add `compiler/` (Phase 4+): `git sparse-checkout add '/tests/cases/compiler/**'`.

## microsoft/TypeScript-Website — commit `1e398d648b3e670423b477f18b64ed1a41943792`

(The website repo has no per-TS-release tags; pinned to `v2` branch HEAD at
vendor time. Content documents the 6.x-era language.)

```sh
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/microsoft/TypeScript-Website.git tests/tsconf/upstream/TypeScript-Website
cd tests/tsconf/upstream/TypeScript-Website
git checkout 1e398d648b3e670423b477f18b64ed1a41943792
git sparse-checkout set --no-cone '/packages/documentation/copy/en/**'
```

Checked-out inventory (measured at vendor time):

| What | Count |
|------|-------|
| English doc `.md` files | 133 |
| ...with twoslash code blocks | 45 |
| ` ```ts twoslash ` blocks | 741 |
| `// @errors:` annotations (negative expectations) | 219 |
| `// ^?` type queries (type assertions) | 241 |
| `release-notes/` pages (feature denominator) | 48 |

## Version bumps

Bump BOTH pins together, deliberately, as a baselined sweep+retriage event
(same discipline as a test262 snapshot update). Update this file and the
TSCONF baseline in the same commit.
