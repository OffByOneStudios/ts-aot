import ms from 'ms';

function user_main(): number {
    console.log("Before ms call");
    const result = ms('2 days');
    console.log("After ms call");
    console.log(result);
    return 0;
}
