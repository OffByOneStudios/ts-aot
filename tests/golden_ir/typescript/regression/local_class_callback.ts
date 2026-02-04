// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: Test: Local class with callback
// OUTPUT: Executing: task_one
// OUTPUT: Executing: task_two
// OUTPUT: PASS

// Regression test: Local classes (classes declared inside functions) with method callbacks

function user_main(): number {
    console.log("Test: Local class with callback");

    // Class defined inside a function (local class)
    class TaskRunner {
        private tasks: Array<{ name: string; fn: () => void }> = [];

        addTask(name: string, fn: () => void): void {
            this.tasks.push({ name: name, fn: fn });
        }

        runAll(): void {
            for (const task of this.tasks) {
                console.log("Executing: " + task.name);
                task.fn();
            }
        }
    }

    const runner = new TaskRunner();

    runner.addTask("task_one", () => {
        // Callback body intentionally empty for this test
    });

    runner.addTask("task_two", () => {
        // Callback body intentionally empty for this test
    });

    runner.runAll();

    console.log("PASS");
    return 0;
}
