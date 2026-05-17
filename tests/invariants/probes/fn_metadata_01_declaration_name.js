// A named function declaration has .name = the declared name.

function helloWorld() {}

if (helloWorld.name === "helloWorld") {
  console.log("PASS");
} else {
  console.log("FAIL: function declaration .name = " + JSON.stringify(helloWorld.name));
}
