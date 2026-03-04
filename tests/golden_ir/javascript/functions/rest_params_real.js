// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 3
// OUTPUT: hello-world

// Test array as function parameter (simulates rest param patterns)
function countItems(items) {
    return items.length;
}

function joinItems(items, sep) {
    return items.join(sep);
}

console.log(countItems([1, 2, 3]));
console.log(joinItems(["hello", "world"], "-"));
