// Test: Map and Set builtin types
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Map constructor uses runtime call
// HIR-CHECK: call "ts_map_create"
// Set constructor uses runtime call
// HIR-CHECK: call "ts_set_create"
// HIR-CHECK: ret

// OUTPUT: hello
// OUTPUT: true
// OUTPUT: true

function user_main(): number {
  // Map operations
  const map = new Map<string, string>();
  map.set("key", "hello");
  console.log(map.get("key"));

  // Set operations
  const set = new Set<number>();
  set.add(1);
  set.add(2);
  console.log(set.has(1));
  console.log(set.has(2));

  return 0;
}
