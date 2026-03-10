// Test: Generator functions generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// user_main calls countUp and iterates
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "countUp"
// HIR-CHECK: call_method {{.*}}, "next"
// HIR-CHECK: ret

// Generator function with yield
// HIR-CHECK: define generator @countUp() -> class(Generator)
// HIR-CHECK: yield
// HIR-CHECK: yield
// HIR-CHECK: yield
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function* countUp(): Generator<number> {
  yield 1;
  yield 2;
  yield 3;
}

function user_main(): number {
  const gen = countUp();

  let result = gen.next();
  while (!result.done) {
    console.log(result.value);
    result = gen.next();
  }

  return 0;
}
