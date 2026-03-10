// Test: Async functions and await generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// user_main is async, calls fetchData and awaits
// HIR-CHECK: define async @user_main() -> class(Promise)
// HIR-CHECK: call "fetchData"
// HIR-CHECK: await
// HIR-CHECK: ret

// fetchData is async, returns a constant
// HIR-CHECK: define async @fetchData() -> class(Promise)
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
