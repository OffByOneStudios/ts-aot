class C {
  x = 1;
  y = 2;
  static count = 0;
  constructor() { C.count++; }
  sum() { return this.x + this.y; }
}

var a = new C();
var b = new C();
console.log("a.sum:", a.sum());
console.log("C.count:", C.count);
console.log("keys:", Object.keys(a).sort().join(","));
