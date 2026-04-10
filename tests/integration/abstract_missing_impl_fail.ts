abstract class Shape {
    abstract getArea(): number;
}

class Square extends Shape {
    side: number;
    constructor(side: number) {
        super();
        this.side = side;
    }
    getArea(): number {
        return this.side * this.side;
    }
}

function user_main(): number {
    const s = new Square(5);
    console.log("Square area: " + s.getArea());
    return s.getArea() === 25 ? 0 : 1;
}
