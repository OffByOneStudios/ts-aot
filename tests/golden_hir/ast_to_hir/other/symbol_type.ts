// Test: Symbol primitive type
// XFAIL: Symbol comparison outputs wrong result in HIR pipeline
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Symbol() creates unique symbol
// HIR-CHECK: call "ts_symbol_create"
// HIR-CHECK: ret

// OUTPUT: symbol
// OUTPUT: false

function user_main(): number {
  // Create unique symbols
  const sym1 = Symbol("key");
  const sym2 = Symbol("key");

  // typeof returns "symbol"
  console.log(typeof sym1);

  // Each symbol is unique
  console.log(sym1 === sym2);

  return 0;
}
