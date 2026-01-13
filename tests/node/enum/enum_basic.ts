// Test basic enum functionality

enum NumericEnum {
    First,
    Second,
    Third
}

enum StringEnum {
    Up = "UP",
    Down = "DOWN"
}

enum HeteroEnum {
    Active = 1,
    Inactive = "INACTIVE"
}

function user_main(): number {
    // Expected output:
    // 0
    // 1
    // 2
    // UP
    // DOWN
    // 1
    // INACTIVE
    // equals
    console.log(NumericEnum.First);
    console.log(NumericEnum.Second);
    console.log(NumericEnum.Third);
    console.log(StringEnum.Up);
    console.log(StringEnum.Down);
    console.log(HeteroEnum.Active);
    console.log(HeteroEnum.Inactive);
    
    const dir = StringEnum.Up;
    if (dir === StringEnum.Up) {
        console.log("equals");
    }
    
    return 0;
}
