// Test: Continue statement generates correct HIR branch to loop header
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe
// XFAIL: HIR load_capture bug - uses load_capture for local variable in while loop

// HIR-CHECK: define @user_main() -> f64

// While loop with continue
// HIR-CHECK: while.cond{{.*}}:
// HIR-CHECK: condbr
// HIR-CHECK: while.body{{.*}}:
// HIR-CHECK: if.then{{.*}}:
// continue generates branch to while.cond
// HIR-CHECK: br %while.cond
// HIR-CHECK: while.end{{.*}}:

// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 3
// OUTPUT: 5
// OUTPUT: done

function user_main(): number {
  // Test continue in while loop
  let j: number = 0;
  while (j < 6) {
    j = j + 1;
    if (j % 2 === 0) {
      continue;
    }
    console.log(j);
  }
  console.log("done");

  return 0;
}
