class Point {
    x: number;
    y: number;
    constructor(x: number, y: number) {
        this.x = x;
        this.y = y;
    }
    draw() {
        console.log(this.x, this.y);
    }
}

function doDraw(p: Point) {
    p.draw();
}

const p = new Point(1, 2);
doDraw(p);
