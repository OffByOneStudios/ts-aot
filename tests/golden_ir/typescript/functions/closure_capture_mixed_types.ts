// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: Alice is 30 active=true
// OUTPUT: Bob is 25 active=false

// Test: Closure capturing boolean, number, and string simultaneously
function makePrinter(name: string, age: number, active: boolean): () => string {
    return (): string => {
        let status = "false";
        if (active === true) {
            status = "true";
        }
        return name + " is " + age + " active=" + status;
    };
}

console.log(makePrinter("Alice", 30, true)());
console.log(makePrinter("Bob", 25, false)());
