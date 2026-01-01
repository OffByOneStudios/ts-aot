// Debug rest parameters

function logArgs(...args: number[]): void {
    console.log("args.length =");
    console.log(args.length);
    
    console.log("args[0] =");
    console.log(args[0]);
}

console.log("=== Rest Parameters Debug ===");
logArgs(42);
console.log("=== Done ===");
