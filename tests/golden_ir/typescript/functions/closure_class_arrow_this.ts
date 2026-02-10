// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: 3
// OUTPUT: name is: Timer
// OUTPUT: 100

// Test arrow functions in class methods capturing `this`.
// Exercises: arrow function lexical `this` binding in class method scope,
// `this` captured as closure variable, property access on captured `this`.

class Accumulator {
  total: number;
  constructor() {
    this.total = 0;
  }
  addAll(numbers: number[]): void {
    numbers.forEach((n: number) => {
      this.total = this.total + n;
    });
  }
}

class Widget {
  name: string;
  value: number;
  constructor(name: string, value: number) {
    this.name = name;
    this.value = value;
  }
  getName(): string {
    const getter = () => this.name;
    return getter();
  }
  getValue(): number {
    const getter = () => this.value;
    return getter();
  }
}

function user_main(): number {
  const acc = new Accumulator();
  acc.addAll([1, 2]);
  console.log(acc.total);

  const w = new Widget("Timer", 100);
  console.log("name is: " + w.getName());
  console.log(w.getValue());

  return 0;
}
