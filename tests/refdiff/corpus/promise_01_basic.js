async function f() { return 42; }

async function main() {
  var v = await f();
  console.log("v:", v);

  var r = await Promise.resolve("hello");
  console.log("r:", r);

  try {
    await Promise.reject(new Error("oops"));
  } catch (e) {
    console.log("caught:", e.message);
  }

  var all = await Promise.all([Promise.resolve(1), Promise.resolve(2), Promise.resolve(3)]);
  console.log("all:", all.join(","));
}

main();
