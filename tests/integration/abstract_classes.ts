abstract class Shape {
    abstract getArea(): number;
    
    printArea(): void {
        console.log(this.getArea());
    }
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

class Circle extends Shape {
    radius: number;
    
    constructor(radius: number) {
        super();
        this.radius = radius;
    }
    
    getArea(): number {
        return 3 * this.radius * this.radius; // Simplified PI=3
    }
}

function main() {
    const s = new Square(5);
    s.printArea(); // 25
    
    const c = new Circle(3);
    c.printArea(); // 27
}
