// Test: Default parameters generate conditional initialization
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @greet
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "greet"
// HIR-CHECK: ret

// OUTPUT: Hello, Alice!
// OUTPUT: Hello, World!

function greet(name: string = "World"): void {
  console.log("Hello, " + name + "!");
}

function user_main(): number {
  greet("Alice");
  greet();
  return 0;
}
