// Test: Basic class generates new_object and property access
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// HIR-CHECK: class @Point
// HIR-CHECK: define @Point_constructor
// HIR-CHECK: set_prop.static
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object "Point"
// HIR-CHECK: call "Point_constructor"
// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 20

class Point {
  x: number;
  y: number;

  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }
}

function user_main(): number {
  const p = new Point(10, 20);
  console.log(p.x);
  console.log(p.y);
  return 0;
}
