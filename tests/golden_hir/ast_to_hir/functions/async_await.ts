// Test: Async functions and await generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define async @fetchData
// HIR-CHECK: ret

// HIR-CHECK: define async @user_main()
// HIR-CHECK: call
// HIR-CHECK: await
// HIR-CHECK: ret

// OUTPUT: 42

async function fetchData(): Promise<number> {
  return 42;
}

async function user_main(): Promise<number> {
  const result = await fetchData();
  console.log(result);
  return 0;
}
