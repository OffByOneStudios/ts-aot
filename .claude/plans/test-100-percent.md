# Plan: Achieve 100% Test Conformance

## Overview

Bring all test suites to 100% pass rate by fixing remaining failures and reorganizing ECMAScript tests into the golden IR test directory.

## Current State

### Node.js API Tests (`tests/node/`)
- **Total**: 293 tests
- **Passing**: 281 (95.9%)
- **Failing**: 12

### Golden IR Tests (`tests/golden_ir/`)
- **Total**: 92 tests
- **Passing**: 87 (94.6%)
- **Failing**: 4
- **XPass**: 1 (bug fixed, should update test)

## Failing Tests Analysis

### Node.js Test Failures (12)

| Test | Error | Root Cause | Fix Approach |
|------|-------|------------|--------------|
| `cluster/cluster_fork.ts` | Timeout | Cluster worker not exiting properly | Fix IPC disconnect handling |
| `dgram/dgram_address_test.ts` | Null dereference | Socket address access before bind | Add null check |
| `es2018/test_for_await_of.ts` | Access violation | for await of arrays crashes | Fix async iteration on Promise[] |
| `fs/fs_promises_watch.ts` | Timeout | Watch never resolves | Fix watcher cleanup |
| `fs/fs_watch_simple.ts` | Timeout | Watch never resolves | Fix watcher cleanup |
| `http/http_add_trailers.ts` | Access violation | Trailer handling crash | Fix addTrailers codegen |
| `http/http_incoming_socket_aborted.ts` | Timeout | Socket abort not detected | Fix abort event |
| `http/http_server_timeout.ts` | Timeout | Server timeout not firing | Fix setTimeout |
| `http/http_trailers.ts` | Unhandled error | Trailer parsing error | Fix trailer parsing |
| `http/http_trailers_simple.ts` | Timeout | Trailer test hangs | Fix trailer send |
| `object/object_defineProperty.ts` | Null dereference | defineProperty getter issue | Fix property descriptor |
| `timers/timers_basic.ts` | Timeout | Timer test hangs | Fix timer cleanup |

### Golden IR Test Failures (4)

| Test | Error | Root Cause | Fix Approach |
|------|-------|------------|--------------|
| `typescript/regression/closure_increment.ts` | Compilation failed | ts-aot not in PATH | Fix test runner PATH |
| 3 other tests | Various | Need investigation | Debug individually |

## Implementation Plan

### Phase 1: Reorganize ECMAScript Tests (Move to Golden IR)

**Rationale**: ECMAScript language feature tests belong with golden IR tests (compiler/language behavior), not Node.js API tests. This improves organization and allows separate tracking.

**Files to Move**:
```
tests/node/es6/                    -> tests/golden_ir/ecmascript/es2015/
tests/node/es2015/                 -> tests/golden_ir/ecmascript/es2015/
tests/node/es2018/                 -> tests/golden_ir/ecmascript/es2018/
tests/node/es2019/                 -> tests/golden_ir/ecmascript/es2019/
tests/node/es2020/                 -> tests/golden_ir/ecmascript/es2020/
tests/node/es2022/                 -> tests/golden_ir/ecmascript/es2022/
tests/node/es2024/                 -> tests/golden_ir/ecmascript/es2024/
```

**Steps**:
1. Create `tests/golden_ir/ecmascript/` directory structure
2. Move ES* test files with git mv to preserve history
3. Update test runner configurations if needed
4. Verify tests still pass after move

### Phase 2: Fix Golden IR Test Runner (1 test)

**Issue**: `closure_increment.ts` fails because `ts-aot` is not in PATH.

**Fix**: Update `tests/golden_ir/runner.py` to use absolute path to compiler:
```python
# Use full path to compiler
COMPILER_PATH = os.path.join(PROJECT_ROOT, "build/src/compiler/Release/ts-aot.exe")
```

### Phase 3: Fix Timer/Watch Timeout Issues (5 tests)

These tests share a common pattern: they use timers or watchers that don't clean up properly, causing the event loop to hang.

**Root Cause**: Missing `unref()` calls or improper cleanup.

**Tests**:
- `fs/fs_promises_watch.ts`
- `fs/fs_watch_simple.ts`
- `http/http_server_timeout.ts`
- `http/http_incoming_socket_aborted.ts`
- `timers/timers_basic.ts`

