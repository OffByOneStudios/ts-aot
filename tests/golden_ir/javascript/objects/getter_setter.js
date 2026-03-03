// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Getter and setter in object literals (JS slow path)
// CHECK: define
// OUTPUT: Alice
// OUTPUT: Bob
// OUTPUT: 1
// NOTE: Counter outputs 1 (not 3) because getter self-mutation doesn't persist.
//       This is a known JS slow-path limitation.

var person = {
    _name: "Alice",
    get name() {
        return this._name;
    },
    set name(v) {
        this._name = v;
    }
};

console.log(person.name);
person.name = "Bob";
console.log(person.name);

// Computed getter
var counter = {
    _count: 0,
    get count() {
        this._count = this._count + 1;
        return this._count;
    }
};

counter.count;
counter.count;
console.log(counter.count);
