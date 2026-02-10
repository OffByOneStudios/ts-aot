// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Alice
// OUTPUT: Bob
// OUTPUT: 2

interface Item {
    name: string;
    value: number;
}

class Container {
    private items: Item[] = [];

    add(name: string, value: number): void {
        this.items.push({ name: name, value: value });
    }

    printNames(): void {
        for (const item of this.items) {
            console.log(item.name);
        }
    }

    count(): number {
        return this.items.length;
    }
}

function user_main(): number {
    const c = new Container();
    c.add("Alice", 1);
    c.add("Bob", 2);
    c.printNames();
    console.log(c.count());
    return 0;
}
