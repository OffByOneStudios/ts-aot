// Test while loop with dynamic values
function testLoop(length: any): any[] {
    console.log('testLoop called with length =', length);
    let index = 0;
    let result: any[] = [];
    
    console.log('Before loop: index =', index, 'length =', length);
    console.log('index < length =', index < length);
    
    while (index < length) {
        console.log('Loop iteration: index =', index);
        result.push(index);
        index++;
    }
    
    console.log('After loop: result =', JSON.stringify(result));
    return result;
}

const r = testLoop(4);
console.log('Final:', JSON.stringify(r));
