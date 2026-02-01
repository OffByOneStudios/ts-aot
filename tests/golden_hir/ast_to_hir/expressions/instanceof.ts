// Test: instanceof operator for type checking
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// instanceof uses runtime check
// HIR-CHECK: instanceof
// HIR-CHECK: ret

// OUTPUT: true
// OUTPUT: true
// OUTPUT: false

class Animal {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
}

class Dog extends Animal {
  bark(): void {
    console.log("woof");
  }
}

function user_main(): number {
  const dog = new Dog("Rex");
  const animal = new Animal("Generic");

  // Dog instanceof Dog = true
  console.log(dog instanceof Dog);

  // Dog instanceof Animal = true (inheritance)
  console.log(dog instanceof Animal);

  // Animal instanceof Dog = false
  console.log(animal instanceof Dog);

  return 0;
}
