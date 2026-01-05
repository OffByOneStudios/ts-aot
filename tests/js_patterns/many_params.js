// Test 105.11.5.12: Function with Many Parameters (Lodash uses up to 10)
// Pattern: Functions with many arguments

function fn2(a, b) { return a + b; }
function fn5(a, b, c, d, e) { return a + b + c + d + e; }
function fn10(a, b, c, d, e, f, g, h, i, j) { 
    return a + b + c + d + e + f + g + h + i + j; 
}

console.log("fn2(1,2) =", fn2(1, 2));                           // 3
console.log("fn5(1,2,3,4,5) =", fn5(1, 2, 3, 4, 5));            // 15
console.log("fn10(1..10) =", fn10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)); // 55

console.log("PASS: many_params");
