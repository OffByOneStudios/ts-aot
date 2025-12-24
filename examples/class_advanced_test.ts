class Advanced {
    #privateField: number = 42;
    static #staticPrivateField: string = "hello";

    static {
        console.log("Static block executed!");
        Advanced.#staticPrivateField = "world";
        console.log("Static private field set to world");
        console.log(Advanced.#staticPrivateField);
    }

    getPrivate() {
        return this.#privateField;
    }

    static getStaticPrivate() {
        return Advanced.#staticPrivateField;
    }
}

const a = new Advanced();
console.log("Private field:", a.getPrivate());
console.log("Static private field:", Advanced.getStaticPrivate());

// This should fail analysis if uncommented:
// console.log(a.#privateField);
