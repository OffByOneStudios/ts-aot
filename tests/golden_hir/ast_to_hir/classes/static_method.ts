// Test: Static methods generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Static method with no parameters
// HIR-CHECK: define @MathUtils_static_PI
// HIR-CHECK: const.f64 3.14159
// HIR-CHECK: ret

// Static method with parameters
// HIR-CHECK: define @MathUtils_static_add
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// Static method calling another static method (inlined)
// HIR-CHECK: define @MathUtils_static_double
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// Factory static method with inlined constructor
// HIR-CHECK: define @Point_static_origin
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: set_prop.static {{.*}}, "x"
// HIR-CHECK: set_prop.static {{.*}}, "y"
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 3.14159
// HIR-CHECK: call "ts_console_log"
// HIR-CHECK: add.f64
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: ret

// OUTPUT: 3.14159
// OUTPUT: 15
// OUTPUT: 20
// OUTPUT: 0
// OUTPUT: 0

class MathUtils {
  static PI(): number {
    return 3.14159;
  }

  static add(a: number, b: number): number {
    return a + b;
  }

  static double(x: number): number {
    return MathUtils.add(x, x);
  }
}

class Point {
  x: number;
  y: number;

  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }

  static origin(): Point {
    return new Point(0, 0);
  }
}

function user_main(): number {
  console.log(MathUtils.PI());
  console.log(MathUtils.add(5, 10));
  console.log(MathUtils.double(10));

  const origin = Point.origin();
  console.log(origin.x);
  console.log(origin.y);

  return 0;
}
