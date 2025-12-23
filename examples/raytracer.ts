function struct(constructor: Function) {}

@struct
class Vector3 {
    constructor(public x: number, public y: number, public z: number) {}

    static add(a: Vector3, b: Vector3): Vector3 {
        return new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    static sub(a: Vector3, b: Vector3): Vector3 {
        return new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    static mul(a: Vector3, b: number): Vector3 {
        return new Vector3(a.x * b, a.y * b, a.z * b);
    }

    static dot(a: Vector3, b: Vector3): number {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static cross(a: Vector3, b: Vector3): Vector3 {
        return new Vector3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }

    static normalize(a: Vector3): Vector3 {
        const len = Math.sqrt(Vector3.dot(a, a));
        return Vector3.mul(a, 1 / len);
    }
}

@struct
class Ray {
    constructor(public origin: Vector3, public direction: Vector3) {}
}

class Sphere {
    constructor(public center: Vector3, public radius: number, public color: Vector3) {}

    intersect(ray: Ray): number {
        const oc = Vector3.sub(ray.origin, this.center);
        const a = Vector3.dot(ray.direction, ray.direction);
        const b = 2 * Vector3.dot(oc, ray.direction);
        const c = Vector3.dot(oc, oc) - this.radius * this.radius;
        const discriminant = b * b - 4 * a * c;

        if (discriminant < 0) {
            return -1;
        } else {
            return (-b - Math.sqrt(discriminant)) / (2 * a);
        }
    }
}

function render() {
    const width = 200;
    const height = 100;
    const spheres: Sphere[] = [
        new Sphere(new Vector3(0, 0, -5), 1, new Vector3(1, 0, 0)),
        new Sphere(new Vector3(2, 0, -5), 1, new Vector3(0, 1, 0)),
        new Sphere(new Vector3(-2, 0, -5), 1, new Vector3(0, 0, 1)),
        new Sphere(new Vector3(0, -1001, -5), 1000, new Vector3(0.5, 0.5, 0.5))
    ];

    const origin = new Vector3(0, 0, 0);
    let checksum = 0;

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const u = (x - width / 2) / width;
            const v = (y - height / 2) / height;
            const dir = Vector3.normalize(new Vector3(u, v, -1));
            const ray = new Ray(origin, dir);

            let closestT = 1000000;
            let hitSphere: Sphere | null = null;

            for (let i = 0; i < spheres.length; i++) {
                const t = spheres[i].intersect(ray);
                if (t > 0 && t < closestT) {
                    closestT = t;
                    hitSphere = spheres[i];
                }
            }

            if (hitSphere) {
                checksum += hitSphere.color.x + hitSphere.color.y + hitSphere.color.z;
            }
        }
    }
    return checksum;
}

function main() {
    const iterations = 100;
    console.log("Starting Ray Tracer Benchmark...");
    
    // Warmup
    for (let i = 0; i < 2; i++) {
        render();
    }

    const start = Date.now();
    let totalChecksum = 0;
    for (let i = 0; i < iterations; i++) {
        totalChecksum += render();
    }
    const end = Date.now();

    console.log("Benchmark Complete.");
    console.log("Total Checksum: " + totalChecksum);
    console.log("Average Time: " + (end - start) / iterations + "ms");
}

main();

// TYPE-CHECK: L32:C31 CallExpression -> double
// TYPE-CHECK: L33:C16 CallExpression -> Vector3
// TYPE-CHECK: L46:C20 CallExpression -> Vector3
// TYPE-CHECK: L47:C19 CallExpression -> double
// TYPE-CHECK: L77:C25 CallExpression -> Vector3
// TYPE-CHECK: L78:C25 NewExpression -> Ray
// TYPE-CHECK: L84:C27 CallExpression -> double
// TYPE-CHECK: L92:C29 BinaryExpression -> double
