function user_main(): number {
    const obj: { x?: number } = {};
    console.log("Created obj");
    
    Object.defineProperty(obj, 'x', { value: 42 });
    console.log("Called defineProperty");
    
    console.log("obj.x = " + obj.x);
    return 0;
}
