// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Creating decorator with prefix: [LOG]
// OUTPUT: [LOG] Decorating class: MyService
// OUTPUT: Service created
// OUTPUT: All tests passed!

// Test: Decorator factory pattern

function logWithPrefix(prefix: string): (target: any) => any {
    console.log("Creating decorator with prefix: " + prefix);
    return function(target: any): any {
        console.log(prefix + " Decorating class: " + target.name);
        return target;
    };
}

@logWithPrefix("[LOG]")
class MyService {
    constructor() {
        console.log("Service created");
    }
}

function user_main(): number {
    const svc = new MyService();
    console.log("All tests passed!");
    return 0;
}
