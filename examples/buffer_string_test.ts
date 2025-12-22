const buf = Buffer.alloc(10);
console.log(buf.length);

const buf2 = Buffer.from("Hello");
console.log(buf2.toString());

const s = "a,b,c";
const parts = s.split(",");
console.log(parts.length);
console.log(parts[0]);

const s2 = "a1b2c";
const parts2 = s2.split(/[0-9]/);
console.log(parts2.length);
console.log(parts2[1]);
