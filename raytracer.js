class Vector3 {
    constructor(x, y, z) {
        this.x = x;
        this.y = y;
        this.z = z;
    }
    static add(a, b) {
        return new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
    }
    static sub(a, b) {
        return new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
    }
    static mul(a, b) {
        return new Vector3(a.x * b, a.y * b, a.z * b);
    }
    static dot(a, b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static cross(a, b) {
        return new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }
    static normalize(a) {
        const len = Math.sqrt(Vector3.dot(a, a));
        return Vector3.mul(a, 1 / len);
    }
}
class Ray {
    constructor(origin, direction) {
        this.origin = origin;
        this.direction = direction;
    }
}
class Sphere {
    constructor(center, radius, color) {
        this.center = center;
        this.radius = radius;
        this.color = color;
    }
    intersect(ray) {
        const oc = Vector3.sub(ray.origin, this.center);
        const a = Vector3.dot(ray.direction, ray.direction);
        const b = 2 * Vector3.dot(oc, ray.direction);
        const c = Vector3.dot(oc, oc) - this.radius * this.radius;
        const discriminant = b * b - 4 * a * c;
        if (discriminant < 0) {
            return -1;
        }
        else {
            return (-b - Math.sqrt(discriminant)) / (2 * a);
        }
    }
}
function render() {
    const width = 200;
    const height = 100;
    const spheres = [
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
            let hitSphere = null;
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
    const iterations = 5;
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
