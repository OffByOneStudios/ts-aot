// Test: Function with default parameters
function greet(name: string = "World"): void {
    console.log(name);
}

greet();
greet("Alice");

// CHECK: define

// OUTPUT: World
// OUTPUT: Alice
