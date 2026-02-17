// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: processing: enabled
// OUTPUT: processing: disabled
// OUTPUT: done

// Test: Boolean captured by a callback passed to another function
function processItem(label: string, callback: () => string): void {
    console.log("processing: " + callback());
}

function makeProcessor(enabled: boolean): () => string {
    return (): string => {
        if (enabled === true) {
            return "enabled";
        }
        return "disabled";
    };
}

processItem("item1", makeProcessor(true));
processItem("item2", makeProcessor(false));
console.log("done");
