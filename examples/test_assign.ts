function user_main(): number {
    const obj1 = { a: 1 };
    const obj2 = { b: 2 };
    const result = Object.assign(obj1, obj2);
    console.log("assigned");
    console.log(result.a);
    return 0;
}
