// Simple object in array test

function user_main(): number {
    // Use a simpler type without interface
    const arr: { name: string }[] = [
        { name: "Alice" }
    ];

    // Get first element
    const first = arr[0];
    console.log("Got first element");
    console.log(first.name);

    return 0;
}
