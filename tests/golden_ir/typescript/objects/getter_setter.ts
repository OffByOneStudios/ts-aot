// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Getter/setter properties not implemented
// CHECK: define {{.*}} @user_main
// OUTPUT: 10
// OUTPUT: 20

function user_main(): number {
    const obj = {
        _value: 10,
        get value() { return this._value; },
        set value(v: number) { this._value = v; }
    };
    console.log(obj.value);
    obj.value = 20;
    console.log(obj.value);
    return 0;
}
