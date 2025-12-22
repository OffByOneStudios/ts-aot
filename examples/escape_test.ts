
class Point {
    constructor(public x: number, public y: number) {}
}

function test() {
    const p = new Point(1, 2);
    const sum = p.x + p.y;
    ts_console_log("Sum: " + sum);
}

test();
