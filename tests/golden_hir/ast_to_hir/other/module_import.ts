// Test: Module imports and basic path usage
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Path module methods resolve to runtime calls
// HIR-CHECK: call "ts_path
// HIR-CHECK: ret

// OUTPUT: file.txt

import * as path from 'path';

function user_main(): number {
  // Use path module methods
  const filename = path.basename("/foo/bar/file.txt");
  console.log(filename);

  return 0;
}
