# CONF-P1 — Builtin Function-Object Infrastructure (identity, arity, descriptors)

**Status:** Scoped / not started
**Phase:** Roadmap Phase 1 (cross-cutting builtin infrastructure)
**Owner:** TBD
**Baseline:** master `a019e41a`, fresh sweep 33,801 / 50,506 pass (66.9% total, ~75.4% of runnable)

---

## 0. TL;DR and the honest sizing correction

The original roadmap assumed "builtin metadata (`.length`/`.name`/not-a-constructor/descriptors)
is broken across the board → fixing it is worth +3,000–5,000 tests." **Exhaustive measurement
disproves that.** The metadata *infrastructure already works*: the factory functions populate a
`properties` map with correct `{writable:false, enumerable:false, configurable:true}` descriptors,
and `Object.getOwnPropertyDescriptor` reroutes through it. The metadata-named test families are
**already 89% passing** (2,145 / 2,796 runnable).

The ~269 remaining failures in those families decompose as:

| Bucket | ~count | Real home |
|---|---|---|
| **Missing methods** (test reads `.length`/`.name`/desc of an undefined method) | ~144 | **Phase 2/3** (implement the method; metadata comes free) |
| **Wrong `.length` arity value** | ~20 | **Phase 1** (W2) |
| **Identity / value-reference** of builtins (escape/unescape/parseInt `===`, `Number.parseInt`) | ~15 | **Phase 1** (W1) |
| **Wrong descriptor attribute** (enumerable/writable) | ~6 | **Phase 1** (W3) |
| **Symbol.prototype.toString** dispatch on primitive symbol | ~3 | **Phase 1** (W4) |
| arguments-object / Atomics / misc | ~10 | other / deferred |

**Revised Phase 1 yield estimate: ~+60–150 directly**, plus it removes a confusing class of
identity bugs and unblocks accurate measurement of the missing-method work in later phases.
**It is a correctness/cleanup phase, not the big multiplier.** The big multipliers are
missing-method families (Phase 3) and the iterator protocol (Phase 2). This correction should
flow back into the roadmap.

---

## 1. How builtin function objects are built today (map)

Factories (all set `name`, `arity`, install `.length`/`.name` into a `properties` TsMap with
`ATTR_CONFIGURABLE` only = `{writable:false, enumerable:false, configurable:true}` ✓):

| Factory | File:line | Used for | is_constructor |
|---|---|---|---|
| `ts_value_make_native_function` | `TsObject_Value.cpp:173` | base allocator (no metadata) | — |
| `makeNamedNativeFunction` | `TsObject.cpp:722` | global fns, on-demand builtins | **false** |
| `addMethod` | `TsGlobals.cpp:180` | prototype + static methods | **false** |
| `makeSimpleConstructorGlobal` | `TsGlobals.cpp:1263` | constructor scaffold (TsMap) | — |
| `wrapAsCallable` | `TsGlobals.cpp:1291` | promote ctor TsMap → callable | **true** |

Metadata READ paths (all correct on value; descriptors exposed via the properties map):
- `fn.length` → `TsObject.cpp:2609` (returns `func->arity`)
- `fn.name` → `TsObject.cpp:2614`
- `fn.prototype` → `TsObject.cpp:2565` (lazy; `undefined` for non-constructors — correct)
- `new fn()` constructability → `TsObject_Call.cpp:523` (`is_constructor` → "is not a constructor")
- `getOwnPropertyDescriptor(fn,'length'/'name')` → `TsObject_ObjectStatics.cpp:2273` reroutes
  into `func->properties`; descriptor synthesis at `:2490`. **Works.**
- `Function.prototype.toString` → `TsObject.cpp:6452` ("function name() { [native code] }").

Conclusion: do **not** rebuild the metadata layer. Fix the four narrow defects below.

---

## 2. Work items

### W1 — Builtin function identity (the real infra bug)

**Symptom:** `parseInt === parseInt` is **false**; `Number.parseInt` is `undefined`;
`escape.length` / `unescape.length` throw ReferenceError.

**Root cause:** bare global identifiers resolve through
`ts_get_builtin_function` (`TsObject.cpp:955-969`), which calls `makeNamedNativeFunction(...)`
**fresh on every reference** (no cache) for: `parseInt, parseFloat, isNaN, isFinite, encodeURI,
encodeURIComponent, decodeURI, decodeURIComponent`. Meanwhile the same names are stored as
*singletons* in `globalMap` (`TsObject.cpp:7406-7625`), so `globalThis.parseInt` is stable but
bare `parseInt` is not. Constructors (Object/Array/…) avoid this via per-name lazy `static void*
cached` getters (`ts_get_global_X`, `TsGlobals.cpp`).

