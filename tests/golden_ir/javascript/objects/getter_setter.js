// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Getter and setter in object literals (JS slow path)
// CHECK: define
// OUTPUT: Alice
// OUTPUT: Bob
// OUTPUT: 3
// NOTE: The two bare `counter.count;` statements run the getter (observable
//       side effect), and getter self-mutation of `this._count` persists.

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
