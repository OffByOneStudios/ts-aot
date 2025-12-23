function testRangeOpt(arr: Float64Array): number {
    if (arr.length < 0) {
        return -1;
    }
    return 1;
}

testRangeOpt(new Float64Array(10));
