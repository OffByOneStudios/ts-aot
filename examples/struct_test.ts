
function struct(constructor: Function) {}

@struct
class Vector3 {
    x: number;
    y: number;
    z: number;

    constructor(x: number, y: number, z: number) {
        this.x = x;
        this.y = y;
        this.z = z;
    }
}

function printX(v: Vector3) {
    console.log(v.x);
    v.x = 100;
}

function main() {
    const arr = [new Vector3(1, 2, 3)];
    const v = arr[0];
    console.log(v.x);
}

main();

// TYPE-CHECK: L11:C9 AssignmentExpression -> double
// TYPE-CHECK: L11:C9 PropertyAccessExpression -> double
// TYPE-CHECK: L18:C5 CallExpression -> void
// TYPE-CHECK: L18:C17 PropertyAccessExpression -> double
// TYPE-CHECK: L23:C17 ArrayLiteralExpression -> [Vector3]
// TYPE-CHECK: L23:C18 NewExpression -> Vector3
// TYPE-CHECK: L24:C15 ElementAccessExpression -> Vector3
// TYPE-CHECK: L25:C17 PropertyAccessExpression -> double
