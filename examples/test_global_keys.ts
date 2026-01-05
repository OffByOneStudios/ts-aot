console.log("Keys in global:");
const keys = Object.keys(global);
for (let i = 0; i < keys.length; i++) {
    console.log(i + ": " + keys[i]);
}
