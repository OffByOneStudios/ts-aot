// Test: Class inheritance generates correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Base class constructor
// HIR-CHECK: define @Animal_constructor
// HIR-CHECK: set_prop.static {{.*}}, "name"
// HIR-CHECK: ret

// Base class method
// HIR-CHECK: define @Animal_speak
// HIR-CHECK: ret

// Derived class constructor with super call (inlined)
// HIR-CHECK: define @Dog_constructor
// HIR-CHECK: set_prop.static {{.*}}, "name"
// HIR-CHECK: set_prop.static {{.*}}, "breed"
// HIR-CHECK: ret

// Derived class overriding method
// HIR-CHECK: define @Dog_speak
// HIR-CHECK: ret

// Derived class calling super method (inlined)
// HIR-CHECK: define @Cat_constructor
// HIR-CHECK: set_prop.static {{.*}}, "name"
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object "Dog"
// HIR-CHECK: set_prop.static {{.*}}, "name"
// HIR-CHECK: set_prop.static {{.*}}, "breed"
// HIR-CHECK: new_object "Cat"
// HIR-CHECK: set_prop.static {{.*}}, "name"
// HIR-CHECK: ret

// OUTPUT: Buddy
// OUTPUT: Woof!
// OUTPUT: Golden Retriever
// OUTPUT: Whiskers
// OUTPUT: Meow!

class Animal {
  name: string;

  constructor(name: string) {
    this.name = name;
  }

  speak(): string {
    return "...";
  }
}

class Dog extends Animal {
  breed: string;

  constructor(name: string, breed: string) {
    super(name);
    this.breed = breed;
  }

  speak(): string {
    return "Woof!";
  }
}

class Cat extends Animal {
  constructor(name: string) {
    super(name);
  }

  speak(): string {
    return "Meow!";
  }
}

function user_main(): number {
  const dog = new Dog("Buddy", "Golden Retriever");
  console.log(dog.name);
  console.log(dog.speak());
  console.log(dog.breed);

  const cat = new Cat("Whiskers");
  console.log(cat.name);
  console.log(cat.speak());

  return 0;
}
