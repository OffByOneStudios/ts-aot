// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: Added: test1
// OUTPUT: Added: test2 with options
// OUTPUT: Count: 2

// Bug: Class methods with optional parameters cause codegen error:
// "Incorrect number of arguments passed to called function"
//
// The method signature has 3 parameters (name, fn, options?) but when called
// with only 2 arguments, the IR generation fails because it doesn't handle
// the missing optional parameter.

interface Options {
    value?: number;
}

class Builder {
    private items: string[] = [];

    add(name: string, fn: () => void, options?: Options): this {
        this.items.push(name);
        fn();
        return this;
    }

    count(): number {
        return this.items.length;
    }
}

function user_main(): number {
    const builder = new Builder();

    // Call with 2 arguments (omitting optional parameter)
    builder.add("test1", () => {
        console.log("Added: test1");
    });

    // Call with 3 arguments (providing optional parameter)
    builder.add("test2", () => {
        console.log("Added: test2 with options");
    }, { value: 42 });

    console.log("Count: " + builder.count());

    return 0;
}
