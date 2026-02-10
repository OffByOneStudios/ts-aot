// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 3
// OUTPUT: 1
// OUTPUT: 5000
// OUTPUT: hello
// OUTPUT: done

// Test dynamic property access on plain objects passed as any type.
// Exercises: ts_object_get_dynamic -> ts_try_virtual_dispatch_via_vbase
// which must not crash on TsMap objects (plain object literals).
// Regression test for NX fault when reinterpret_cast<TsReadable*> is used
// on non-stream GC objects.

function readOptions(options: any): void {
    if (options !== undefined) {
        if (options.iterations !== undefined) {
            console.log(options.iterations);
        }
        if (options.warmup !== undefined) {
            console.log(options.warmup);
        }
        if (options.minTime !== undefined) {
            console.log(options.minTime);
        }
        if (options.name !== undefined) {
            console.log(options.name);
        }
    }
}

function user_main(): number {
    // Plain object passed to function expecting any
    const opts = { iterations: 3, warmup: 1, minTime: 5000 };
    readOptions(opts);

    // Object stored in array then accessed dynamically
    const items: any[] = [];
    items.push({ name: "hello" });
    for (const item of items) {
        readOptions(item);
    }

    console.log("done");
    return 0;
}
