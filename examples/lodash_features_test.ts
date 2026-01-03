
// Test missing features for Lodash compilation

function testInOperator() {
    const obj: any = { a: 1 };
    if ('a' in obj) {
        console.log("'a' in obj: true");
    } else {
        console.log("'a' in obj: false");
    }
    
    if ('b' in obj) {
        console.log("'b' in obj: true");
    } else {
        console.log("'b' in obj: false");
    }
}

function testObjectSpread() {
    const a: any = { x: 1 };
    const b: any = { ...a, y: 2 };
    console.log("Object spread b.x:", b.x);
    console.log("Object spread b.y:", b.y);
}

function testDefaultParams(a: any = 10) {
    console.log("Default param a:", a);
}

function main() {
    console.log("--- Testing 'in' operator ---");
    testInOperator();

    console.log("--- Testing Object Spread ---");
    testObjectSpread();

    console.log("--- Testing Default Params ---");
    testDefaultParams(undefined); 
    testDefaultParams(20);
}

main();
