function testRange_String(s: string): number {
    let sum = 0;
    for (let i = 0; i < s.length; i++) {
        sum += s.charCodeAt(i);
    }
    return sum;
}

function testRange_Float64Array(arr: Float64Array): number {
    let sum = 0;
    for (let i = 0; i < arr.length; i++) {
        sum += arr[i];
    }
    return sum;
}

function testRange_Buffer(arr: Buffer): number {
    let sum = 0;
    for (let i = 0; i < arr.length; i++) {
        sum += arr[i];
    }
    return sum;
}

function testRange_any(arr: any): number {
    let sum = 0;
    for (let i = 0; i < arr.length; i++) {
        sum += arr[i];
    }
    return sum;
}

testRange_String("hello");
testRange_Float64Array(new Float64Array([1, 2, 3]));
testRange_Buffer(Buffer.from([1, 2, 3]));
testRange_any([1, 2, 3]);
