function add_simple(a: number, b: number) {
    return (a | 0) + (b | 0);
}

console.log(add_simple(1, 2));

// CHECK: define i64 @add_simple_int_int
// CHECK: trunc i64
// CHECK-NEXT: or i32 {{.*?}}, 0
// CHECK: add i64

function bitwise_ops(a: number, b: number) {
    const x = (a | 0) & (b | 0);
    const y = (a | 0) ^ (b | 0);
    return x + y;
}

console.log(bitwise_ops(10, 20));

// CHECK: define i64 @bitwise_ops_int_int
// CHECK: and i32
// CHECK: xor i32
// CHECK: add i64
