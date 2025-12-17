class Point3D {
    x: number;
    y: number;
    z: number;

    constructor(x: number, y: number, z: number) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    add(other: Point3D): Point3D {
        return new Point3D(this.x + other.x, this.y + other.y, this.z + other.z);
    }
}

function main() {
    let p1 = new Point3D(10, 20, 30);
    let p2 = new Point3D(5, 5, 5);
    let p3 = p1.add(p2);
    console.log(`p3: (${p3.x}, ${p3.y}, ${p3.z})`);
}
