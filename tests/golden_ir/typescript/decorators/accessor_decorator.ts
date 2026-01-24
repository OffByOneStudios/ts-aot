// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Decorating accessor: name
// OUTPUT: Descriptor has get: true
// OUTPUT: Setting name to Alice
// OUTPUT: Getting name
// OUTPUT: Name: Alice
// OUTPUT: All tests passed!

// Test: Basic accessor decorator
// An accessor decorator receives (target, propertyKey, descriptor)
// For getters/setters, the descriptor has get/set instead of value

function logged(target: any, propertyKey: string, descriptor: PropertyDescriptor): void {
    console.log("Decorating accessor: " + propertyKey);

    // Check that we receive a valid descriptor with get or set
    if (descriptor && (descriptor.get !== undefined || descriptor.set !== undefined)) {
        console.log("Descriptor has get: " + (descriptor.get !== undefined));
    }
}

class Person {
    private _name: string = "";

    @logged
    get name(): string {
        console.log("Getting name");
        return this._name;
    }

    set name(value: string) {
        console.log("Setting name to " + value);
        this._name = value;
    }
}

function user_main(): number {
    const p = new Person();
    p.name = "Alice";
    console.log("Name: " + p.name);

    console.log("All tests passed!");
    return 0;
}
