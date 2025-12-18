class Point3D {
    x: number;
    y: number;
    z: number;
    constructor(x: number, y: number, z: number) {
        this.x = x;
        this.y = y;
        this.z = z;
    }
}

function printPoint({ x, y }: Point3D) {
    ts_console_log("printPoint x: " + x);
    ts_console_log("printPoint y: " + y);
}

function test() {
    const p = new Point3D(10, 20, 30);
    printPoint(p);

    const { x, y } = p;
    ts_console_log("x: " + x);
    ts_console_log("y: " + y);

    const arr = [1, 2, 3];
    const [a, b, c] = arr;
    ts_console_log("a: " + a);
    ts_console_log("b: " + b);
    ts_console_log("c: " + c);

    const [d, e = 42, ...rest] = [1];
    ts_console_log("d: " + d);
    ts_console_log("e: " + e);
    ts_console_log("rest length: " + rest.length);

    const { x: x2, y: y2, z = 100 } = p;
    ts_console_log("x2: " + x2);
    ts_console_log("y2: " + y2);
    ts_console_log("z: " + z);
}

test();
