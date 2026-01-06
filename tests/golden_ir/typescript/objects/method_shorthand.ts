// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Method shorthand syntax in object literals
// CHECK: define
// CHECK: ts_map_create
// CHECK: ts_value_make_function
// OUTPUT: 42

const obj = {
    getValue() {
        return 42;
    }
};
console.log(obj.getValue());
