// Test ES2022 private class fields (#)

class Counter {
    #count: number = 0;

    increment(): void {
        this.#count++;
    }

    decrement(): void {
        this.#count--;
    }

    getCount(): number {
        return this.#count;
    }

    reset(): void {
        this.#count = 0;
    }
}

class BankAccount {
    #balance: number;
    #owner: string;

    constructor(owner: string, initialBalance: number) {
        this.#owner = owner;
        this.#balance = initialBalance;
    }

    deposit(amount: number): void {
        this.#balance += amount;
    }

    withdraw(amount: number): boolean {
        if (amount <= this.#balance) {
            this.#balance -= amount;
            return true;
        }
        return false;
    }

    getBalance(): number {
        return this.#balance;
    }

    getOwner(): string {
        return this.#owner;
    }
}

// Test Counter
const counter = new Counter();
counter.increment();
counter.increment();
counter.increment();
console.log(counter.getCount());  // 3

counter.decrement();
console.log(counter.getCount());  // 2

counter.reset();
console.log(counter.getCount());  // 0

// Test BankAccount
const account = new BankAccount("Alice", 100);
console.log(account.getOwner());     // Alice
console.log(account.getBalance());   // 100

account.deposit(50);
console.log(account.getBalance());   // 150

const success1 = account.withdraw(30);
console.log(success1);               // true
console.log(account.getBalance());   // 120

const success2 = account.withdraw(200);
console.log(success2);               // false
console.log(account.getBalance());   // 120

function user_main(): number {
    return 0;
}
