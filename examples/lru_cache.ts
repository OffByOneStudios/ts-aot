/**
 * LRU Cache Benchmark for ts-aot
 * Tests: Map, Doubly Linked List (Objects), and GC.
 */

class ListNode {
    key: string;
    value: number;
    prev: ListNode | null = null;
    next: ListNode | null = null;

    constructor(key: string, value: number) {
        this.key = key;
        this.value = value;
    }
}

class LRUCache {
    capacity: number;
    map: Map<string, ListNode>;
    head: any = null;
    tail: any = null;

    constructor(capacity: number) {
        this.capacity = capacity;
        this.map = new Map<string, ListNode>();
    }

    get(key: string): number {
        const node: any = this.map.get(key);
        if (node) {
            this.moveToHead(node);
            return node.value;
        }
        return -1;
    }

    put(key: string, value: number): void {
        let node: any = this.map.get(key);
        if (node) {
            node.value = value;
            // this.moveToHead(node);
        } else {
            node = new ListNode(key, value);
            this.map.set(key, node);
            // this.addToHead(node);

            if (this.map.size > this.capacity) {
                const removed: any = this.removeTail();
                if (removed) {
                    this.map.delete(removed.key);
                }
            }
        }
    }

    private addToHead(node: ListNode): void {
        node.next = this.head;
        node.prev = null;
        if (this.head) {
            this.head.prev = node;
        }
        this.head = node;
        if (!this.tail) {
            this.tail = node;
        }
    }

    private removeNode(node: ListNode): void {
        if (node.prev) {
            node.prev.next = node.next;
        } else {
            this.head = node.next;
        }

        if (node.next) {
            node.next.prev = node.prev;
        } else {
            this.tail = node.prev;
        }
    }

    private moveToHead(node: ListNode): void {
        this.removeNode(node);
        this.addToHead(node);
    }

    private removeTail(): ListNode | null {
        const node = this.tail;
        if (node) {
            this.removeNode(node);
        }
        return node;
    }
}

function runBenchmark() {
    const capacity = 1000;
    const iterations = 1000000;
    const cache = new LRUCache(capacity);

    const start = Date.now();

    for (let i = 0; i < iterations; i++) {
        const key = "key_" + (i % (capacity * 2));
        if (i % 3 === 0) {
            cache.put(key, i);
        } else {
            cache.get(key);
        }
    }

    const end = Date.now();
    console.log("LRU Cache Benchmark: " + (end - start) + "ms");
}

runBenchmark();
