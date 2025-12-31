// Test clone function with return value

function clone(obj: any): any {
    const result: any = {};
    Object.assign(result, obj);
    return result;
}

const source = { name: "Bob", age: 30 };
console.log("Source created");
console.log("Source name:", source.name);

const cloned = clone(source);
console.log("\nClone returned");
console.log("Cloned typeof:", typeof cloned);
console.log("Cloned keys:", Object.keys(cloned));
console.log("Cloned['name']:", cloned["name"]);
console.log("Cloned.name:", cloned.name);
