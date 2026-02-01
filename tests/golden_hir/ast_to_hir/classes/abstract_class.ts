// Test: Abstract classes with abstract methods
// XFAIL: Abstract classes cause super() call resolution issues in HIR (null vtable)
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Abstract class has non-abstract method defined
// HIR-CHECK: define @Shape_describe
// Concrete implementations
// HIR-CHECK: define @Circle_getArea
// HIR-CHECK: define @Square_getArea
// Main function
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 78.5
// OUTPUT: 100

abstract class Shape {
  abstract getArea(): number;

  describe(): string {
    return "I am a shape";
  }
}

class Circle extends Shape {
  radius: number;

  constructor(radius: number) {
    super();
    this.radius = radius;
  }

  getArea(): number {
    return 3.14 * this.radius * this.radius;
  }
}

class Square extends Shape {
  side: number;

  constructor(side: number) {
    super();
    this.side = side;
  }

  getArea(): number {
    return this.side * this.side;
  }
}

function user_main(): number {
  const circle = new Circle(5);
  const square = new Square(10);

  console.log(circle.getArea());
  console.log(square.getArea());

  return 0;
}
