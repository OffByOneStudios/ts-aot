# CONF: Private-fields brand-check subsystem — measured diagnosis (BANKED)

Status: BANKED (no code landed). Every recoverable cluster requires a deep
compiler-pipeline or codegen change carrying real regression risk against the
~1,410 currently-passing class tests. Diagnosis captured here so the next
session resumes from the blocker, not from scratch.

## Measurement (this session)

Focused sweep `language/statements/class/elements`, 1,532 run:
- Passed 1,410 / Failed 122 / 0 crash / 0 compile-error / Pass 92.0%.

Failure decomposition (clustered by real signature, not path):

| Cluster | ~count | Root cause | Scope |
|--------|-------|-----------|-------|
| literal-names "descriptor value should be fn()" | ~13 | synthetic-ctor field-init cannot resolve module-global identifiers (Bug S) | in-repo, deep pipeline |
| "Expected a TypeError to be thrown" (privatefield{get,set,add}-typeerror-*, double-initialisation) | ~15 | per-field install ORDERING brand semantics (Bug O) + per-eval brands (Bug A2) | deep |
| private-*-brand-check-multiple-evaluations-of-class | ~6-8 | per-evaluation brand identity (Bug A2) | codegen |
| *-visible-to-direct-eval, direct-eval-contains-superproperty | ~18 | direct-eval private scoping | OUT OF SCOPE (per task) |
| prod-private-*-before-super-return | 6 | private methods/accessors installed-before-super timing | deep |
| scattered singles (computed-name eval-error order, static-field-init-this, redeclaration, nested-class private scope over-throw) | rest | assorted | mixed |

## Bug A.1 — basic brand check: ALREADY WORKS (verified, +0)

Probe `class C{ #x=5; static has(o){return #x in o;} m(o){return o.#x;} }`:
- `#x in nonInstance` → `false` ✓
- `o.#x` on non-instance → throws TypeError ✓

The runtime already brand-checks via class-name-qualified hidden keys
(`\x01#x@ClassName`) in `ts_object_get_private` / `ts_object_set_private`
(src/runtime/src/TsObject.cpp ~4852/4923) and `#x in o` lowering
(src/compiler/hir/ASTToHIR_Expressions_Binary.cpp ~240). No change needed for
the basic cases the task flagged as "likely returns undefined/succeeds" — they
already pass.

## Bug A.2 — per-evaluation brand identity: BANK (needs codegen)

Verified broken:
```
function make(){ return class C { #x=1; static has(o){ return #x in o; } }; }
let A=make(), B=make(); let a=new A(), b=new B();
A.has(a)=true  A.has(b)=true(WRONG, want false)  B.has(a)=true(WRONG)  B.has(b)=true
```
Root: brands are keyed by the textual class NAME (`resolvePrivateName` →
`#x@C`, src/compiler/hir/ASTToHIR.h ~357). Two evaluations of the same source
`class C` share the compile-time key, so an eval-1 instance satisfies eval-2's
brand.

Fix shape (NOT contained to ASTToHIR + runtime storage → banked): allocate a
unique brand TOKEN per ClassDefinitionEvaluation, install it on instances, and
CAPTURE it into each method/accessor closure (distinct method function objects
per evaluation) so private get/set/`in` check the receiver carries THAT token.
Threading the token through method closures is a codegen change
(HIRToLLVM_Closures + Monomorphizer method specs), exactly the class the task
said to bank.

## Bug S — synthetic-ctor field-init module-global resolution: BANK (pipeline order)

Root-caused with env-gated instrumentation (`TS_DBG_SYNCTOR`, since reverted).
A public/private field initializer that references an OUTER binding resolves to
`const.undefined` ONLY when the class has NO explicit constructor:
```
const fn = function(){};
class C { f = fn; }           // new C().f === undefined  (BUG)
class D { f = fn; constructor(){} }  // new D().f === fn   (OK)
```
Mechanism: field initializers are lowered inline into the constructor. The
synthetic default constructor (src/compiler/hir/ASTToHIR_Classes.cpp ~824-951)
lowers them in the class first-pass, where the enclosing module's binding `fn`
is not yet registered as a module global (`isModuleGlobalVar("fn")==false`), so
`fn` resolves to undefined. EXPLICIT constructors escape because the
Monomorphizer RE-LOWERS their body after module-global registration
(`isMGV==true`); IMPORTED classes escape via the spec-loop default-ctor
generator (src/compiler/hir/ASTToHIR.cpp ~743, correct context). The
compiler-synthesized default ctor for a MAIN-file class is never re-lowered.

This is the actual root of the task's "Bug C — literal-names descriptors": the
property DESCRIPTORS are already correct (verified: methods
`{writable,!enumerable,configurable}`, fields `{writable,enumerable,configurable}`
via `ts_object_set_method` METHOD_ATTRS and the flat-object field path). The
literal-names tests fail on the `c = fn` (module-const) field being undefined,
not on descriptor attributes.

Fix shape (NOT contained → banked): either (a) synthesize an empty explicit
constructor into the AST BEFORE the Monomorphizer runs (cross-stage change, so
the normal re-lowered path handles field inits), or (b) defer the
synthetic-ctor field-init lowering to a post-module-global-registration pass
(new infra mirroring the imported-class generator at ASTToHIR.cpp ~743). Both
touch class-lowering order with regression surface across the ~1,410 passing
class tests; must be gated (golden 280 + node 304 + focused class sweeps 0-lost).

## Bug O — per-field install ordering brand semantics: BANK

`class C { y = this.#x = 1; #x; }` must throw TypeError (setting `#x` before its
own declaration-order installation). Currently no throw. Requires
ordering-aware field installation: the brand for `#x` must not exist until the
`#x;` declaration executes, and `ts_object_set_private` must brand-check against
fields installed SO FAR, not all declared. Subsystem change to ctor field-install
ordering + timed brand check.

## Gate status

No code landed → golden 280/280 and node 304/304 unchanged (baseline). Files
that a future fix WILL touch (for integration awareness):
- src/compiler/hir/ASTToHIR_Classes.cpp (synthetic ctor, private install)
- src/compiler/hir/ASTToHIR.cpp (spec-loop default-ctor generator, registration order)
- src/runtime/src/TsObject.cpp (ts_object_get_private / ts_object_set_private brand check)
- src/compiler/hir/HIRToLLVM_Closures.cpp + Monomorphizer (per-eval brand token capture)

No probe crashed during investigation.
