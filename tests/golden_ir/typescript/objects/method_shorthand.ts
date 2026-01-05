// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Method shorthand returns undefined instead of function result
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
