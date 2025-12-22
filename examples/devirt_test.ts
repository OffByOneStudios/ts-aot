class Point {
    x: number;
    constructor(x: number) {
        this.x = x;
    }
    getX(): number {
        return this.getInnerX();
    }
    getInnerX(): number {
        return this.x;
    }
}

class SubPoint extends Point {
    getX(): number {
        return this.getInnerX() + 1;
    }
    getInnerX(): number {
        return this.x + 2;
    }
}

function test(p: Point) {
    ts_console_log(p.getX());
}

function main() {
    const p = new Point(10);
    ts_console_log(p.getX()); // Should devirtualize
    
    const p2: Point = new SubPoint(20);

    ts_console_log(p2.getX()); // Should devirtualize to SubPoint::getX

    test(p); // Should devirtualize in test_Point
    test(p2); // Should devirtualize in test_SubPoint
}
