console.log("Testing process and console...");

console.log("Platform:", process.platform);
console.log("Arch:", process.arch);
console.log("CWD:", process.cwd());

process.chdir("..");
console.log("New CWD:", process.cwd());
process.chdir("ts-aoc"); // Go back

console.log("Env PATH exists:", process.env["PATH"] !== undefined);

process.nextTick(() => {
    console.log("nextTick callback");
});

setImmediate(() => {
    console.log("setImmediate callback");
});

console.time("timer");
// for (let i = 0; i < 1000000; i++) {}
console.timeEnd("timer");

console.error("This is an error message");

console.log("Done!");
