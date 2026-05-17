// Accessor descriptor has {get, set, enumerable, configurable} — NOT
// {value, writable}.

var o = { get v() { return 1; }, set v(x) {} };
var d = Object.getOwnPropertyDescriptor(o, "v");

if (d && typeof d.get === "function" && typeof d.set === "function"
    && !("value" in d) && !("writable" in d)
    && d.enumerable === true && d.configurable === true) {
  console.log("PASS");
} else {
  var got = d ? Object.keys(d).join(",") : "no descriptor";
  console.log("FAIL: accessor descriptor shape wrong; keys=" + got);
}
