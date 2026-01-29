// Test: Constructor methods generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Class with constructor parameters and property initialization
// HIR-CHECK: define @Rectangle_constructor
// HIR-CHECK: set_prop.static {{.*}}, "width"
// HIR-CHECK: set_prop.static {{.*}}, "height"
// HIR-CHECK: ret

// Constructor with explicit parameter
// HIR-CHECK: define @Circle_constructor
// HIR-CHECK: set_prop.static {{.*}}, "radius"
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: call "Rectangle_constructor"
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: call "Circle_constructor"
// HIR-CHECK: ret

// OUTPUT: 50
// OUTPUT: 78.5

class Rectangle {
  width: number;
  height: number;

  constructor(width: number, height: number) {
    this.width = width;
    this.height = height;
  }

  area(): number {
    return this.width * this.height;
  }
}

class Circle {
  radius: number;

  constructor(radius: number) {
    this.radius = radius;
  }

  area(): number {
    return 3.14 * this.radius * this.radius;
  }
}

function user_main(): number {
  const rect = new Rectangle(5, 10);
  console.log(rect.area());

  const circle = new Circle(5);
  console.log(circle.area());

  return 0;
}
