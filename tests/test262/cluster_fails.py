#!/usr/bin/env python3
"""Cluster test262 failures by normalized failure SIGNATURE (cross-path), not by
spec-area path. Path clustering groups tests by what they test; signature
clustering groups them by why they fail -- which is what you need to find a
single root cause that flips many tests.

Usage:
    python cluster_fails.py                 # report: categories + in-scope/OOS clusters + migrations
    python cluster_fails.py --structural    # re-bucket by structural FAMILY (got-undefined /
                                            #   missing-throw / crash / ...) + per-family spec-area.
                                            #   THIS is the view that finds shared roots: one bug
                                            #   surfaces with many messages across many dirs, and a
                                            #   read returning undefined fakes 1000s of "mismatches".
    python cluster_fails.py --family <name> # list in-scope tests in a structural family (substring)
    python cluster_fails.py --sig "<text>"  # list tests matching a signature substring
    python cluster_fails.py --crashes       # just the crash subset (for -g re-triage)
    python cluster_fails.py --oos           # list out-of-scope fails with their tag
    python cluster_fails.py --include-oos   # legacy single table, OOS not partitioned

Workflow to find a STRUCTURAL root (not chase edge cases):
    1. --structural             -> see which families dominate + where they concentrate
    2. --family got-undefined   -> list the tests in the biggest family
    3. reduce 2-3 to a minimal repro (run_test262.py --one) -> confirm ONE root
    4. probe siblings: does the same root also crash / no-throw elsewhere?

Key metric: a signature with a HIGH count concentrated in FEW paths is the best
single-root-cause candidate. A high count spread over many paths is heterogeneous
(many small causes sharing a message) -- harder, lower ROI per fix.

OOS partitioning (2026-06-09 review finding: ~15% of the fail set is
AOT-out-of-scope and 3 of the top-8 concentration clusters were 100% OOS bait):
fails are tagged by path (staging/sm, staging, annexB, intl402) and by source
content (with-statement, Function(string) constructor, $262.evalScript /
createRealm). Tagged = LABELED, not hidden -- staging/sm is often
content-winnable plain ECMA-262; the tag means "skipped by policy", and the
in-scope table is what the lever loop should mine.

Signature-migration sidecar (.sig_prev.json): each run against a CHANGED results
file reports tests whose failure signature migrated since the previous snapshot
(the "fixed assertion 1, now failing assertion 2" whack-a-mole effect --
measured ~0.3% as of 2026-06-09; this keeps it observable).
"""
import json, collections, re, sys, os, hashlib

# test262 reasons carry arbitrary Unicode (Cyrillic, emoji, U+FFFD markers);
# force utf-8 so piping to a cp1252 console doesn't crash mid-report.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
JSONL = os.path.join(HERE, ".test262_results.jsonl")
SIG_PREV = os.path.join(HERE, ".sig_prev.json")
TEST_ROOT = os.path.join(HERE, "test262", "test")

OOS_PATH_PREFIXES = [
    ("staging/sm/", "sm"),
    ("staging/", "staging"),
    ("annexB/", "annexB"),
    ("intl402/", "intl402"),
]
RE_WITH = re.compile(r"\bwith\s*\(")
RE_FUNC_CTOR = re.compile(r"\bFunction\s*\(\s*['\"]")
RE_262 = re.compile(r"\$262\s*\.\s*(evalScript|createRealm)")


def load():
    seen = {}
    with open(JSONL, "rb") as f:
        for raw in f:
            try:
                r = json.loads(raw.decode("utf-8", "ignore"))
                seen[r.get("path", "")] = r
            except Exception:
                pass
    return seen


def signature(r):
    """Normalize a failure into a structural signature: drop concrete values
    (quoted strings, numbers, hex, the U+FFFD-delimited SameValue payloads) so
    that '...SameValue(?3?, ?4?)...' and '...SameValue(?7?, ?9?)...' collapse."""
    st = r.get("status")
    if st != "fail":
        return f"[{st}]"
    s = (r.get("reason") or "").strip()
    s = re.sub(r"^Uncaught:?\s*", "", s)
    s = re.sub(r"�.*?�", "V", s)   # SameValue ?X? markers
    s = re.sub(r'"[^"]*"', "'S'", s)
    s = re.sub(r"\b0x[0-9a-fA-F]+\b", "H", s)
    s = re.sub(r"\b\d+\b", "#", s)
    s = re.sub(r"\s+", " ", s).strip()
    return s[:90]


def is_crash(r):
    blob = (r.get("reason") or "") + " " + (r.get("stderr") or "")
    return ("VectoredException" in blob or "CRASH" in blob
            or "access violation" in blob.lower())


