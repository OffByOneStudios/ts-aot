// Test arrayEach with callback

function arrayEach(array: any[], iteratee: (value: any, index: number) => void): any[] {
    console.log("arrayEach: starting, length=" + array.length);
    let index = -1;
    const length = array.length;
    while (++index < length) {
        console.log("arrayEach: index=" + index);
        if (iteratee(array[index], index) === false) {
            console.log("arrayEach: iteratee returned false, breaking");
            break;
        }
    }
    console.log("arrayEach: done");
    return array;
}

const names = ["a", "b", "c"];
console.log("Step 1: Calling arrayEach");

arrayEach(names, function(value: any, index: number) {
    console.log("callback: value=" + value + " index=" + index);
});

console.log("Done");