**Fix Strategy**:
1. Review each test for proper cleanup patterns
2. Ensure all handles call `unref()` before test exits
3. Add explicit cleanup in test teardown
4. Consider adding test timeout mechanism

### Phase 4: Fix HTTP Trailers (3 tests)

**Tests**:
- `http/http_add_trailers.ts`
- `http/http_trailers.ts`
- `http/http_trailers_simple.ts`

**Root Cause**: Trailer implementation has codegen/runtime issues.

**Fix Strategy**:
1. Debug `http_add_trailers.ts` crash with auto-debug skill
2. Fix `addTrailers` method in IRGenerator and runtime
3. Verify trailer parsing in HTTP response handling

### Phase 5: Fix Object.defineProperty (1 test)

**Test**: `object/object_defineProperty.ts`

**Root Cause**: Property descriptor with getter returns null.

**Fix Strategy**:
1. Debug the null dereference with auto-debug
2. Fix descriptor handling in `ts_object_define_property`
3. Verify get/set/value descriptor modes work

### Phase 6: Fix UDP Socket Address (1 test)

**Test**: `dgram/dgram_address_test.ts`

**Root Cause**: `socket.address()` called before bind completes.

**Fix Strategy**:
1. Add null check in `ts_udp_socket_address`
2. Or ensure bind is complete before address access

### Phase 7: Fix Cluster Fork (1 test)

**Test**: `cluster/cluster_fork.ts`

**Root Cause**: Worker doesn't disconnect properly.

**Fix Strategy**:
1. Debug cluster worker lifecycle
2. Ensure proper IPC cleanup on worker exit
3. Add timeout fallback in test

### Phase 8: Fix for-await-of on Arrays (1 test)

**Test**: `es2018/test_for_await_of.ts`

**Root Cause**: `for await (const x of promises)` where promises is `Promise<T>[]` crashes.

**Fix Strategy**:
1. This is an ECMAScript feature test - should move to golden_ir first
2. Debug async iteration codegen for Promise arrays
3. Fix iterator protocol for arrays in async context

## Execution Order

1. **Phase 1**: Reorganize ECMAScript tests (reduces Node.js test count, improves organization)
2. **Phase 2**: Fix golden IR runner PATH issue (quick fix)
3. **Phase 3**: Fix timer/watch timeouts (5 tests, common pattern)
4. **Phase 4**: Fix HTTP trailers (3 tests, related code)
5. **Phase 5**: Fix Object.defineProperty (1 test)
6. **Phase 6**: Fix UDP address (1 test)
7. **Phase 7**: Fix cluster fork (1 test)
8. **Phase 8**: Fix for-await-of (1 test, now in golden_ir)

## Expected Results After Completion

### Node.js API Tests
- **Total**: ~278 tests (after moving 15 ES* tests)
- **Target**: 278/278 (100%)

### Golden IR Tests
- **Total**: ~107 tests (after adding 15 ES* tests)
- **Target**: 107/107 (100%)

## Files to Modify

### Phase 1 (Reorganize)
- Create: `tests/golden_ir/ecmascript/` directory tree
- Move: 15 ES* test files
- Update: Test documentation if needed

### Phase 2 (Runner)
- `tests/golden_ir/runner.py` - Fix compiler path

### Phase 3-8 (Bug Fixes)
- `src/runtime/src/TsTimers.cpp` - Timer cleanup
- `src/runtime/src/TsFileSystem.cpp` - Watcher cleanup
- `src/runtime/src/TsHttp.cpp` - Trailer handling
- `src/runtime/src/TsObject.cpp` - defineProperty fix
- `src/runtime/src/TsUDPSocket.cpp` - Address null check
- `src/runtime/src/TsCluster.cpp` - Disconnect handling
- `src/compiler/codegen/IRGenerator_*.cpp` - Async iteration fix

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| Test reorganization breaks CI | Low | Test locally before commit |
| Timer fixes cause new failures | Medium | Run full test suite after each fix |
| HTTP trailer fix is complex | Medium | Use auto-debug skill for crash analysis |
| for-await-of fix is architectural | High | May require significant async codegen changes |

## Success Criteria

- [ ] All 15 ES* tests moved to `tests/golden_ir/ecmascript/`
- [ ] Golden IR tests: 100% pass rate
- [ ] Node.js tests: 100% pass rate
- [ ] No new regressions in existing tests
- [ ] All changes committed with proper test documentation
