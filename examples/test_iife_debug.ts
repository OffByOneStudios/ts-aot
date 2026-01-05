// Debug: what does 'double' look like?
const obj: any = (function() {
    function double(x: any) { 
        console.log("double called with:", x);
        return x * 2; 
    }
    console.log("typeof double:", typeof double);
    console.log("double directly:", double(5));
    return { double };
})();

console.log("typeof obj:", typeof obj);
console.log("typeof obj.double:", typeof obj.double);
const result = obj.double(5);
console.log("obj.double(5):", result);
