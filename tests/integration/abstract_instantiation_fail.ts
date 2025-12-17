abstract class Shape {
    abstract getArea(): number;
}

function main() {
    const s = new Shape(); // Should fail
}
