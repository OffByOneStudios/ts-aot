// Test: Default parameters generate conditional initialization
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main calls greet_str (monomorphized name)
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "greet_str"
// HIR-CHECK: call "greet_str"
// HIR-CHECK: ret

// greet_str checks for undefined and uses default
// HIR-CHECK: define @greet_str({{.*}}) -> void
// HIR-CHECK: call "ts_value_is_undefined"
// HIR-CHECK: condbr
// HIR-CHECK: const.string "World"
// HIR-CHECK: ret void

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
