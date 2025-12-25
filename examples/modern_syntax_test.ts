
function testOptionalChaining() {
    let a: any = { b: { c: 123 } };
    console.log("a?.b?.c =", a?.b?.c);
    
    let x: any = null;
    console.log("x?.b =", x?.b);
    
    let y: any = undefined;
    console.log("y?.b =", y?.b);

    let z: any = { foo: () => "hello" };
    console.log("z?.foo() =", z?.foo?.());
    
    let w: any = null;
    console.log("w?.foo() =", w?.foo?.());
}

function testNullishCoalescing() {
    let a: any = null;
    let b = a ?? "default";
    console.log("null ?? default =", b);
    
    let c: any = undefined;
    let d = c ?? "default";
    console.log("undefined ?? default =", d);
    
    let e = 0 ?? "default";
    console.log("0 ?? default =", e);
    
    let f = "" ?? "default";
    console.log("'' ?? default =", f);
    
    let g = false ?? "default";
    console.log("false ?? default =", g);
}

testOptionalChaining();
testNullishCoalescing();
