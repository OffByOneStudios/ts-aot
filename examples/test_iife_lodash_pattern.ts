// Test lodash's IIFE pattern: var _ = (function() { ... return lodash; })();

var lodash = (function() {
    function add(a: number, b: number): number {
        return a + b;
    }
    
    function greet(name: string): string {
        return "Hello, " + name;
    }
    
    return {
        add: add,
        greet: greet
    };
})();

console.log("Testing lodash IIFE pattern...");
const sum = lodash.add(10, 32);
console.log("add(10, 32) =", sum);
const msg = lodash.greet("World");
console.log("greet:", msg);
console.log("Done!");
