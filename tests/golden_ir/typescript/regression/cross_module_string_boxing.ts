// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: first
// OUTPUT: second
// OUTPUT: 2

// Tests that string properties survive cross-module function calls
// without double-boxing. When a function is too complex to inline,
// any-typed strings from property access get passed to string-typed
// parameters, which must not double-box via ts_value_make_string.

import { Result, makeResult } from './cross_module_string_lib';

class Suite {
    private items: Array<{ name: string; fn: () => void }> = [];
    private results: Result[] = [];

    add(name: string, fn: () => void): void {
        this.items.push({ name: name, fn: fn });
    }

    run(): void {
        this.results = [];
        for (const item of this.items) {
            const result = makeResult(item.name);
            this.results.push(result);
        }
    }

    printNames(): void {
        for (const r of this.results) {
            console.log(r.name);
        }
    }

    count(): number {
        return this.results.length;
    }
}

function user_main(): number {
    const suite = new Suite();
    suite.add('first', () => {});
    suite.add('second', () => {});
    suite.run();
    suite.printNames();
    console.log(suite.count());
    return 0;
}
