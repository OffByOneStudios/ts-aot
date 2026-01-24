// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Decorating method: greet
// OUTPUT: Descriptor has value: true
// OUTPUT: Hello, World!
// OUTPUT: All tests passed!

// Test: Basic method decorator
// A method decorator receives (target, propertyKey, descriptor)
// For AOT, we support observation but not method replacement

function logged(target: any, propertyKey: string, descriptor: PropertyDescriptor): void {
    console.log("Decorating method: " + propertyKey);

    // Check that we receive a valid descriptor
    if (descriptor && descriptor.value !== undefined) {
        console.log("Descriptor has value: true");
    }
}

class Greeter {
    @logged
    greet(): void {
        console.log("Hello, World!");
    }
}

function user_main(): number {
    const g = new Greeter();
    g.greet();

    console.log("All tests passed!");
    return 0;
}
