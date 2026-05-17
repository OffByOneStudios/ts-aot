// ECMA-262 NamedEvaluation: an anonymous function assigned to a binding
// inherits the binding name.

var myFn = function() {};
var myArrow = () => 0;

if (myFn.name === "myFn" && myArrow.name === "myArrow") {
  console.log("PASS");
} else {
  console.log("FAIL: NamedEvaluation broken — myFn.name="
    + JSON.stringify(myFn.name) + " myArrow.name=" + JSON.stringify(myArrow.name));
}
