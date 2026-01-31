// Test: Optional chaining (?.) generates null checks
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Optional chaining creates null check branching
// HIR-CHECK: call "ts_value_is_nullish"
// HIR-CHECK: condbr
// HIR-CHECK: ret

// OUTPUT: safe
// OUTPUT: undefined

interface Person {
  name: string;
  address?: {
    city: string;
  };
}

function user_main(): number {
  const person: Person = { name: "Alice" };
  const city = person.address?.city;

  if (city === undefined) {
    console.log("safe");
  }
  console.log(city);
  return 0;
}
