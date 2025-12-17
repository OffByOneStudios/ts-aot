class Animal {
    name: string;
    constructor(name: string) {
        this.name = name;
    }
}

class Dog extends Animal {
    bark() {
        ts_console_log("woof");
    }
}

class Cat extends Animal {
    meow() {
        ts_console_log("meow");
    }
}

function identify(a: Animal) {
    if (a instanceof Dog) {
        a.bark();
    } else if (a instanceof Cat) {
        a.meow();
    } else {
        ts_console_log("unknown animal");
    }
}

function main() {
    let d = new Dog("Fido");
    let c = new Cat("Whiskers");
    
    identify(d);
    identify(c);
}
