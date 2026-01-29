// Test: For-of loop generates correct HIR control flow
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_array.boxed
// HIR-CHECK: array.length
// HIR-CHECK: forof.cond{{.*}}:
// HIR-CHECK: cmp.lt.i64
// HIR-CHECK: condbr
// HIR-CHECK: forof.body{{.*}}:
// HIR-CHECK: get_elem
// HIR-CHECK: br
// HIR-CHECK: forof.end{{.*}}:
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function user_main(): number {
  const arr: number[] = [1, 2, 3];
  for (const x of arr) {
    console.log(x);
  }
  return 0;
}