def oos_tag(path, _cache={}):
    """Return an out-of-scope tag for this test, or None (in scope).
    Path checks are free; source checks read the test file once (cached)."""
    p = path.replace("\\", "/")
    for prefix, tag in OOS_PATH_PREFIXES:
        if p.startswith(prefix):
            return tag
    if p in _cache:
        return _cache[p]
    tag = None
    try:
        with open(os.path.join(TEST_ROOT, path), "r",
                  encoding="utf-8", errors="ignore") as f:
            src = f.read()
        if RE_WITH.search(src):
            tag = "with"
        elif RE_FUNC_CTOR.search(src):
            tag = "Function-ctor"
        elif RE_262.search(src):
            tag = "realm/evalScript"
    except OSError:
        pass
    _cache[p] = tag
    return tag


def spec_area(path):
    """Collapse a path into a coarse, actionable spec-area LEVER bucket — the
    dimension the reason-signature view lacks. language/* -> language/<a>/<b>;
    built-ins/X[/prototype] -> built-ins/X[/prototype]. Lets you see where the
    winnable mass concentrates by feature area, not just by failure message."""
    p = path.replace("\\", "/")
    parts = p.split("/")
    if parts[0] == "language":
        return "/".join(parts[:3])
    if parts[0] == "built-ins":
        if len(parts) >= 3 and parts[2] == "prototype":
            return f"built-ins/{parts[1]}/prototype"
        return f"built-ins/{parts[1]}"
    return "/".join(parts[:2])


def category(r):
    """Coarse failure-reason category. The concentration metric structurally
    buries low-concentration POLICY buckets (e.g. 506 parse-leniency fails over
    75 prefixes); this view surfaces them."""
    if r.get("status") == "timeout":
        return "timeout"
    if r.get("status") == "compile_error":
        return "compile-error"
    reason = r.get("reason") or ""
    if is_crash(r):
        return "crash"
    if "expected parse error" in reason.lower():
        return "parse-leniency"
    m = re.search(r"Expected a (\w+) to be thrown but no exception", reason)
    if m:
        return f"no-{m.group(1)}"
    if re.search(r"Expected a \w+ to be thrown but a \w+", reason) or \
       re.search(r"Expected a \w+ but got", reason):
        return "wrong-exception"
    if "is not defined" in reason:
        return "binding-missing"
    if "SameValue" in reason or "Expected true but got false" in reason \
       or "Expected false but got true" in reason:
        return "value-mismatch"
    if "Cannot read properties" in reason or "is not a function" in reason \
       or "Cannot destructure" in reason:
        return "runtime-threw"
    if not reason.strip() or "no message" in reason:
        return "opaque"
    return "other"


RE_GOT_UNDEF = re.compile(r"SameValue\([«�]?undefined")


def structural_family(r):
    """Finer than category(): map a failure to a symptom CLASS that tends to
    share ONE structural root, so a few roots explain thousands of fails.

    The key split category() misses: 'got-undefined' (a read/lookup/install
    returned undefined where a value was due) vs a generic value-mismatch.
    A read silently returning undefined is the classic multiplier — it fakes
    thousands of distinct 'mismatches' across every dir. Likewise CRASH is
    pulled out of the fail bucket (a VectoredException is recorded as 'fail',
    not 'crash' status — it hides there)."""
    if r.get("status") == "timeout":
        return "timeout"
    if r.get("status") == "compile_error":
        return "compile-error"
    reason = r.get("reason") or ""
    if is_crash(r):
        return "CRASH (access-violation, hidden in fail bucket)"
    if "expected parse error" in reason.lower():
        return "PARSER-too-lenient (missing early error)"
    if "no exception was thrown" in reason:
        return "MISSING-THROW (spec throw not raised)"
    if RE_GOT_UNDEF.search(reason):
        return "GOT-UNDEFINED (read/install returned undefined)"
    if "without async flag" in reason or "without calling $DONE" in reason \
       or "asyncTest" in reason:
        return "ASYNC-HARNESS ($DONE / async-flag integration)"
    if "Cannot read properties" in reason or "Cannot destructure" in reason:
        return "NULL-DEREF (read prop of undefined/null)"
    if "is not a function" in reason:
        return "NOT-CALLABLE (x is not a function)"
    if "is not defined" in reason:
        return "BINDING-MISSING (identifier not defined)"
    if "should have an own property" in reason or "descriptor should be" in reason \
       or "verifyProperty" in reason:
        return "PROPERTY-DESCRIPTOR (own-prop / verifyProperty)"
    if "SameValue" in reason or "Expected true but got false" in reason \
       or "Expected false but got true" in reason:
        return "VALUE-MISMATCH (wrong non-undefined value)"
    if not reason.strip() or "no message" in reason:
        return "opaque (no message)"
    return "other"


