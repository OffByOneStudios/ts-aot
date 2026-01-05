// Test: Function with default parameters
function greet(name: string = "World"): void {
    console.log(name);
}

greet();
greet("Alice");

// CHECK: define
// CHECK: icmp eq

// OUTPUT: World
// OUTPUT: Alice
