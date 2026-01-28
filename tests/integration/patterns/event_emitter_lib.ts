// Library for event_emitter.ts pattern test

import { EventEmitter } from 'events';

export class Counter extends EventEmitter {
    private value: number;

    constructor() {
        super();
        this.value = 0;
    }

    increment(): void {
        this.value++;
        this.emit('change', this.value);
    }

    decrement(): void {
        this.value--;
        this.emit('change', this.value);
    }

    getValue(): number {
        return this.value;
    }
}

export function createCounter(): Counter {
    return new Counter();
}