def report_structural(seen):
    """Re-bucket in-scope fails by structural FAMILY, then decompose each family
    by spec-area. Reveals the handful of roots behind the long tail; the
    per-area split tells you where to reduce a minimal repro first."""
    fails = {p: r for p, r in seen.items()
             if r.get("status") not in ("pass", "skip")}
    in_scope = {p: r for p, r in fails.items() if not oos_tag(p)}
    fam = collections.Counter()
    fam_area = collections.defaultdict(collections.Counter)
    fam_ex = {}
    for p, r in in_scope.items():
        f = structural_family(r)
        fam[f] += 1
        fam_area[f][spec_area(p)] += 1
        fam_ex.setdefault(f, p.replace("\\", "/"))
    print(f"== STRUCTURAL FAMILIES (in-scope fails: {len(in_scope)}) ==")
    print("Each family tends to share a few roots; reduce a sample per top area.\n")
    for f, n in fam.most_common():
        print(f"{n:6d}  {f}")
        # Drop only the leading built-ins/language segment so Array/prototype
        # vs DataView/prototype stay distinct (the bare leaf 'prototype' hides it).
        top = "  ".join(f"{a.split('/', 1)[-1]}:{c}"
                        for a, c in fam_area[f].most_common(5))
        print(f"        by-area: {top}")
        print(f"        e.g.   : {fam_ex[f]}")


