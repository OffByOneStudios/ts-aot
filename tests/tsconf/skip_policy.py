#!/usr/bin/env python3
"""TSCONF-001 Phase 0: data-driven skip policy for the acceptance sweep.

Derived from tests/tsconf/census.json (2026-07-13, TypeScript v6.0.3,
5,907 conformance tests). Every skip carries a counted reason — silent
skips would recreate the self-grading problem TSCONF-001 exists to kill.

Recorded user defaults (2026-07-13):
- negative policy: permissive-by-design; neg-* scored as a separate family,
  neg-crash must be 0.
- TC39 decorators (esDecorators/): skipped-with-reason family (we implement
  legacy @experimentalDecorators, which is HONORED, not skipped).
- tests/cases/compiler/ corpus: out of the headline until Phase 4.

Library use:
    from skip_policy import classify
    verdict, reason = classify(tc)   # ("run"|"skip", reason-slug)

Dry run:
    python tests/tsconf/skip_policy.py dryrun tests/tsconf/upstream/TypeScript/tests/cases/conformance
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_meta import parse_test  # noqa: E402

# --- directory-level families (relative top dir under conformance/) ---------
SKIP_DIRS = {
    "jsdoc":            "jsdoc-checkjs",      # JS-with-JSDoc checker tests (332/341 multifile)
    "salsa":            "jsdoc-checkjs",      # salsa = the JS inference engine
    "declarationEmit":  "declaration-emit",   # .d.ts emit is out of scope for an AOT backend
    "node":             "module-resolution",  # package.json exports matrix (94/94 multifile)
    "moduleResolution": "module-resolution",  # ditto (51/51 multifile)
    "esDecorators":     "tc39-decorators",    # USER DEFAULT: separate skipped family
}

# --- option-level classification --------------------------------------------
# SKIP options change the language shape or need infra we don't model yet.
# Everything NOT listed here is IGNORED for the acceptance axis: whether the
# source is VALID under its options is already encoded in the .errors.txt
# baseline; strictness/emit-layout flags don't change what we must accept.
SKIP_OPTIONS = {
    "allowjs":  "allowjs",             # JS virtual files + JSDoc checking
    "checkjs":  "allowjs",
    "jsx":      "jsx-mode",            # react/preserve modes need React lib types
    "typeroots": "module-resolution",
    "types":     "module-resolution",
    "traceresolution": "module-resolution",
    "currentdirectory": "module-resolution",
}

# HONORED options the Phase-1 runner maps to real behavior (documented here,
# enforced in the runner): @filename (multi-file split — SKIPPED in the
# initial sweep, see below), @experimentaldecorators (our dialect; compile
# normally), @target/@module (compile ONCE with our default; comma-variants
# collapse to the first value — a counted simplification, not a skip).

import re as _re
_NS_RE = _re.compile(r'^\s*(export\s+)?(declare\s+)?(namespace|module)\s+[A-Za-z_$][\w$.]*\s*\{', _re.M)

# Mixins / dynamic base classes are N/A-BY-DESIGN (recorded in
# docs/conformance/typescript-features.md: "Requires dynamic class
# construction incompatible with AOT"; user-ratified 2026-07-14 in
# TSCONF-002 Phase 2). A class is dynamic-based when it extends a CALL
# (`extends Mixin(Base)`) or a VALUE binding (function parameter /
# const/var holding a class), not a statically-known class declaration.
_EXT_RE = _re.compile(r'\bclass\s+[A-Za-z_$]?[\w$]*\s*(?:<[^>{]*>)?\s*extends\s+([A-Za-z_$][\w$]*)\s*(\()?')

def _is_mixin_test(tc):
    for f in tc.files:
        src = f.content
        for m in _EXT_RE.finditer(src):
            ident, call = m.group(1), m.group(2)
            if ident in ("null", "undefined"):
                continue                  # `extends null` is a static feature
            if call:                      # extends Mixin(Base)
                return True
            if _re.search(r'\bclass\s+' + _re.escape(ident) + r'\b', src):
                continue                  # static base class exists
            if _re.search(r'\bdeclare\s+(const|var|let)\s+' + _re.escape(ident) + r'\s*:', src):
                return True               # extends a declared VALUE
            if _re.search(r'\b(const|let|var)\s+' + _re.escape(ident) + r'\s*=', src):
                return True               # extends a computed value
            if ident[0].islower():
                return True               # parameter-style dynamic base
    return False

def classify(tc, conformance_root=None):
    """(verdict, reason) for the Phase-1 acceptance sweep."""
    rel = tc.path
    if conformance_root:
        rel = os.path.relpath(tc.path, conformance_root)
    top = rel.replace("\\", "/").split("/")[0]
    if top in SKIP_DIRS:
        return "skip", SKIP_DIRS[top]
    for opt, reason in SKIP_OPTIONS.items():
        if opt in tc.options:
            return "skip", reason
    # Namespaces / internal modules are N/A-BY-DESIGN (recorded in
    # docs/conformance/typescript-features.md: "legacy pattern - use ES
    # modules"; user-ratified in the Tier-1 plan 2026-07-14). Identifier-named
    # namespace/module declarations only -- string-named `declare module "x"`
    # is the ambient-module feature, which we DO parse.
    for f in tc.files:
        if _NS_RE.search(f.content):
            return "skip", "namespaces-na"
    if _is_mixin_test(tc):
        return "skip", "mixins-na"
    if tc.is_multifile:
        # The metadata parser already splits these; the initial sweep skips
        # them until the runner grows the virtual-fs + module-link step.
        return "skip", "multifile"
    if tc.path.endswith(".tsx"):
        return "skip", "jsx-mode"
    return "run", ""


def main():
    if len(sys.argv) < 3 or sys.argv[1] != "dryrun":
        print(__doc__)
        return 1
    root = sys.argv[2]
    verdicts = collections.Counter()
    reasons = collections.Counter()
    neg = run_neg = 0
    baseline_dir = os.path.normpath(os.path.join(
        root, "..", "..", "baselines", "reference"))
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith((".ts", ".tsx")):
                continue
            p = os.path.join(dirpath, fn)
            tc = parse_test(p)
            v, r = classify(tc, root)
            verdicts[v] += 1
            if v == "skip":
                reasons[r] += 1
            else:
                base = os.path.splitext(fn)[0]
                err = os.path.join(baseline_dir, base + ".errors.txt")
                if os.path.exists(err):
                    neg += 1
                    run_neg += 1
    print(f"run:  {verdicts['run']}   (of which negative/.errors.txt: {run_neg} "
          f"-> acceptance denominator {verdicts['run'] - run_neg})")
    print(f"skip: {verdicts['skip']}")
    for r, n in reasons.most_common():
        print(f"   {n:5d}  {r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
