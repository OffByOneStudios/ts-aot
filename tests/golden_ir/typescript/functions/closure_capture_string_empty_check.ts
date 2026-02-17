// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: hello
// OUTPUT: default
// OUTPUT: world
// OUTPUT: default

// Test: String captured by closure, checked for empty vs non-empty
function makeGreeter(name: string): () => string {
    return (): string => {
        if (name !== '') {
            return name;
        }
        return "default";
    };
}

console.log(makeGreeter("hello")());
console.log(makeGreeter("")());
console.log(makeGreeter("world")());
console.log(makeGreeter("")());
