// Test: For loop generates correct HIR control flow
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: for.cond{{.*}}:
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: for.body{{.*}}:
// HIR-CHECK: for.update{{.*}}:
// HIR-CHECK: add.f64
// HIR-CHECK: br
// HIR-CHECK: for.end{{.*}}:
// HIR-CHECK: ret

// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2

function user_main(): number {
  for (let i: number = 0; i < 3; i = i + 1) {
    console.log(i);
  }
  return 0;
}
