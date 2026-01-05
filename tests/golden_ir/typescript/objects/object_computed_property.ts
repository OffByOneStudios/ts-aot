// Test: Object computed property access
const obj: any = { x: 10, y: 20 };
const key = "x";
console.log(obj[key]);
const key2 = "y";
console.log(obj[key2]);

// CHECK: ts_map_create
// CHECK: ts_object_get_dynamic

// OUTPUT: 10
// OUTPUT: 20
