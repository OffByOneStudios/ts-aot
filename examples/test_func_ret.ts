// Simpler test without IIFE - just a function expression called
function wrapper() {
    function double(x: any) { return x * 2; }
    console.log("double(5):", double(5));
    const obj = { double };
    console.log("typeof obj.double:", typeof obj.double);
    return obj;
}

const obj = wrapper();
console.log("obj.double(7):", obj.double(7));
