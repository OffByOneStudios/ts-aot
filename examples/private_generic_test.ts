class Box<T> {
    #value: T;
    constructor(v: T) {
        this.#value = v;
    }
    getValue(): T {
        return this.#value;
    }
}

const b1 = new Box<number>(42);
console.log("Box<number>:", b1.getValue());

const b2 = new Box<string>("hello");
console.log("Box<string>:", b2.getValue());
