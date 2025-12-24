class A {
    #x: number = 10;
    static #y: number = 20;

    static {
        console.log("Static block 1 in A");
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

function main() {
    console.log("Main started");
    const a = new A();
    console.log("a.getX(): " + a.getX());
    console.log("A.getY(): " + A.getY());
}

main();
