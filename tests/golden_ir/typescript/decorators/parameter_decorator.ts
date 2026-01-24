// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Decorating parameter at index 0 in greet
// OUTPUT: Decorating parameter at index 1 in greet
// OUTPUT: Hello, World!
// OUTPUT: All tests passed!

// Test: Basic parameter decorator
// A parameter decorator receives (target, propertyKey, parameterIndex)

function validate(target: any, propertyKey: string, parameterIndex: number): void {
    console.log("Decorating parameter at index " + parameterIndex + " in " + propertyKey);
}

class Greeter {
    greet(@validate name: string, @validate message: string): void {
        console.log("Hello, " + message + "!");
    }
}

function user_main(): number {
    const g = new Greeter();
    g.greet("User", "World");

    console.log("All tests passed!");
    return 0;
}
