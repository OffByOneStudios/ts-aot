// Test: Do-while loop generates correct HIR control flow
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: do.body{{.*}}:
// HIR-CHECK: add.f64
// HIR-CHECK: do.cond{{.*}}:
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: do.end{{.*}}:
// HIR-CHECK: ret

// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2

function user_main(): number {
  let i: number = 0;
  do {
    console.log(i);
    i = i + 1;
  } while (i < 3);
  return 0;
}
