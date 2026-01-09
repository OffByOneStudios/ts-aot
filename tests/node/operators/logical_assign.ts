function user_main(): number {
    let a: number = 0;
    let b: number = 5;

    // Test ||= (assign if falsy)
    a ||= 10;  // a is 0 (falsy), so a = 10
    console.log("a after ||= 10: " + a);  // Should be 10

    b ||= 20;  // b is 5 (truthy), so b stays 5
    console.log("b after ||= 20: " + b);  // Should be 5

    // Test &&= (assign if truthy)
    let c: number = 5;
    let d: number = 0;

    c &&= 100;  // c is 5 (truthy), so c = 100
    console.log("c after &&= 100: " + c);  // Should be 100

    d &&= 200;  // d is 0 (falsy), so d stays 0
    console.log("d after &&= 200: " + d);  // Should be 0

    // Test ??= (assign if nullish)
    let e: number | null = null;
    let f: number = 0;

    // For now, test with 0 which is not nullish
    f ??= 300;  // f is 0 (not nullish), so f stays 0
    console.log("f after ??= 300: " + f);  // Should be 0

    return 0;
}
