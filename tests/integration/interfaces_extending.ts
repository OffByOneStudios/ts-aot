interface Named {
    name: string;
}

interface Shape extends Named {
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

function printName(n: Named) {
    ts_console_log(n.name);
}

function main() {
    const r = new Rect("ExtendedRect", 5, 10);
    printName(r);
}
