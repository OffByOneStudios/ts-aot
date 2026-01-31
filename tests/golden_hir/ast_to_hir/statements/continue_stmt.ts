// Test: Continue statement generates correct HIR branch to loop header
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64

// While loop structure with continue
// HIR-CHECK: while.cond
// HIR-CHECK: condbr
// HIR-CHECK: while.body
// while.end block contains the return (comes before if.then in HIR)
// HIR-CHECK: while.end
// HIR-CHECK: ret
// continue generates branch back to while.cond (in if.then block)
// HIR-CHECK: br %while.cond

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
