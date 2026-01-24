// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Class decorator called: MyClass
// OUTPUT: Testing class decorators...
// OUTPUT: All tests passed!

// Test: Basic class decorator

function log(target: any): any {
    console.log("Class decorator called: " + target.name);
    return target;
}

@log
class MyClass {
    value: number = 42;

    constructor() {}

    getValue(): number {
        return this.value;
    }
}

function user_main(): number {
    console.log("Testing class decorators...");

    // Note: The decorator should have been called when the class was defined

    const obj = new MyClass();
    const val = obj.getValue();

    if (val === 42) {
        console.log("All tests passed!");
    }

    return 0;
}
