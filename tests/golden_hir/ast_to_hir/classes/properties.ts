// Test: Class properties generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Instance property with initializer
// IntegerOptimizationPass converts integer constants not used in f64 context
// HIR-CHECK: define @Config_constructor
// HIR-CHECK: const.i64 100
// HIR-CHECK: set_prop.static {{.*}}, "maxItems"
// HIR-CHECK: const.bool true
// HIR-CHECK: set_prop.static {{.*}}, "enabled"
// HIR-CHECK: ret

// Static property access
// HIR-CHECK: define @user_main() -> f64

// Getting instance property
// HIR-CHECK: get_prop.static {{.*}}, "maxItems"
// HIR-CHECK: get_prop.static {{.*}}, "enabled"

// Setting instance property
// HIR-CHECK: set_prop.static {{.*}}, "maxItems"

// HIR-CHECK: ret

// OUTPUT: 100
// OUTPUT: true
// OUTPUT: 200
// OUTPUT: 0

class Config {
  maxItems: number = 100;
  enabled: boolean = true;
  name: string;

  constructor(name: string) {
    this.name = name;
  }
}

class Constants {
  static VERSION: number = 42;
  static NAME: string = "MyApp";
}

function user_main(): number {
  const config = new Config("test");

  // Read instance properties
  console.log(config.maxItems);
  console.log(config.enabled);

  // Write instance property
  config.maxItems = 200;
  console.log(config.maxItems);

  // Read static property
  console.log(Constants.VERSION);

  return 0;
}
