var __esDecorate = (this && this.__esDecorate) || function (ctor, descriptorIn, decorators, contextIn, initializers, extraInitializers) {
    function accept(f) { if (f !== void 0 && typeof f !== "function") throw new TypeError("Function expected"); return f; }
    var kind = contextIn.kind, key = kind === "getter" ? "get" : kind === "setter" ? "set" : "value";
    var target = !descriptorIn && ctor ? contextIn["static"] ? ctor : ctor.prototype : null;
    var descriptor = descriptorIn || (target ? Object.getOwnPropertyDescriptor(target, contextIn.name) : {});
    var _, done = false;
    for (var i = decorators.length - 1; i >= 0; i--) {
        var context = {};
        for (var p in contextIn) context[p] = p === "access" ? {} : contextIn[p];
        for (var p in contextIn.access) context.access[p] = contextIn.access[p];
        context.addInitializer = function (f) { if (done) throw new TypeError("Cannot add initializers after decoration has completed"); extraInitializers.push(accept(f || null)); };
        var result = (0, decorators[i])(kind === "accessor" ? { get: descriptor.get, set: descriptor.set } : descriptor[key], context);
        if (kind === "accessor") {
            if (result === void 0) continue;
            if (result === null || typeof result !== "object") throw new TypeError("Object expected");
            if (_ = accept(result.get)) descriptor.get = _;
            if (_ = accept(result.set)) descriptor.set = _;
            if (_ = accept(result.init)) initializers.unshift(_);
        }
        else if (_ = accept(result)) {
            if (kind === "field") initializers.unshift(_);
            else descriptor[key] = _;
        }
    }
    if (target) Object.defineProperty(target, contextIn.name, descriptor);
    done = true;
};
var __runInitializers = (this && this.__runInitializers) || function (thisArg, initializers, value) {
    var useValue = arguments.length > 2;
    for (var i = 0; i < initializers.length; i++) {
        value = useValue ? initializers[i].call(thisArg, value) : initializers[i].call(thisArg);
    }
    return useValue ? value : void 0;
};
var __setFunctionName = (this && this.__setFunctionName) || function (f, name, prefix) {
    if (typeof name === "symbol") name = name.description ? "[".concat(name.description, "]") : "";
    return Object.defineProperty(f, "name", { configurable: true, value: prefix ? "".concat(prefix, " ", name) : name });
};
function struct(constructor) { }
let Vector3 = (() => {
    let _classDecorators = [struct];
    let _classDescriptor;
    let _classExtraInitializers = [];
    let _classThis;
    var Vector3 = _classThis = class {
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
    };
    __setFunctionName(_classThis, "Vector3");
    (() => {
        const _metadata = typeof Symbol === "function" && Symbol.metadata ? Object.create(null) : void 0;
        __esDecorate(null, _classDescriptor = { value: _classThis }, _classDecorators, { kind: "class", name: _classThis.name, metadata: _metadata }, null, _classExtraInitializers);
        Vector3 = _classThis = _classDescriptor.value;
        if (_metadata) Object.defineProperty(_classThis, Symbol.metadata, { enumerable: true, configurable: true, writable: true, value: _metadata });
        __runInitializers(_classThis, _classExtraInitializers);
    })();
    return Vector3 = _classThis;
})();
let Ray = (() => {
    let _classDecorators = [struct];
    let _classDescriptor;
    let _classExtraInitializers = [];
    let _classThis;
    var Ray = _classThis = class {
        constructor(origin, direction) {
            this.origin = origin;
            this.direction = direction;
        }
    };
    __setFunctionName(_classThis, "Ray");
    (() => {
        const _metadata = typeof Symbol === "function" && Symbol.metadata ? Object.create(null) : void 0;
        __esDecorate(null, _classDescriptor = { value: _classThis }, _classDecorators, { kind: "class", name: _classThis.name, metadata: _metadata }, null, _classExtraInitializers);
        Ray = _classThis = _classDescriptor.value;
        if (_metadata) Object.defineProperty(_classThis, Symbol.metadata, { enumerable: true, configurable: true, writable: true, value: _metadata });
        __runInitializers(_classThis, _classExtraInitializers);
    })();
    return Ray = _classThis;
})();
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
