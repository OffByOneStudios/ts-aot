// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Decorating property: message
// OUTPUT: Decorating property: count
// OUTPUT: Hello from MyClass
// OUTPUT: Count: 42
// OUTPUT: All tests passed!

// Test: Basic property decorator
// A property decorator receives (target, propertyKey) - no descriptor

function observable(target: any, propertyKey: string): void {
    console.log("Decorating property: " + propertyKey);
}

class MyClass {
    @observable
    message: string = "Hello from MyClass";

    @observable
    count: number = 42;
}

function user_main(): number {
    const obj = new MyClass();
    console.log(obj.message);
    console.log("Count: " + obj.count);

    console.log("All tests passed!");
    return 0;
}
