// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Getter/setter syntax not yet implemented (compiler crash)
// Test: Getter/setter properties on objects
// OUTPUT: 10
// OUTPUT: 20

const obj = {
    _value: 10,
    get value() { return this._value; },
    set value(v: number) { this._value = v; }
};
console.log(obj.value);
obj.value = 20;
console.log(obj.value);
