// Test: Object property assignment
const obj: any = {};
obj.name = "Test";
obj.value = 42;
console.log(obj.name);
console.log(obj.value);

// CHECK: ts_map_create
// CHECK: ts_object_set_dynamic
// CHECK: ts_object_get_dynamic

// OUTPUT: Test
// OUTPUT: 42
