// Compare direct vs computed property access

const obj = {
    a: 1
};

console.log("Direct: " + obj.a);

const key = "a";
const val = obj[key];
console.log("Computed: " + val);

console.log("Done");
