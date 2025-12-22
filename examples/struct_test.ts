
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
