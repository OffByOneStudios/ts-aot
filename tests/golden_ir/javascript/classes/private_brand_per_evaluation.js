// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: ts_private_brand_new_evaluation
// CHECK: ts_private_brand_check
// OUTPUT: same-eval: ok
// OUTPUT: cross-eval: TypeError
// OUTPUT: top-level cross-instance: ok
//
// ES2022 per-evaluation private brand (ECMA-262 15.7.14 ClassDefinitionEvaluation
// step 31): every EVALUATION of a class definition mints a fresh [[PrivateBrand]].
// Instances of two evaluations of the same class text must not cross-satisfy
// each other's instance private methods (7.3.30 PrivateGet step 5 -> TypeError).
// Instances of ONE evaluation (incl. any top-level class) still interoperate.

function createAndInstantiate() {
  class C {
    #m() { return 'ok'; }
    access(o) { return o.#m(); }
  }
  return new C();
}

var c1 = createAndInstantiate();
var c2 = createAndInstantiate();
console.log("same-eval: " + c1.access(c1));
try {
  c1.access(c2);
  console.log("cross-eval: NO THROW");
} catch (e) {
  console.log("cross-eval: " + e.constructor.name);
}

// Top-level class evaluates once: sibling instances share the brand.
class T {
  #m() { return 'ok'; }
  access(o) { return o.#m(); }
}
console.log("top-level cross-instance: " + new T().access(new T()));
