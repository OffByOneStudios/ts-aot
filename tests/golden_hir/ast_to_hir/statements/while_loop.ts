// Test: While loop generates correct HIR control flow
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: while.cond{{.*}}:
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: while.body{{.*}}:
// HIR-CHECK: add.f64
// HIR-CHECK: br
// HIR-CHECK: ret

// OUTPUT: 10

function user_main(): number {
  let i: number = 0;

  while (i < 10) {
    i = i + 1;
  }

  console.log(i);
  return 0;
}
