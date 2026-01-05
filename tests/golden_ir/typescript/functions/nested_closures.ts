// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 6

// Test nested arrow functions capturing variables from outer scopes
const x = 1;
const outer = () => {
    const y = 2;
    const inner = () => {
        const z = 3;
        return x + y + z;
    };
    return inner();
};
console.log(outer());
