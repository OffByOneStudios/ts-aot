// ES2022 Static Class Blocks

function user_main(): number {
    // Basic static block
    class Counter {
        static count: number = 0;

        static {
            Counter.count = 10;
        }
    }

    console.log("Counter.count after static block:");
    console.log(Counter.count);

    // Multiple static blocks (run in order)
    class MultiBlock {
        static value: number = 0;

        static {
            MultiBlock.value = 5;
        }

        static {
            MultiBlock.value = MultiBlock.value * 2;
        }
    }

    console.log("MultiBlock.value after two blocks:");
    console.log(MultiBlock.value);

    // Static block with console.log (side effects)
    class Counter2 {
        static x: number = 0;
        static y: number = 0;

        static {
            console.log("Static block executing!");
            Counter2.x = 100;
            Counter2.y = 200;
        }
    }

    console.log("Counter2 values after static block:");
    console.log(Counter2.x);
    console.log(Counter2.y);

    return 0;
}
