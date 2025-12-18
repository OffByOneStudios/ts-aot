console.log("Start");

setTimeout(() => {
    console.log("Timeout 1 (100ms)");
}, 100);

setTimeout(() => {
    console.log("Timeout 2 (50ms)");
}, 50);

let count = 0;
const interval = setInterval(() => {
    count++;
    console.log("Interval " + count);
    if (count === 3) {
        clearInterval(interval);
        console.log("Interval cleared");
    }
}, 30);

console.log("End of script");
