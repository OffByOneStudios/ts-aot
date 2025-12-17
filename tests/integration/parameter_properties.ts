class Point {
    constructor(
        public x: number,
        private y: number,
        public readonly label: string
    ) {}

    print() {
        console.log(this.label);
        console.log(this.x);
        console.log(this.y);
    }
}

function main() {
    const p = new Point(10, 20, "Origin");
    p.print();
    p.x = 100;
    // p.label = "New"; // Should fail
    console.log(p.x);
}