def fingerprint():
    h = hashlib.md5()
    with open(JSONL, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def report_migrations(cur_sigs):
    """Compare current (path -> signature) against the previous snapshot;
    report fixed / new / migrated; update the sidecar when the jsonl changed."""
    fp = fingerprint()
    prev = {}
    if os.path.exists(SIG_PREV):
        try:
            with open(SIG_PREV, "r", encoding="utf-8") as f:
                prev = json.load(f)
        except Exception:
            prev = {}
    if prev.get("fingerprint") == fp:
        print(f"\n[migrations] results file unchanged since last snapshot "
              f"({len(prev.get('sigs', {}))} fails tracked)")
        return
    psigs = prev.get("sigs", {})
    if psigs:
        fixed = [p for p in psigs if p not in cur_sigs]
        new = [p for p in cur_sigs if p not in psigs]
        migrated = [(p, psigs[p], cur_sigs[p]) for p in cur_sigs
                    if p in psigs and psigs[p] != cur_sigs[p]]
        print(f"\n[migrations] vs previous snapshot: {len(fixed)} fixed, "
              f"{len(new)} newly failing, {len(migrated)} MIGRATED signature")
        for p, old, newsig in migrated[:20]:
            print(f"  MIGRATED {p.replace(chr(92), '/')}\n"
                  f"      was: {old}\n      now: {newsig}")
        if len(migrated) > 20:
            print(f"  ... and {len(migrated) - 20} more")
    else:
        print("\n[migrations] no previous snapshot; recording this one")
    with open(SIG_PREV, "w", encoding="utf-8") as f:
        json.dump({"fingerprint": fp, "sigs": cur_sigs}, f)


def main():
    seen = load()
    if "--sig" in sys.argv:
        needle = sys.argv[sys.argv.index("--sig") + 1]
        for p, r in seen.items():
            if r.get("status") == "fail" and needle in (r.get("reason") or ""):
                print(p.replace("\\", "/"))
        return
    if "--crashes" in sys.argv:
        for p, r in seen.items():
            if r.get("status") == "fail" and is_crash(r):
                print(p.replace("\\", "/"))
        return
    if "--structural" in sys.argv:
        report_structural(seen)
        return
    if "--family" in sys.argv:
        # List in-scope tests in a given structural family (substring match).
        needle = sys.argv[sys.argv.index("--family") + 1].lower()
        for p, r in sorted(seen.items()):
            if r.get("status") in ("pass", "skip") or oos_tag(p):
                continue
            if needle in structural_family(r).lower():
                print(p.replace("\\", "/"))
        return

    fails = {p: r for p, r in seen.items()
             if r.get("status") not in ("pass", "skip")}

    if "--oos" in sys.argv:
        for p, r in sorted(fails.items()):
            tag = oos_tag(p)
            if tag:
                print(f"{tag:16s} {p.replace(chr(92), '/')}")
        return

    partition = "--include-oos" not in sys.argv
    tags = {p: (oos_tag(p) if partition else None) for p in fails}
    n_oos = sum(1 for t in tags.values() if t)
    tag_counts = collections.Counter(t for t in tags.values() if t)

    print(f"total non-pass/non-skip: {len(fails)}   "
          f"in-scope: {len(fails) - n_oos}   out-of-scope: {n_oos} "
          f"({dict(tag_counts)})" if partition else
          f"total non-pass/non-skip: {len(fails)}")

    # --- HONEST conformance: pass / (pass + in-scope fails). Out-of-scope and
    # skipped tests are excluded since they aren't winnable in an AOT compiler.
    n_pass = sum(1 for r in seen.values() if r.get("status") == "pass")
    n_inscope = len(fails) - n_oos
    if partition and (n_pass + n_inscope):
        print(f"HONEST in-scope conformance = {n_pass}/{n_pass + n_inscope} "
              f"= {100 * n_pass / (n_pass + n_inscope):.1f}%  "
              f"(pass / (pass + in-scope-fail); excludes OOS + skip)")

    # --- reason-category view (in-scope vs OOS) -------------------------
    cat_in = collections.Counter()
    cat_oos = collections.Counter()
    for p, r in fails.items():
        (cat_oos if tags[p] else cat_in)[category(r)] += 1
    print(f"\n{'category':<18}{'in-scope':>9}{'OOS':>7}")
    for cat, n in cat_in.most_common():
        print(f"{cat:<18}{n:>9}{cat_oos.get(cat, 0):>7}")
    for cat in cat_oos:
        if cat not in cat_in:
            print(f"{cat:<18}{0:>9}{cat_oos[cat]:>7}")

    # --- concentration tables -------------------------------------------
    def table(subset, title, limit=30):
        buckets = collections.Counter()
        paths = collections.defaultdict(set)
        for p in subset:
            g = signature(fails[p])
            buckets[g] += 1
            parts = p.replace("\\", "/").split("/")
            paths[g].add("/".join(parts[:3]))
        rows = [(g, n, len(paths[g])) for g, n in buckets.items() if n >= 20]
        rows.sort(key=lambda t: (t[1] / t[2], t[1]), reverse=True)
        if not rows:
            return
        print(f"\n== {title} ==  ({len(subset)} tests, {len(buckets)} signatures)")
        print(f"{'count':>6} {'#paths':>6} {'conc':>5}  signature")
        for g, n, np_ in rows[:limit]:
            print(f"{n:6d} {np_:6d} {n/np_:5.0f}  {g}")

    in_scope = [p for p in fails if not tags[p]]
    oos = [p for p in fails if tags[p]]

    # --- LEVER analysis. A spec-AREA (class, Array) is NOT a lever unless it
    # has a unified root cause; most are heterogeneous (Array/prototype alone
    # has 300+ distinct signatures). Real levers are (a) cross-cutting FEATURES
    # and (b) coherent LEAF clusters = one narrow dir sharing one signature.
    if partition:
        # Honest decomposition: how much sits in workable clusters vs the
        # irreducible singleton tail (one-at-a-time bugs, no shared fix).
        leaf = collections.Counter((os.path.dirname(p.replace("\\", "/")),
                                    signature(fails[p])) for p in in_scope)
        N = len(in_scope) or 1
        big = sum(n for n in leaf.values() if n >= 8)
        mid = sum(n for n in leaf.values() if 3 <= n < 8)
        tail = sum(n for n in leaf.values() if n < 3)
        print(f"\n== LEVER decomposition of {N} in-scope fails ==")
        print(f"  workable leaf clusters (>=8 same dir+sig): {big:5d} ({100*big//N}%)")
        print(f"  small clusters (3-7):                      {mid:5d} ({100*mid//N}%)")
        print(f"  irreducible singletons/pairs (no lever):   {tail:5d} ({100*tail//N}%)")

        # Cross-cutting FEATURE levers (one engine spanning many paths) — these,
        # not the spec-areas, are the real big levers. Detected by path token.
        XCUT = [("destructuring", "/dstr/"), ("private-#", "-private"),
                ("static-elements", "static"), ("computed-names", "computed"),
                ("subclass-builtins", "/subclass-builtins/")]
        print("  -- cross-cutting feature levers --")
        for label, tok in XCUT:
            n = sum(1 for p in in_scope if tok in p.replace("\\", "/"))
            if n >= 30:
                print(f"     {n:5d}  {label}  ({tok})")

        # Coherent LEAF levers: one narrow dir + one signature ~= one root cause.
        print("\n== coherent LEAF levers (dir x signature, >=10) ==")
        for (d, s), n in leaf.most_common(20):
            if n >= 10:
                print(f"  {n:4d}  {d}\n        ^ {s[:64]}")

        # Spec-area distribution kept for reference — feature spread, NOT levers.
        areas = collections.Counter(spec_area(p) for p in in_scope)
        print(f"\n== in-scope by spec-area (reference; NOT levers - see above) ==")
        for a, n in areas.most_common(12):
            print(f"  {n:5d}  {a}")

    table(in_scope, "IN-SCOPE clusters (mine these)" if partition
          else "ALL clusters")
    if partition and oos:
        table(oos, "OUT-OF-SCOPE clusters (policy-skipped; sm may be winnable)",
              limit=10)

    # --- signature-migration sidecar ------------------------------------
    cur_sigs = {p: signature(r) for p, r in fails.items()
                if r.get("status") == "fail"}
    report_migrations(cur_sigs)


if __name__ == "__main__":
    main()
