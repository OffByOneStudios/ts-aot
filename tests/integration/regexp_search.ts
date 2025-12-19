const s = "hello world";
console.log("Search 'world':");
console.log(s.search(/world/));

console.log("Search 'notfound':");
console.log(s.search(/notfound/));

const s2 = "abc 123 def";
console.log("Search digit:");
console.log(s2.search(/\d+/));
