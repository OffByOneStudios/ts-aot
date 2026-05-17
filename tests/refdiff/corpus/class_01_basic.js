class Point {
  constructor(x, y) { this.x = x; this.y = y; }
  add(p) { return new Point(this.x + p.x, this.y + p.y); }
  toString() { return "(" + this.x + "," + this.y + ")"; }
}

var a = new Point(1, 2);
var b = new Point(3, 4);
var c = a.add(b);
console.log(c.toString());
console.log("a.x:", a.x, "a.y:", a.y);
console.log("c instanceof Point:", c instanceof Point);
