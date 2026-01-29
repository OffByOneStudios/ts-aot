// Test: Break statement generates correct HIR branch to loop exit
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64

// For loop with break
// HIR-CHECK: for.cond{{.*}}:
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: for.body{{.*}}:
// HIR-CHECK: cmp.eq.f64
// HIR-CHECK: condbr
// HIR-CHECK: if.then{{.*}}:
// break generates branch to for.end
// HIR-CHECK: br %for.end
// HIR-CHECK: for.update{{.*}}:
// HIR-CHECK: for.end{{.*}}:

// HIR-CHECK: ret

// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: done break

function user_main(): number {
  // Test break in for loop
  for (let i: number = 0; i < 10; i = i + 1) {
    if (i === 3) {
      break;
    }
    console.log(i);
  }
  console.log("done break");

  return 0;
}
