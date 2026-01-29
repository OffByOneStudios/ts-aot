// Test: Do-while loop generates correct HIR control flow
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Note: do-while currently uses while.* block naming
// HIR-CHECK: while.cond{{.*}}:
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: while.body{{.*}}:
// HIR-CHECK: add.f64
// HIR-CHECK: while.end{{.*}}:
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
