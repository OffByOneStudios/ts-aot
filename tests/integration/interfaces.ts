interface Shape {
    name: string;
    getArea(): number;
}

class Rect implements Shape {
    name: string;
    width: number;
    height: number;

    constructor(name: string, w: number, h: number) {
        this.name = name;
        this.width = w;
        this.height = h;
    }

    getArea(): number {
        return this.width * this.height;
    }
}

function printArea(s: Shape) {
    console.log(s.name);
    console.log(s.getArea());
}

function main() {
    const r = new Rect("MyRect", 10, 20);
    printArea(r);
}
