try {
  throw new Error("first");
} catch (e) {
  console.log("caught:", e.message);
  console.log("name:", e.name);
  console.log("instanceof Error:", e instanceof Error);
}

try {
  null.foo;
} catch (e) {
  console.log("typeof e:", typeof e);
  console.log("is TypeError:", e instanceof TypeError);
}

function nested() {
  try {
    throw new RangeError("nested!");
  } finally {
    console.log("finally ran");
  }
}
try {
  nested();
} catch (e) {
  console.log("outer caught:", e.message);
}
