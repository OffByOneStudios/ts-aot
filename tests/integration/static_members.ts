class MathUtils {
    static PI: number = 3.14159;
    static counter: number = 0;

    static add(a: number, b: number): number {
        MathUtils.counter = MathUtils.counter + 1;
        return a + b;
    }

    static getCounter(): number {
        return MathUtils.counter;
    }
}

function main() {
    console.log("PI: " + MathUtils.PI);
    
    const sum = MathUtils.add(10, 20);
    console.log("Sum: " + sum);
    console.log("Counter: " + MathUtils.getCounter());
    
    MathUtils.add(5, 5);
    console.log("Counter after another add: " + MathUtils.getCounter());
    
    // Test static field assignment
    MathUtils.PI = 3.14;
    console.log("Updated PI: " + MathUtils.PI);
}
