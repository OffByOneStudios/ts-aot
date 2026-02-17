// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// CHECK: @ts_cell_set
// OUTPUT: false
// OUTPUT: true
// OUTPUT: false
// OUTPUT: true

// Test: Mutable boolean captured by closure, toggled and read back
function makeToggle(): { get: () => boolean; toggle: () => void } {
    let state: boolean = false;

    const get = (): boolean => {
        return state;
    };

    const toggle = (): void => {
        if (state === true) {
            state = false;
        } else {
            state = true;
        }
    };

    return { get: get, toggle: toggle };
}

const t = makeToggle();
console.log(t.get());   // false
t.toggle();
console.log(t.get());   // true
t.toggle();
console.log(t.get());   // false
t.toggle();
console.log(t.get());   // true
