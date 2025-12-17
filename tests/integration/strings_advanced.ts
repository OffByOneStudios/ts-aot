const s = "Hello, World!";
console.log(s.length);
console.log(s.charCodeAt(0));
console.log(s.charCodeAt(7));

const parts = s.split(", ");
console.log(parts.length);
console.log(parts[0]);
console.log(parts[1]);

const trimmed = "  hello  ".trim();
console.log(trimmed);
console.log(trimmed.length);

const sub = s.substring(7, 12);
console.log(sub);

const sub2 = s.substring(7);
console.log(sub2);
