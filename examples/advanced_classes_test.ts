namespace ts_aot {
    export declare function comptime<T>(fn: () => T): T;
}

class A {
    #x: number = 10;
    
    @ts_aot.comptime
    static #y: number = 20;

    static {
        console.log("Static block 1 in A");
        // This will overwrite the comptime value at runtime if called, 
        // but the field starts as 20 in the binary.
        A.#y = 30;
    }

    getX() {
        return this.#x;
    }

    static getY() {
        return A.#y;
    }

    static {
        console.log("Static block 2 in A");
    }
}

class Box<T> {
    #value: T;
    constructor(v: T) {
        this.#value = v;
    }
    getValue(): T {
        return this.#value;
    }
}

function main() {
    console.log("Main started");
    
    const secret = ts_aot.comptime(() => 42);
    console.log("Comptime secret: " + secret);

    const msg = ts_aot.comptime(() => "Hello from comptime!");
    console.log("Comptime msg: " + msg);

    const a = new A();
    console.log("a.getX(): " + a.getX());
    console.log("A.getY(): " + A.getY());

    const b1 = new Box<number>(123);
    const b2 = new Box<string>("Hello");
    console.log("Box<number>: " + b1.getValue());
    console.log("Box<string>: " + b2.getValue());
}

main();
