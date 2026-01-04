// Test ternary with null comparison and property access
function test(x: any) {
    const result = x == null ? "null path" : "non-null path: " + x.foo;
    console.log(result);
}

test(null);
test(undefined);
test({ foo: "bar" });