`escape`/`unescape` are worse: they are **only special-cased as CALLS** in the compiler
(`ASTToHIR_Expressions_Calls.cpp:1308-1356`), so a *value* reference (`escape.length`,
`escape === escape`) has no binding → ReferenceError. (Confirmed this session: adding an Analyzer
symbol alone did not fix it — the bare-identifier HIR lowering must also emit a global lookup.)

**Fix approach (pick the smaller of two):**
- **(a) Runtime cache (preferred, smallest):** make `ts_get_builtin_function` memoize each builtin
  in a `static void*` slot (GC-rooted, like the `ts_get_global_X` pattern) so repeated resolution
  returns the same object. One slot per builtin; lazily initialized.
- **(b) Route bare identifiers to the global-map singletons:** in `HIRToLLVM_CallsMethod.cpp`
  `lowerLoadGlobal` (`:987-1175`), add `parseInt/parseFloat/isNaN/isFinite/encode*/decode*/escape/
  unescape` to the hardcoded dispatch so a bare reference loads the already-cached `globalMap`
  entry instead of calling `ts_get_builtin_function`. Also add Analyzer symbol defs
  (`Analyzer_Core.cpp` ~195, `Analyzer_StdLib.cpp` ~173) for `escape`/`unescape` so they resolve
  as values, not just calls.

**Also:** register `Number.parseInt` / `Number.parseFloat` as the **same object** as the globals
(`TsGlobals.cpp:1628-1631`, in `ts_get_global_Number`) — `addMethod` would create a *distinct*
function, so instead assign the cached global parseInt/parseFloat object into Number's
`properties` with `BUILTIN_ATTRS`.

**Files:** `TsObject.cpp:955-969`, `HIRToLLVM_CallsMethod.cpp:987-1175`,
`ASTToHIR_Expressions_Other.cpp:944-1001`, `Analyzer_Core.cpp`/`Analyzer_StdLib.cpp`,
`TsGlobals.cpp:1628`.
**Est. yield:** ~+12–20 (escape/unescape `length`/`name`/`not-a-constructor` ×2, Number.parseInt
prop-desc + identity, parseInt/isNaN/isFinite identity tests). Higher *correctness* value than
test count — removes a whole class of `===`/same-object surprises that also bite real code.
**Risk:** identity caching must be GC-safe (root the static slots; copy the `ts_get_global_X`
pattern exactly). The compiler-lowering route (b) touches HIR — verify golden-IR.

### W2 — Correct `.length` arity values

**Symptom:** `descriptor value should be N` on `length.js` for methods that DO exist but report the
wrong arity (e.g. `Array.prototype.toSpliced.length`, `Function.prototype.apply.length`,
`Iterator/concat.length`). ~20 tests.

**Fix:** audit the `arity` argument passed to every `addMethod`/`makeNamedNativeFunction` call site
against the spec's "length" for each method (the count of required params before the first
optional/rest). This is mechanical; the spec value is in each method's clause. Build a checklist
from the failing `*/length.js` set (124 failures, of which ~20 are real arity-value bugs; the rest
are missing-method, see Scope-Out).

**Files:** `TsGlobals.cpp` (the `addMethod(... , arity)` calls), `TsObject.cpp` builtin table.
**Est. yield:** ~+15–20. **Risk:** very low (pure constant fixes); a `--filter "*/length.js"`
diff per family confirms each.

### W3 — Descriptor attribute correctness

**Symptom:** `descriptor should be enumerable` / writable mismatches (~6). A handful of properties
installed with the wrong attribute mask (e.g. a data property that should be enumerable installed
non-enumerable, or a constant installed writable).

**Fix:** for each failing `prop-desc.js`/`descriptor*.js`, compare the installed
`SetWithAttrs(..., mask)` against the spec descriptor and correct the mask. Attribute constants:
`TsHashTable.h:109` (`ATTR_ENUMERABLE=0x01`, `ATTR_WRITABLE=0x02`, `ATTR_CONFIGURABLE=0x04`).
**Est. yield:** ~+6–10. **Risk:** low; per-test verified.

### W4 — Symbol.prototype.toString on a primitive symbol

**Symptom:** `Symbol('d').toString()` returns `undefined` (should `"Symbol(d)"`).
`Symbol.prototype.toString` IS registered (`TsGlobals.cpp:2261`) and handles both primitive and
wrapper receivers — so this is a **dispatch / prototype-chain** problem: calling `.toString()` on a
*primitive* symbol does not reach `Symbol.prototype.toString`.

**Investigate first:** is this the deferred "primitive-`this` magic-read" crash family, or a
plain "primitive symbol's method lookup doesn't consult Symbol.prototype"? If the method is
reachable but `this` is mis-bound → fixable here. If it needs the primitive-`this` boxing rework →
**defer to its own ticket** (do not absorb into Phase 1).
**Est. yield:** ~+3–8 (Symbol toString/description family). **Risk:** medium — may touch the
primitive-receiver dispatch path; time-box and defer if it widens.

