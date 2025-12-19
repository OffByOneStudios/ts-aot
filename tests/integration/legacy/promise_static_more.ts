const p1 = Promise.resolve(1);
const p2 = Promise.reject("error");

Promise.allSettled([p1, p2]).then((results) => {
    console.log("allSettled results:");
    for (let i = 0; i < results.length; i++) {
        const res = results[i];
        if (res.status === "fulfilled") {
            console.log("  fulfilled: " + res.value);
        } else {
            console.log("  rejected: " + res.reason);
        }
    }
});

const p3 = Promise.reject("fail1");
const p4 = Promise.resolve("success");
const p5 = Promise.reject("fail2");

Promise.any([p3, p4, p5]).then((val) => {
    console.log("any success: " + val);
}).catch((err) => {
    console.log("any failed: " + err);
});

Promise.any([p3, p5]).catch((err) => {
    console.log("any all failed: " + err.length + " errors");
});
