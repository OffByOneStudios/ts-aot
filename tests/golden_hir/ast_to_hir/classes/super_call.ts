// Test: Super calls in inheritance
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Class declarations come first
// HIR-CHECK: class @Animal
// HIR-CHECK: class @Dog extends @Animal

// Then constructor definitions
// HIR-CHECK: define @Animal_constructor
// HIR-CHECK: set_prop.static {{.*}}, "name"
// HIR-CHECK: ret

// HIR-CHECK: define @Dog_constructor
// HIR-CHECK: set_prop.static {{.*}}, "name"
// HIR-CHECK: set_prop.static {{.*}}, "breed"
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

// OUTPUT: Buddy
// OUTPUT: Labrador

class Animal {
  name: string;

  constructor(name: string) {
    this.name = name;
  }
}

class Dog extends Animal {
  breed: string;

  constructor(name: string, breed: string) {
    super(name);
    this.breed = breed;
  }
}

function user_main(): number {
  const dog = new Dog("Buddy", "Labrador");
  console.log(dog.name);
  console.log(dog.breed);
  return 0;
}
