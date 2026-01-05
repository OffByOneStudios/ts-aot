// Test global object access
const checkGlobal = typeof global === 'object' && global;
const checkThis = Function('return this')();

console.log("global check:", checkGlobal);
console.log("this check:", checkThis);

if (checkGlobal) {
    console.log("Has global!");
    (checkGlobal as any).test = "success";
    console.log("Set global.test:", (checkGlobal as any).test);
} else {
    console.log("NO GLOBAL - would crash!");
}
