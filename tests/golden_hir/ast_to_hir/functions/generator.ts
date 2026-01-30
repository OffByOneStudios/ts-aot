// Test: Generator functions generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// XFAIL: HIR pipeline lacks generator state machine transformation (generators run to completion instead of suspending at yield points)

// HIR-CHECK: define @countUp
// HIR-CHECK: yield
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call
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
