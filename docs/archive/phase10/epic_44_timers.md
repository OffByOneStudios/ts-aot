# Epic 44: Libuv Event Loop Integration

Integrate `libuv` into the runtime to handle asynchronous I/O and timers.

## Milestones
- [x] Initialize `uv_loop_t` in `ts_main`.
- [x] Implement `setTimeout` and `setInterval` using `uv_timer_t`.
- [x] Update `ts_main` to run the loop until no more work remains.

## Action Items
- [x] Add `libuv` to `vcpkg.json`.
- [x] Implement `ts_loop_init` and `ts_loop_run` in `src/runtime/src/EventLoop.cpp`.
- [x] Implement `ts_set_timeout` and `ts_set_interval` in `src/runtime/src/EventLoop.cpp`.
- [x] Update `IRGenerator_Expressions.cpp` to handle `setTimeout` and `setInterval` calls.
- [x] Fix `PostfixUnaryExpression` support in compiler to support `count++` in timer tests.
- [x] Fix global variable scoping in `IRGenerator` to support timer variable assignments.
- [x] Verify with `tests/integration/timer_test.ts`.
