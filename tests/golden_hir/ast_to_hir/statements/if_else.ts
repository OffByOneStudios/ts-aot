// Test: If-else generates correct HIR control flow
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: condbr
// HIR-CHECK: if.then{{.*}}:
// HIR-CHECK: if.else{{.*}}:
// HIR-CHECK: ret

// OUTPUT: greater

function user_main(): number {
  const x: number = 10;

  if (x > 5) {
    console.log("greater");
  } else {
    console.log("smaller");
  }

  return 0;
}
