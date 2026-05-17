var s = "Hello, World!";
console.log(/World/.test(s));
console.log(/world/i.test(s));
console.log(/\d+/.test("abc 42"));

var m = "abc-def-ghi".match(/-(\w+)-/);
console.log(m[0], m[1]);

console.log("abc-def-ghi".replace(/-/g, "/"));
console.log("a1b2c3".split(/\d/).join("|"));

var named = "2023-12-31".match(/(?<year>\d+)-(?<month>\d+)-(?<day>\d+)/);
console.log(named.groups.year, named.groups.month, named.groups.day);
