// Test: Labeled statements with break
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe
// XFAIL: HIR labeled break not properly implemented - break outer doesn't exit outer loop

// HIR-CHECK: define @user_main() -> f64

// Outer loop
// HIR-CHECK: for.cond{{.*}}:
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: for.body{{.*}}:

// Inner loop (nested)
// HIR-CHECK: for.cond{{.*}}:
// HIR-CHECK: condbr
// HIR-CHECK: for.body{{.*}}:

// Labeled break targets the outer loop's end
// HIR-CHECK: if.then{{.*}}:
// HIR-CHECK: br %for.end

// Outer loop end
// HIR-CHECK: for.end{{.*}}:
// HIR-CHECK: ret

// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: found
// OUTPUT: done

function user_main(): number {
  outer: for (let i: number = 0; i < 3; i = i + 1) {
    for (let j: number = 0; j < 3; j = j + 1) {
      console.log(j);
      if (i === 0 && j === 1) {
        console.log("found");
        break outer;
      }
    }
  }
  console.log("done");
  return 0;
}
