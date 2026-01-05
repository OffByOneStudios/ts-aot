// Test: Object.keys() iteration
const obj: any = { a: 1, b: 2, c: 3 };
const keys = Object.keys(obj);
console.log(keys.length);

// CHECK: ts_map_create
// CHECK: ts_object_keys

// OUTPUT: 3
