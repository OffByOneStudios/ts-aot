import { add, PI, Calculator } from "./math";

const result = add(10, 20);
console.log("10 + 20 = " + result);
console.log("PI = " + PI);

const calc = new Calculator();
console.log("Calc add: " + calc.add(5, 5));
