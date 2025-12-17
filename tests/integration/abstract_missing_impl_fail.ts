abstract class Shape {
    abstract getArea(): number;
}

class Square extends Shape {
    // Missing getArea
}

function main() {
    const s = new Square();
}
