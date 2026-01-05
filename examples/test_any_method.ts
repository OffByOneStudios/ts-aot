// Test method call on any-typed object
const obj: any = {
    double: function(x: any) { return x * 2; }
};

const result = obj.double(5);
console.log("obj.double(5):", result);  // Should be 10
