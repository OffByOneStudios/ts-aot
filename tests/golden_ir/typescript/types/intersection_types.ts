// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: 42
// OUTPUT: hello
// OUTPUT: 1
// OUTPUT: test
// OUTPUT: true
// OUTPUT: 999
// OUTPUT: inline

interface A {
    foo: number;
}

interface B {
    bar: string;
}

interface C {
    baz: boolean;
}

type AB = A & B;
type ABC = A & B & C;

function user_main(): number {
    // Test 1: Object with intersection type
    const obj: AB = { foo: 42, bar: "hello" };
    console.log(obj.foo);
    console.log(obj.bar);

    // Test 2: Triple intersection
    const triple: ABC = { foo: 1, bar: "test", baz: true };
    console.log(triple.foo);
    console.log(triple.bar);
    console.log(triple.baz);

    // Test 3: Inline intersection parameter
    function inlineIntersection(x: { a: number } & { b: string }): void {
        console.log(x.a);
        console.log(x.b);
    }
    inlineIntersection({ a: 999, b: "inline" });

    return 0;
}
