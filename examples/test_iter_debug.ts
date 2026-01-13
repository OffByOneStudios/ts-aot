// Debug custom iterator

function user_main(): number {
    console.log("Debug custom iterator...");

    const data1: number[] = [10, 20, 30];

    // First verify the function works when called directly
    const iterFunc = function(): any {
        let index = 0;
        return {
            next: function(): any {
                console.log("next called, index=" + index);
                if (index < data1.length) {
                    const val = data1[index];
                    index++;
                    console.log("returning value: " + val);
                    return { value: val, done: false };
                }
                console.log("returning done");
                return { value: undefined, done: true };
            }
        };
    };

    console.log("\nDirect call test:");
    const iter = iterFunc();
    console.log("Got iterator");
    let result = iter.next();
    console.log("First next: value=" + result.value + ", done=" + result.done);
    result = iter.next();
    console.log("Second next: value=" + result.value + ", done=" + result.done);
    result = iter.next();
    console.log("Third next: value=" + result.value + ", done=" + result.done);
    result = iter.next();
    console.log("Fourth next: value=" + result.value + ", done=" + result.done);

    console.log("\nCustom iterable test:");
    const customIterable: any = {
        "[Symbol.iterator]": iterFunc
    };

    console.log("Created customIterable");
    console.log("Keys: " + Object.keys(customIterable).join(", "));

    let sum = 0;
    for (const val of customIterable) {
        console.log("for-of value: " + val);
        sum += val;
    }
    console.log("Sum: " + sum);

    return 0;
}