---

## 3. Scope

**IN:** W1 identity + value-references, W2 arity values, W3 descriptor attrs, W4 Symbol.toString
(if shallow).

**OUT (explicitly — these are later phases, surfaced here only because they fail metadata-named
tests):**
- **Missing methods** (~144 fails): `Array.prototype.toLocaleString`, `Symbol.species` accessors,
  `Iterator`/`AsyncIterator` helpers, `FinalizationRegistry.prototype.register/unregister`,
  `WeakRef` edges, `GeneratorPrototype`/`AsyncGeneratorPrototype` methods. → **Phase 2 (iterator
  protocol)** and **Phase 3 (family completion)**. They get correct metadata *for free* once the
  method exists (the factories already do it).
- `Atomics` (not defined) → separate (shared-memory, likely N/A for AOT).
- `language/arguments-object` mapped-descriptor edges (~9) → compiler/semantics (Phase 4).
- The primitive-`this` boxing rework, if W4 needs it → its own ticket.

---

## 4. Measurement & gates

**Baseline (run once, before starting):**
```
cd tests/test262
# per-family metadata snapshot
python run_test262.py --filter "built-ins" --timeout 8 --time-budget-min 20 --fresh \
  --results-file .p1_base.jsonl
# then diff name.js/length.js/not-a-constructor.js/prop-desc.js pass counts (see §0 script)
```
Targeted per-work-item measurement: `--filter "*/length.js"`, `--filter "Number/parseInt"`,
`--filter "annexB/built-ins/escape"`, `--filter "Symbol/prototype/toString"`, common-set
GAINED/LOST vs `.realbaseline.jsonl`.

**Gate every commit (run SEPARATELY — the combined chain exceeds the foreground cap):**
1. `cmake --build build --config Release` (taskkill `ts-aot.exe` first; copy
   `build/src/sharedrt/Release/tsruntime_shared.dll` → `tests/test262/`).
2. golden-ir 267/279.
3. node 295/297.
4. 2k `regression_sample.txt` via `.gate_base.pkl` — **0 REAL lost** (flaky BigInt-TA `--fast`
   3-LOST/8-GAIN is the chronic baseline, not a regression).
5. Per-family `--filter` GAINED/LOST; inspect every LOST `-j1`.

Commit explicit paths, branch + `merge --no-ff`, never leave master red.

---

## 5. Risks & sequencing

- **W1 is the only structurally interesting item** (touches runtime caching + possibly HIR
  lowering). Do it first on its own branch; it's also the one with cross-cutting correctness value.
  If the HIR route (b) proves invasive, ship the runtime-cache route (a) alone — it fixes identity
  without compiler changes.
- **W2/W3 are mechanical** and parallelizable; batch them per family.
- **W4 is a fl: bail to a separate ticket if it reaches into primitive-`this` boxing.**
- **Do NOT chase the missing-method metadata fails here** — they will keep showing up in
  `length.js`/`name.js` measurements; that's expected, they close in Phase 2/3.

**Recommended order:** W1 → W2 → W3 → (W4 timeboxed). Re-baseline after W1 since identity changes
can shift several scattered tests.

---

## 6. Roadmap feedback (important)

This scoping **invalidates the roadmap's Phase 1 estimate (+3,000–5,000)**. Corrected picture:
- Builtin **metadata infra is ~done** (89% passing); Phase 1 is a ~+60–150 correctness cleanup.
- The real multipliers are **(a) missing-method families** (toLocaleString, Iterator helpers,
  species accessors, FinalizationRegistry/WeakRef, async generators) — i.e. Phase 3 family
  completion is *larger* and *earlier-value* than thought, and **(b) the iterator protocol**
  (Phase 2). Recommend reordering: keep W1 (identity) as a quick early win, then prioritize the
  iterator protocol and the missing-method sweep, which is where the volume actually is.

---
## STATUS UPDATE (2026-06-29)
The W1–W4 CORE infrastructure defects (builtin identity caching, .length arity audit, descriptor-attribute fixes, Symbol.prototype.toString primitive dispatch) remain OPEN — none addressed; ticket premise STILL-VALID.
However, several items §3 scoped OUT ("missing methods → Phase 2/3") are now DONE and should be struck from the OUT list: **Array.prototype.toLocaleString** (implemented); **@@species accessors** on Array/Map/Set/Promise/ArrayBuffer/RegExp/TypedArray (symbol-keyed-accessor get-path foundation fix); **%TypedArray%.prototype[@@toStringTag] getter**.
