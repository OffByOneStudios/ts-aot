#include "TsPromise.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include <uv.h>
#include <iostream>
#include <cstdio>

namespace ts {

extern "C" {
    TsValue* Generator_next_internal(void* context, TsValue* value);
    TsValue* AsyncGenerator_next_internal(void* context, TsValue* value);
    TsValue* ts_map_get_property(void* obj, void* propName);
}

// Async iterator wrapper for arrays - used by for await...of
struct AsyncArrayIterator : public TsMap {
    TsArray* array;
    int64_t index;

    AsyncArrayIterator(TsArray* arr) : array(arr), index(0) {
        vtable = nullptr;
    }
};

extern void* TsPromise_VTable[];

TsPromise::TsPromise() {
    vtable = TsPromise_VTable;
    magic = MAGIC;  // Set magic for identification
    state = PromiseState::Pending;
    handled = false;
}

AsyncContext::AsyncContext() {
    state = 0;
    error = false;
    yielded = false;
    promise = ts_promise_create();
    pendingNextPromise = nullptr;
    resumeFn = nullptr;
    data = nullptr;
    resumedValue = nullptr;
    execContext = nullptr;
}

TsValue* create_generator_result(TsValue value, bool done) {
    TsMap* map = TsMap::Create();
    map->Set(TsString::Create("value"), value);
    map->Set(TsString::Create("done"), TsValue(done));
    return ts_value_make_object(map);
}

TsGenerator::TsGenerator(AsyncContext* ctx) : ctx(ctx) {
    vtable = nullptr;
    done = false;
    static_cast<TsObject*>(this)->magic = MAGIC; // Set magic for type detection

    TsValue nextFunc = nanbox_to_tagged(ts_value_make_function((void*)Generator_next_internal, this));
    this->Set(TsString::Create("next"), nextFunc);
    
    TsValue iterFunc = nanbox_to_tagged(ts_value_make_function((void*)(TsValue*(*)(void*, TsValue*))[](void* ctx, TsValue* arg) -> TsValue* {
        return ts_value_make_object(ctx);
    }, this));
    this->Set(TsString::Create("[Symbol.iterator]"), iterFunc);
}

TsValue* TsGenerator::next(TsValue* value) {
    if (done) {
        return create_generator_result(TsValue(), true);
    }

    ctx->yielded = false;
    ctx->resumedValue = value;
    ctx->resumeFn(ctx);

    if (ctx->yielded) {
        return create_generator_result(ctx->yieldedValue, false);
    } else {
        done = true;
        return create_generator_result(ctx->yieldedValue, true);
    }
}

TsAsyncGenerator::TsAsyncGenerator(AsyncContext* ctx) : ctx(ctx) {
    vtable = nullptr;
    done = false;
    static_cast<TsObject*>(this)->magic = MAGIC; // Set magic for type detection

    TsValue nextFunc = nanbox_to_tagged(ts_value_make_function((void*)AsyncGenerator_next_internal, this));
    this->Set(TsString::Create("next"), nextFunc);
    
    TsValue iterFunc = nanbox_to_tagged(ts_value_make_function((void*)(TsValue*(*)(void*, TsValue*))[](void* ctx, TsValue* arg) -> TsValue* {
        return ts_value_make_object(ctx);
    }, this));
    this->Set(TsString::Create("[Symbol.asyncIterator]"), iterFunc);
}

TsPromise* TsAsyncGenerator::next(TsValue* value) {
    TsPromise* p = ts_promise_create();
    if (done) {
        ts_promise_resolve_internal(p, create_generator_result(TsValue(), true));
        return p;
    }
    
    ctx->pendingNextPromise = p;
    ctx->yielded = false;
    ctx->resumedValue = value;
    ctx->resumeFn(ctx);

    return p;
}

extern "C" {

void ts_async_yield(TsValue* value, AsyncContext* ctx) {
    ctx->yielded = true;
    ctx->yieldedValue = nanbox_to_tagged(value);

    if (ctx->pendingNextPromise) {
        ts_promise_resolve_internal(ctx->pendingNextPromise, create_generator_result(nanbox_to_tagged(value), false));
        ctx->pendingNextPromise = nullptr;
    }
}

TsGenerator* ts_generator_create(AsyncContext* ctx) {
    void* mem = ts_alloc(sizeof(TsGenerator));
    TsGenerator* gen = new (mem) TsGenerator(ctx);
    ctx->syncGenerator = gen;  // Store back-pointer for impl function access
    return gen;
}

TsValue* Generator_next_internal(void* context, TsValue* value) {
    TsGenerator* gen = (TsGenerator*)context;
    if (!gen) return nullptr;
    return gen->next(value);
}

TsValue* Generator_next(TsValue* genVal, TsValue* value) {
    void* raw = ts_value_get_object(genVal);
    if (!raw) return ts_value_make_undefined();

    // Check if this is a TsMap-based iterator (has "next" property)
    // rather than a real TsGenerator
    uint32_t magic = *(uint32_t*)((char*)raw + 16);
    if (magic == 0x4D415053) { // TsMap::MAGIC = "MAPS"
        // It's a Map object acting as an iterator - look up "next" and call it
        TsString* nextKey = TsString::Create("next");
        TsValue* nextFn = ts_map_get_property(raw, nextKey);
        if (nextFn && !ts_value_is_undefined(nextFn)) {
            return ts_call_0(nextFn);
        }
        return ts_value_make_undefined();
    }

    return Generator_next_internal(raw, value);
}

void ts_generator_return(TsGenerator* gen, TsValue* value) {
    if (!gen) return;
    gen->done = true;
    if (value) {
        gen->ctx->yieldedValue = nanbox_to_tagged(value);
    } else {
        gen->ctx->yieldedValue = TsValue(); // undefined
    }
}

void ts_generator_return_via_ctx(AsyncContext* ctx, TsValue* value) {
    if (!ctx || !ctx->syncGenerator) return;
    ts_generator_return((TsGenerator*)ctx->syncGenerator, value);
}

TsValue* ts_generator_yield(TsValue* value) {
    // This is a simplified yield implementation.
    // In a proper generator state machine, this would:
    // 1. Store the yielded value
    // 2. Suspend the generator
    // 3. Return the value passed to next() when resumed
    //
    // For now, we just return undefined (value passed to next() is typically undefined)
    return ts_value_make_undefined();
}

TsAsyncGenerator* ts_async_generator_create(AsyncContext* ctx) {
    void* mem = ts_alloc(sizeof(TsAsyncGenerator));
    TsAsyncGenerator* gen = new (mem) TsAsyncGenerator(ctx);
    ctx->generator = gen;
    return gen;
}

TsValue* AsyncGenerator_next_internal(void* context, TsValue* value) {
    TsAsyncGenerator* gen = (TsAsyncGenerator*)context;
    if (!gen) return nullptr;
    TsPromise* p = gen->next(value);
    return ts_value_make_promise(p);
}

TsValue* AsyncGenerator_next(TsValue* genVal, TsValue* value) {
    return AsyncGenerator_next_internal(ts_value_get_object(genVal), value);
}

void ts_async_generator_return(TsAsyncGenerator* gen, TsValue* value) {
    if (!gen) return;
    gen->done = true;
    if (value) {
        gen->ctx->yieldedValue = nanbox_to_tagged(value);
    } else {
        gen->ctx->yieldedValue = TsValue(); // undefined
    }
}

// Magic number for AsyncArrayIterator
static constexpr uint32_t ASYNC_ARRAY_ITER_MAGIC = 0x41414954; // "AAIT"

extern "C" TsValue* ts_async_iterator_get(TsValue* iterable) {
    if (!iterable) {
        return nullptr;
    }

    TsValue iterVal = nanbox_to_tagged(iterable);

    // Check if it's an array (ARRAY_PTR type = 7) - wrap it in an async iterator
    if (iterVal.type == ValueType::ARRAY_PTR && iterVal.ptr_val) {
        uint32_t magic = *(uint32_t*)iterVal.ptr_val;
        if (magic == TsArray::MAGIC) {
            // Create AsyncArrayIterator wrapper
            void* mem = ts_alloc(sizeof(AsyncArrayIterator));
            AsyncArrayIterator* iter = new (mem) AsyncArrayIterator((TsArray*)iterVal.ptr_val);
            // Set magic for identification
            *(uint32_t*)mem = ASYNC_ARRAY_ITER_MAGIC;
            return ts_value_make_object(iter);
        }
    }

    if (iterVal.type == ValueType::OBJECT_PTR && iterVal.ptr_val) {
        TsString* key = TsString::Create("[Symbol.asyncIterator]");
        // Use scalar helpers directly
        uint64_t hash = (uint64_t)key; // Use pointer as hash
        int64_t bucket = __ts_map_find_bucket(iterVal.ptr_val, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)key);
        if (bucket >= 0) {
            uint8_t method_type;
            int64_t method_val;
            __ts_map_get_value_at(iterVal.ptr_val, bucket, &method_type, &method_val);
            // Check for both OBJECT_PTR and FUNCTION_PTR since ts_value_make_function uses FUNCTION_PTR
            if (method_type == (uint8_t)ValueType::OBJECT_PTR || method_type == (uint8_t)ValueType::FUNCTION_PTR) {
                TsFunction* func = (TsFunction*)method_val;
                typedef TsValue* (*AsyncIterFunc)(void*);
                return ((AsyncIterFunc)func->funcPtr)(func->context);
            }
        }
    }

    return iterable;
}

extern "C" TsValue* ts_async_iterator_next(TsValue* iterator, TsValue* value) {
    if (!iterator) {
        return nullptr;
    }

    TsValue iterVal = nanbox_to_tagged(iterator);

    // Check if it's our AsyncArrayIterator
    if (iterVal.type == ValueType::OBJECT_PTR && iterVal.ptr_val) {
        uint32_t magic = *(uint32_t*)iterVal.ptr_val;
        if (magic == ASYNC_ARRAY_ITER_MAGIC) {
            AsyncArrayIterator* iter = (AsyncArrayIterator*)iterVal.ptr_val;
            int64_t len = iter->array->Length();

            if (iter->index >= len) {
                // Done - return resolved promise with { value: undefined, done: true }
                TsValue undef;
                undef.type = ValueType::UNDEFINED;
                undef.i_val = 0;
                TsValue* result = create_generator_result(undef, true);

                // Wrap in resolved promise
                TsPromise* p = ts_promise_create();
                ts_promise_resolve_internal(p, result);
                return ts_value_make_promise(p);
            }

            // Get current element - stored as NaN-boxed TsValue* in array
            TsValue* elemBoxed = (TsValue*)iter->array->Get(iter->index);
            iter->index++;

            // Create a new promise that resolves with { value, done: false } when elem resolves
            TsPromise* resultPromise = ts_promise_create();

            // Decode the element using nanbox_to_tagged
            TsValue elemDecoded = nanbox_to_tagged(elemBoxed);

            // Check for Promise
            if ((elemDecoded.type == ValueType::OBJECT_PTR || elemDecoded.type == ValueType::PROMISE_PTR)
                && elemDecoded.ptr_val) {
                void* elemPtr = elemDecoded.ptr_val;
                // Check if it's a TsPromise by checking magic at offset 16
                uint32_t magicVal = *(uint32_t*)((char*)elemPtr + 16);
                if (magicVal == TsPromise::MAGIC) {
                    TsPromise* elemPromise = (TsPromise*)elemPtr;
                        // When elemPromise resolves, resolve resultPromise with { value, done: false }
                        TsValue onFulfilled;
                        onFulfilled.type = ValueType::OBJECT_PTR;

                        // Create a function that wraps the resolved value
                        struct WrapContext {
                            TsPromise* resultPromise;
                        };
                        WrapContext* ctx = (WrapContext*)ts_alloc(sizeof(WrapContext));
                        ctx->resultPromise = resultPromise;

                        void* wrapFuncAddr = (void*)(+[](void* context, TsValue* resolvedValue) -> TsValue* {
                            WrapContext* ctx = (WrapContext*)context;
                            TsValue* iterResult = create_generator_result(nanbox_to_tagged(resolvedValue), false);
                            ts_promise_resolve_internal(ctx->resultPromise, iterResult);
                            return nullptr;
                        });
                        // Use placement new with constructor to properly set up vtable
                        TsFunction* wrapFunc = new (ts_alloc(sizeof(TsFunction))) TsFunction(wrapFuncAddr, ctx, FunctionType::COMPILED);

                        onFulfilled.ptr_val = wrapFunc;
                        elemPromise->then(onFulfilled);
                        return ts_value_make_promise(resultPromise);
                    }
                }

            // Non-promise value - resolve immediately
            TsValue* iterResult = create_generator_result(elemDecoded, false);
            ts_promise_resolve_internal(resultPromise, iterResult);
            return ts_value_make_promise(resultPromise);
        }
    }

    if (iterVal.type == ValueType::OBJECT_PTR && iterVal.ptr_val) {
        TsString* key = TsString::Create("next");
        // Use scalar helpers directly
        uint64_t hash = (uint64_t)key; // Use pointer as hash
        int64_t bucket = __ts_map_find_bucket(iterVal.ptr_val, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)key);
        if (bucket >= 0) {
            uint8_t method_type;
            int64_t method_val;
            __ts_map_get_value_at(iterVal.ptr_val, bucket, &method_type, &method_val);
            // Check for both OBJECT_PTR and FUNCTION_PTR since ts_value_make_function uses FUNCTION_PTR
            if (method_type == (uint8_t)ValueType::OBJECT_PTR || method_type == (uint8_t)ValueType::FUNCTION_PTR) {
                TsFunction* func = (TsFunction*)method_val;
                typedef TsValue* (*NextFunc)(void*, TsValue*);
                return ((NextFunc)func->funcPtr)(func->context, value);
            }
        }
    }

    return AsyncGenerator_next(iterator, value);
}

void ts_async_generator_resolve(AsyncContext* ctx, TsValue* value, bool done) {
    if (!ctx->generator) return;
    ctx->generator->done = done;
    if (ctx->pendingNextPromise) {
        TsValue* res = create_generator_result(nanbox_to_tagged(value), done);
        ts_promise_resolve_internal(ctx->pendingNextPromise, res);
        ctx->pendingNextPromise = nullptr;
    }
}

// yield* delegation support - get an iterator from an iterable
TsValue* ts_iterator_get(TsValue* iterable) {
    if (!iterable) {
        return nullptr;
    }

    // First, try to extract the raw object pointer using ts_value_get_object
    // This handles both boxed TsValue* and raw object pointers
    void* rawObj = ts_value_get_object(iterable);

    // Check if we have a TsMap-based object (TsMap, TsGenerator, TsAsyncGenerator)
    if (rawObj) {
        // Check magic at offset 16 for TsObject-derived types
        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
        if (magic16 == 0x4D415053 ||  // TsMap::MAGIC
            magic16 == 0x47454E52 ||  // TsGenerator::MAGIC "GENR"
            magic16 == 0x4147454E) {  // TsAsyncGenerator::MAGIC "AGEN"
            TsMap* obj = (TsMap*)rawObj;

            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                TsFunction* func = (TsFunction*)iterMethod.ptr_val;
                if (func && func->funcPtr) {
                    typedef TsValue* (*IterFunc)(void*);
                    return ((IterFunc)func->funcPtr)(func->context);
                }
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                // Already an iterator, return as-is
                return iterable;
            }
        }

        // Also try magic at offset 8 for some TsMap layouts
        uint32_t magic8 = *(uint32_t*)((char*)rawObj + 8);
        if (magic8 == 0x4D415053) { // TsMap::MAGIC
            TsMap* obj = (TsMap*)rawObj;

            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                TsFunction* func = (TsFunction*)iterMethod.ptr_val;
                if (func && func->funcPtr) {
                    typedef TsValue* (*IterFunc)(void*);
                    return ((IterFunc)func->funcPtr)(func->context);
                }
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                return iterable;
            }
        }
    }

    // Check for raw TsArray* pointer (TsArray has no vtable, magic at offset 0)
    TsValue iterDecoded = nanbox_to_tagged(iterable);
    if (rawObj) {
        uint32_t magic0 = *(uint32_t*)rawObj;
        if (magic0 == 0x41525259) { // TsArray::MAGIC
            TsArray* arr = (TsArray*)rawObj;
            // Update decoded value for array iterator path below
            iterDecoded.type = ValueType::ARRAY_PTR;
            iterDecoded.ptr_val = arr;
            // Fall through to ARRAY_PTR check below
        }
    }

    // Fall back to type-based check for explicit OBJECT_PTR values
    if (iterDecoded.type == ValueType::OBJECT_PTR) {
        TsMap* obj = (TsMap*)iterDecoded.ptr_val;
        if (obj) {
            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                TsFunction* func = (TsFunction*)iterMethod.ptr_val;
                if (func->funcPtr) {
                    typedef TsValue* (*IterFunc)(void*);
                    return ((IterFunc)func->funcPtr)(func->context);
                }
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                // Already an iterator, return as-is
                return iterable;
            }
        }
    }

    // For arrays, create an array iterator
    if (iterDecoded.type == ValueType::ARRAY_PTR) {
        TsArray* arr = (TsArray*)iterDecoded.ptr_val;
        if (arr) {
            // Create a simple array iterator object
            TsMap* iterator = TsMap::Create();

            // Create properly typed keys for internal properties
            TsValue arrKey, idxKey;
            arrKey.type = ValueType::STRING_PTR;
            arrKey.ptr_val = TsString::Create("__arr");
            idxKey.type = ValueType::STRING_PTR;
            idxKey.ptr_val = TsString::Create("__idx");

            iterator->Set(arrKey, iterDecoded);
            iterator->Set(idxKey, TsValue((int64_t)0));

            // Create the next function that iterates over the array
            TsValue nextFunc = nanbox_to_tagged(ts_value_make_function((void*)(TsValue*(*)(void*, TsValue*))[](void* ctx, TsValue* arg) -> TsValue* {
                TsMap* self = (TsMap*)ctx;
                if (!self) return create_generator_result(TsValue(), true);

                TsValue arrKey, idxKey;
                arrKey.type = ValueType::STRING_PTR;
                arrKey.ptr_val = TsString::Create("__arr");
                idxKey.type = ValueType::STRING_PTR;
                idxKey.ptr_val = TsString::Create("__idx");

                TsValue arrVal = self->Get(arrKey);
                TsValue idxVal = self->Get(idxKey);

                TsArray* arr = (TsArray*)arrVal.ptr_val;
                int64_t idx = idxVal.i_val;

                if (!arr) {
                    return create_generator_result(TsValue(), true);
                }

                size_t len = arr->Length();

                if (idx >= (int64_t)len) {
                    return create_generator_result(TsValue(), true);
                }

                // Get the value and increment index
                // Use GetElementBoxed which returns a proper TsValue*, not Get() which returns raw int64_t
                TsValue* elem = arr->GetElementBoxed(idx);
                self->Set(idxKey, TsValue(idx + 1));

                if (elem) {
                    return create_generator_result(nanbox_to_tagged(elem), false);
                } else {
                    TsValue undef;
                    undef.type = ValueType::UNDEFINED;
                    return create_generator_result(undef, false);
                }
            }, iterator));

            TsValue nextKey;
            nextKey.type = ValueType::STRING_PTR;
            nextKey.ptr_val = TsString::Create("next");
            iterator->Set(nextKey, nextFunc);
            return ts_value_make_object(iterator);
        }
    }

    return iterable;
}

// Call next on an iterator
TsValue* ts_iterator_next(TsValue* iterator, TsValue* value) {
    if (!iterator) return nullptr;

    TsValue iterVal = nanbox_to_tagged(iterator);
    if (iterVal.type == ValueType::OBJECT_PTR && iterVal.ptr_val) {
        TsMap* obj = (TsMap*)iterVal.ptr_val;
        if (obj) {
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = TsString::Create("next");
            TsValue nextMethod = obj->Get(nextKeyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                TsFunction* func = (TsFunction*)nextMethod.ptr_val;
                if (func->funcPtr) {
                    typedef TsValue* (*NextFunc)(void*, TsValue*);
                    return ((NextFunc)func->funcPtr)(func->context, value);
                }
            }
        }
    }

    // Fallback: return done
    return create_generator_result(TsValue(), true);
}

// Check if an iterator result is done
bool ts_iterator_result_done(TsValue* result) {
    if (!result) return true;

    TsValue resVal = nanbox_to_tagged(result);
    if (resVal.type == ValueType::OBJECT_PTR && resVal.ptr_val) {
        TsMap* obj = (TsMap*)resVal.ptr_val;
        if (obj) {
            TsValue doneKeyVal;
            doneKeyVal.type = ValueType::STRING_PTR;
            doneKeyVal.ptr_val = TsString::Create("done");
            TsValue doneVal = obj->Get(doneKeyVal);
            if (doneVal.type == ValueType::BOOLEAN) {
                return doneVal.b_val;
            }
        }
    }

    return true;
}

// Get value from an iterator result
TsValue* ts_iterator_result_value(TsValue* result) {
    if (!result) {
        return ts_value_make_undefined();
    }

    TsValue resVal = nanbox_to_tagged(result);
    if (resVal.type == ValueType::OBJECT_PTR && resVal.ptr_val) {
        TsMap* obj = (TsMap*)resVal.ptr_val;
        if (obj) {
            TsValue valueKeyVal;
            valueKeyVal.type = ValueType::STRING_PTR;
            valueKeyVal.ptr_val = TsString::Create("value");
            TsValue val = obj->Get(valueKeyVal);
            return nanbox_from_tagged(val);
        }
    }

    return ts_value_make_undefined();
}

void ts_async_resume(AsyncContext* ctx, TsValue* value) {
    // With NaN boxing, the value IS the NaN-boxed uint64_t encoded as TsValue*.
    // No heap copy needed - the value is self-contained in the pointer.
    ctx->resumedValue = value;

    if (ctx->resumeFn) {
        ctx->resumeFn(ctx);
    }
}

AsyncContext* ts_async_context_create() {
    return new (ts_alloc(sizeof(AsyncContext))) AsyncContext();
}

TsPromise* ts_async_get_promise(AsyncContext* ctx) {
    return ctx->promise;
}

TsPromise* ts_promise_create() {
    void* mem = ts_alloc(sizeof(TsPromise));
    return new (mem) TsPromise();
}

struct PromiseResolveTask {
    TsPromise* promise;
    TsValue value;
};

struct PromiseCallbackTask {
    TsPromise* promise;
    TsPromise::Callback cb;
};

void ts_promise_run_callback(TsPromise* promise, TsPromise::Callback& cb, TsValue& value) {
    // Convert stored TsValue struct to NaN-boxed for outgoing calls
    TsValue* nbValue = nanbox_from_tagged(value);

    if (cb.asyncCtx) {
        cb.asyncCtx->error = (promise->state == PromiseState::Rejected);
        ts_async_resume(cb.asyncCtx, nbValue);
        return;
    }

    if (cb.onFinally.type == ValueType::OBJECT_PTR && cb.onFinally.ptr_val) {
        ts_call_0(nanbox_from_tagged(cb.onFinally));
        if (cb.nextPromise) {
            if (promise->state == PromiseState::Fulfilled) {
                ts_promise_resolve_internal(cb.nextPromise, nbValue);
            } else {
                ts_promise_reject_internal(cb.nextPromise, nbValue);
            }
        }
        return;
    }

    TsValue handler = (promise->state == PromiseState::Fulfilled) ? cb.onFulfilled : cb.onRejected;

    if ((handler.type == ValueType::OBJECT_PTR || handler.type == ValueType::FUNCTION_PTR) && handler.ptr_val) {
        TsValue* result = ts_call_1(nanbox_from_tagged(handler), nbValue);
        if (cb.nextPromise) {
            if (result) {
                ts_promise_resolve_internal(cb.nextPromise, result);
            } else {
                ts_promise_resolve_internal(cb.nextPromise, ts_value_make_undefined());
            }
        }
    } else {
        // No handler, propagate
        if (cb.nextPromise) {
            if (promise->state == PromiseState::Fulfilled) {
                ts_promise_resolve_internal(cb.nextPromise, nbValue);
            } else {
                ts_promise_reject_internal(cb.nextPromise, nbValue);
            }
        }
    }
}

void ts_promise_settle_microtask(void* data) {
    auto task = static_cast<PromiseResolveTask*>(data);
    TsPromise* promise = task->promise;
    if (promise->state == PromiseState::Rejected && !promise->handled) {
        ts_console_log_value(nanbox_from_tagged(promise->value));
    }

    for (auto& cb : promise->callbacks) {
        ts_promise_run_callback(promise, cb, promise->value);
    }
    promise->callbacks.clear();
}

void ts_promise_callback_microtask(void* data) {
    auto task = static_cast<PromiseCallbackTask*>(data);
    ts_promise_run_callback(task->promise, task->cb, task->promise->value);
}

void ts_promise_resolve_internal(TsPromise* promise, TsValue* value);
void ts_promise_reject_internal(TsPromise* promise, TsValue* reason);

TsValue* ts_promise_resolve_internal_helper(void* context, TsValue* val) {
    ts_promise_resolve_internal((TsPromise*)context, val);
    return nullptr;
}

TsValue* ts_promise_reject_internal_helper(void* context, TsValue* reason) {
    ts_promise_reject_internal((TsPromise*)context, reason);
    return nullptr;
}

// Native function wrappers for Promise constructor (variadic calling convention)
TsValue* ts_promise_resolve_wrapper(void* context, int argc, TsValue** argv) {
    TsValue* value = (argc > 0) ? argv[0] : nullptr;
    ts_promise_resolve_internal((TsPromise*)context, value);
    return nullptr;
}

TsValue* ts_promise_reject_wrapper(void* context, int argc, TsValue** argv) {
    TsValue* reason = (argc > 0) ? argv[0] : nullptr;
    ts_promise_reject_internal((TsPromise*)context, reason);
    return nullptr;
}

void ts_promise_resolve_internal(TsPromise* promise, TsValue* value) {
    if (!promise) {
        return;
    }
    if (promise->state != PromiseState::Pending) {
        return;
    }

    TsValue val = value ? nanbox_to_tagged(value) : TsValue();

    if (val.type == ValueType::PROMISE_PTR && val.ptr_val) {
        TsPromise* other = (TsPromise*)val.ptr_val;
        TsValue onFulfilled;
        onFulfilled.type = ValueType::OBJECT_PTR;
        TsFunction* f1 = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_resolve_internal_helper, promise, FunctionType::COMPILED);
        onFulfilled.ptr_val = f1;

        TsValue onRejected;
        onRejected.type = ValueType::OBJECT_PTR;
        TsFunction* f2 = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_reject_internal_helper, promise, FunctionType::COMPILED);
        onRejected.ptr_val = f2;

        other->then(onFulfilled, onRejected);
        return;
    }

    promise->state = PromiseState::Fulfilled;
    promise->value = val;

    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = promise->value;
    ts_queue_microtask(ts_promise_settle_microtask, task);
}

void ts_promise_reject_internal(TsPromise* promise, TsValue* reason) {
    if (promise->state != PromiseState::Pending) {
        return;
    }
    promise->state = PromiseState::Rejected;
    promise->value = reason ? nanbox_to_tagged(reason) : TsValue();

    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = promise->value;
    ts_queue_microtask(ts_promise_settle_microtask, task);
}

TsValue* ts_promise_resolve(void* context, TsValue* value) {
    TsValue val = value ? nanbox_to_tagged(value) : TsValue();
    if (val.type == ValueType::PROMISE_PTR) {
        return value;
    }
    TsPromise* p = ts_promise_create();
    ts_promise_resolve_internal(p, value);
    return ts_value_make_promise(p);
}

TsValue* ts_promise_reject(void* context, TsValue* reason) {
    TsPromise* p = ts_promise_create();
    ts_promise_reject_internal(p, reason);
    return ts_value_make_promise(p);
}

TsValue* ts_promise_then(TsValue* promise, TsValue* onFulfilled, TsValue* onRejected) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return nullptr;
    TsPromise* p = (TsPromise*)pVal.ptr_val;
    TsPromise* next = p->then(onFulfilled ? nanbox_to_tagged(onFulfilled) : TsValue(), onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_catch(TsValue* promise, TsValue* onRejected) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return nullptr;
    TsPromise* p = (TsPromise*)pVal.ptr_val;
    TsPromise* next = p->catch_error(onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_finally(TsValue* promise, TsValue* onFinally) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return nullptr;
    TsPromise* p = (TsPromise*)pVal.ptr_val;
    TsPromise* next = p->finally(onFinally ? nanbox_to_tagged(onFinally) : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_new(TsValue* executor) {
    if (!executor) {
        return nullptr;
    }

    // Create a new promise
    TsPromise* promise = ts_promise_create();

    // Create resolve and reject functions using the variadic wrappers
    TsValue* resolveArg = ts_value_make_native_function(
        (void*)ts_promise_resolve_wrapper,
        promise
    );

    TsValue* rejectArg = ts_value_make_native_function(
        (void*)ts_promise_reject_wrapper,
        promise
    );

    // Call the executor with (resolve, reject)
    if (executor) {
        ts_call_2(executor, resolveArg, rejectArg);
    }

    // Return the promise
    return ts_value_make_promise(promise);
}

TsValue* ts_promise_await(TsValue* promise) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return promise;
    TsPromise* p = (TsPromise*)pVal.ptr_val;

    // If already settled, return value immediately
    if (p->state != PromiseState::Pending) {
        return nanbox_from_tagged(p->value);
    }

    // Run both microtasks and event loop until the promise settles.
    // This is needed because the promise may depend on I/O operations
    // (e.g. fetch() needs DNS resolve + TCP connect via libuv).
    uv_loop_t* loop = uv_default_loop();
    while (p->state == PromiseState::Pending) {
        ts_run_microtasks();
        if (p->state != PromiseState::Pending) break;
        // Run one event loop iteration to process I/O
        if (uv_loop_alive(loop)) {
            uv_run(loop, UV_RUN_ONCE);
        }
    }

    return nanbox_from_tagged(p->value);
}

void ts_async_await(TsValue* promise, AsyncContext* ctx) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) {
        ts_async_resume(ctx, promise);
        return;
    }
    TsPromise* p = (TsPromise*)pVal.ptr_val;
    p->then_async(ctx);
}

struct PromiseAllContext {
    TsPromise* mainPromise;
    TsArray* results;
    size_t remaining;
};

struct PromiseAllElementContext {
    PromiseAllContext* allCtx;
    size_t index;
};

TsValue* ts_promise_all_fulfilled_helper(void* context, TsValue* val) {
    auto ctx = (PromiseAllElementContext*)context;
    ctx->allCtx->results->Set(ctx->index, (int64_t)val);
    ctx->allCtx->remaining--;
    if (ctx->allCtx->remaining == 0) {
        ts_promise_resolve_internal(ctx->allCtx->mainPromise, ts_value_make_array(ctx->allCtx->results));
    }
    return nullptr;
}

TsValue* ts_promise_all_rejected_helper(void* context, TsValue* reason) {
    auto ctx = (PromiseAllElementContext*)context;
    ts_promise_reject_internal(ctx->allCtx->mainPromise, reason);
    return nullptr;
}

TsValue* ts_promise_all(TsValue* iterableVal) {
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        return ts_promise_resolve(nullptr, ts_value_make_array(TsArray::Create(0)));
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();

    if (total == 0) {
        ts_promise_resolve_internal(mainPromise, ts_value_make_array(TsArray::Create(0)));
        return ts_value_make_promise(mainPromise);
    }

    PromiseAllContext* allCtx = (PromiseAllContext*)ts_alloc(sizeof(PromiseAllContext));
    allCtx->mainPromise = mainPromise;
    allCtx->results = TsArray::CreateSized(total);
    allCtx->remaining = total;

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);

        PromiseAllElementContext* elCtx = (PromiseAllElementContext*)ts_alloc(sizeof(PromiseAllElementContext));
        elCtx->allCtx = allCtx;
        elCtx->index = i;

        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_all_fulfilled_helper, elCtx);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_all_rejected_helper, elCtx);

        ts_promise_then(p, onFulfilled, onRejected);
    }

    return ts_value_make_promise(mainPromise);
}

TsValue* ts_promise_race_fulfilled_helper(void* context, TsValue* val) {
    TsPromise* mainPromise = (TsPromise*)context;
    ts_promise_resolve_internal(mainPromise, val);
    return nullptr;
}

TsValue* ts_promise_race_rejected_helper(void* context, TsValue* reason) {
    TsPromise* mainPromise = (TsPromise*)context;
    ts_promise_reject_internal(mainPromise, reason);
    return nullptr;
}

TsValue* ts_promise_race(TsValue* iterableVal) {
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        ts::TsPromise* p = ts_promise_create();
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);
        
        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_race_fulfilled_helper, mainPromise);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_race_rejected_helper, mainPromise);
        
        ts_promise_then(p, onFulfilled, onRejected);
    }
    
    return ts_value_make_promise(mainPromise);
}

struct AllSettledContext {
    TsPromise* mainPromise;
    TsArray* results;
    size_t remaining;
};

struct AllSettledElementContext {
    AllSettledContext* ctx;
    size_t index;
};

TsValue* ts_promise_all_settled_fulfilled_helper(void* context, TsValue* val) {
    AllSettledElementContext* e = (AllSettledElementContext*)context;
    TsMap* obj = TsMap::Create();
    TsValue status;
    status.type = ValueType::STRING_PTR;
    status.ptr_val = ts_string_create("fulfilled");
    obj->Set((TsString*)ts_string_create("status"), status);
    obj->Set((TsString*)ts_string_create("value"), val ? nanbox_to_tagged(val) : TsValue());
    e->ctx->results->Set(e->index, (int64_t)ts_value_make_object(obj));
    e->ctx->remaining--;
    if (e->ctx->remaining == 0) {
        ts_promise_resolve_internal(e->ctx->mainPromise, ts_value_make_array(e->ctx->results));
    }
    return nullptr;
}

TsValue* ts_promise_all_settled_rejected_helper(void* context, TsValue* reason) {
    AllSettledElementContext* e = (AllSettledElementContext*)context;
    TsMap* obj = TsMap::Create();
    TsValue status;
    status.type = ValueType::STRING_PTR;
    status.ptr_val = ts_string_create("rejected");
    obj->Set((TsString*)ts_string_create("status"), status);
    obj->Set((TsString*)ts_string_create("reason"), reason ? nanbox_to_tagged(reason) : TsValue());
    e->ctx->results->Set(e->index, (int64_t)ts_value_make_object(obj));
    e->ctx->remaining--;
    if (e->ctx->remaining == 0) {
        ts_promise_resolve_internal(e->ctx->mainPromise, ts_value_make_array(e->ctx->results));
    }
    return nullptr;
}

extern "C" TsValue* ts_promise_allSettled(TsValue* iterableVal) {
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        ts::TsPromise* p = ts_promise_create();
        ts_promise_resolve_internal(p, ts_value_make_array(TsArray::Create(0)));
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();
    if (total == 0) {
        ts_promise_resolve_internal(mainPromise, ts_value_make_array(TsArray::Create(0)));
        return ts_value_make_promise(mainPromise);
    }

    AllSettledContext* ctx = (AllSettledContext*)ts_alloc(sizeof(AllSettledContext));
    ctx->mainPromise = mainPromise;
    ctx->results = TsArray::CreateSized(total);
    ctx->remaining = total;

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);
        
        AllSettledElementContext* elCtx = (AllSettledElementContext*)ts_alloc(sizeof(AllSettledElementContext));
        elCtx->ctx = ctx;
        elCtx->index = i;

        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_all_settled_fulfilled_helper, elCtx);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_all_settled_rejected_helper, elCtx);
        
        ts_promise_then(p, onFulfilled, onRejected);
    }
    return ts_value_make_promise(mainPromise);
}

struct AnyContext {
    TsPromise* mainPromise;
    TsArray* errors;
    size_t remaining;
};

TsValue* ts_promise_any_fulfilled_helper(void* context, TsValue* val) {
    AnyContext* ctx = (AnyContext*)context;
    ts_promise_resolve_internal(ctx->mainPromise, val);
    return nullptr;
}

TsValue* ts_promise_any_rejected_helper(void* context, TsValue* reason) {
    AnyContext* ctx = (AnyContext*)context;
    ctx->errors->Push((int64_t)reason);
    ctx->remaining--;
    if (ctx->remaining == 0) {
        ts_promise_reject_internal(ctx->mainPromise, ts_value_make_array(ctx->errors));
    }
    return nullptr;
}

// ES2024 Promise.withResolvers()
// Returns an object with { promise, resolve, reject } properties
extern "C" TsValue* ts_promise_withResolvers() {
    // Create a new promise
    TsPromise* promise = ts_promise_create();

    // Create resolve and reject functions using the variadic wrappers
    TsValue* resolveFunc = ts_value_make_native_function(
        (void*)ts_promise_resolve_wrapper,
        promise
    );

    TsValue* rejectFunc = ts_value_make_native_function(
        (void*)ts_promise_reject_wrapper,
        promise
    );

    // Create the result object with { promise, resolve, reject }
    TsMap* result = TsMap::Create();

    // Set promise property
    TsValue promiseVal;
    promiseVal.type = ValueType::PROMISE_PTR;
    promiseVal.ptr_val = promise;
    result->Set(TsString::Create("promise"), promiseVal);

    // Set resolve property
    result->Set(TsString::Create("resolve"), nanbox_to_tagged(resolveFunc));

    // Set reject property
    result->Set(TsString::Create("reject"), nanbox_to_tagged(rejectFunc));

    return ts_value_make_object(result);
}

extern "C" TsValue* ts_promise_any(TsValue* iterableVal) {
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        ts::TsPromise* p = ts_promise_create();
        ts_promise_reject_internal(p, iterableVal);
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();
    if (total == 0) {
        ts_promise_reject_internal(mainPromise, ts_value_make_object(TsArray::Create()));
        return ts_value_make_promise(mainPromise);
    }

    AnyContext* ctx = (AnyContext*)ts_alloc(sizeof(AnyContext));
    ctx->mainPromise = mainPromise;
    ctx->errors = TsArray::Create(total);
    ctx->remaining = total;

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);
        
        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_any_fulfilled_helper, ctx);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_any_rejected_helper, ctx);
        
        ts_promise_then(p, onFulfilled, onRejected);
    }
    return ts_value_make_promise(mainPromise);
}

} // extern "C"

TsPromise* TsPromise::then(TsValue onFulfilled, TsValue onRejected) {
    handled = true;
    Callback cb;
    cb.onFulfilled = onFulfilled;
    cb.onRejected = onRejected;
    cb.onFinally = TsValue();
    cb.nextPromise = ts_promise_create();
    cb.asyncCtx = nullptr;
    
    if (state != PromiseState::Pending) {
        auto task = static_cast<PromiseCallbackTask*>(ts_alloc(sizeof(PromiseCallbackTask)));
        task->promise = this;
        task->cb = cb;
        
        ts_queue_microtask(ts_promise_callback_microtask, task);
    } else {
        callbacks.push_back(cb);
    }
    return cb.nextPromise;
}

TsPromise* TsPromise::catch_error(TsValue onRejected) {
    return then(TsValue(), onRejected);
}

TsPromise* TsPromise::finally(TsValue onFinally) {
    handled = true;
    Callback cb;
    cb.onFulfilled = TsValue();
    cb.onRejected = TsValue();
    cb.onFinally = onFinally;
    cb.nextPromise = ts_promise_create();
    cb.asyncCtx = nullptr;

    if (state != PromiseState::Pending) {
        auto task = static_cast<PromiseCallbackTask*>(ts_alloc(sizeof(PromiseCallbackTask)));
        task->promise = this;
        task->cb = cb;
        
        ts_queue_microtask(ts_promise_callback_microtask, task);
    } else {
        callbacks.push_back(cb);
    }
    return cb.nextPromise;
}

void TsPromise::then_async(AsyncContext* asyncCtx) {
    handled = true;
    Callback cb;
    cb.onFulfilled = TsValue();
    cb.onRejected = TsValue();
    cb.onFinally = TsValue();
    cb.nextPromise = nullptr;
    cb.asyncCtx = asyncCtx;

    if (state != PromiseState::Pending) {
        auto task = static_cast<PromiseCallbackTask*>(ts_alloc(sizeof(PromiseCallbackTask)));
        task->promise = this;
        task->cb = cb;

        ts_queue_microtask(ts_promise_callback_microtask, task);
    } else {
        callbacks.push_back(cb);
    }
}

static TsValue* ts_promise_then_wrapper(void* context, TsValue* onFulfilled, TsValue* onRejected) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->then(onFulfilled ? nanbox_to_tagged(onFulfilled) : TsValue(), onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

static TsValue* ts_promise_catch_wrapper(void* context, TsValue* onRejected) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->catch_error(onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

static TsValue* ts_promise_finally_wrapper(void* context, TsValue* onFinally) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->finally(onFinally ? nanbox_to_tagged(onFinally) : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_get_property(void* obj, void* propName) {
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();
    
    if (strcmp(name, "then") == 0) {
        return ts_value_make_function((void*)ts_promise_then_wrapper, obj);
    } else if (strcmp(name, "catch") == 0) {
        return ts_value_make_function((void*)ts_promise_catch_wrapper, obj);
    } else if (strcmp(name, "finally") == 0) {
        return ts_value_make_function((void*)ts_promise_finally_wrapper, obj);
    }
    return ts_value_make_undefined();
}

TsValue TsPromise::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "then") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_then_wrapper, this, FunctionType::COMPILED, 2);
        return v;
    }
    if (strcmp(key, "catch") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_catch_wrapper, this, FunctionType::COMPILED, 1);
        return v;
    }
    if (strcmp(key, "finally") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_finally_wrapper, this, FunctionType::COMPILED, 1);
        return v;
    }
    return TsObject::GetPropertyVirtual(key);
}

void* TsPromise_VTable[] = {
    nullptr,
    (void*)ts_promise_get_property
};

// ========================================================================
// AsyncContext helper functions for generator state machine
// ========================================================================

void ts_async_context_set_resume_fn(AsyncContext* ctx, void (*fn)(AsyncContext*)) {
    if (ctx) {
        ctx->resumeFn = fn;
    }
}

int ts_async_context_get_state(AsyncContext* ctx) {
    return ctx ? ctx->state : 0;
}

void ts_async_context_set_state(AsyncContext* ctx, int state) {
    if (ctx) {
        ctx->state = state;
    }
}

void ts_async_context_yield(AsyncContext* ctx, TsValue* value) {
    if (ctx) {
        ctx->yielded = true;
        if (value) {
            ctx->yieldedValue = nanbox_to_tagged(value);
        } else {
            ctx->yieldedValue = TsValue();
        }
    }
}

TsValue* ts_async_context_get_resumed_value(AsyncContext* ctx) {
    if (ctx && ctx->resumedValue) {
        return ctx->resumedValue;
    }
    return ts_value_make_undefined();
}

void ts_async_context_set_delegate_iterator(AsyncContext* ctx, TsValue* iter) {
    if (ctx) {
        ctx->delegateIterator = iter;
    }
}

TsValue* ts_async_context_get_delegate_iterator(AsyncContext* ctx) {
    if (ctx) {
        return ctx->delegateIterator;
    }
    return nullptr;
}

void ts_async_context_set_data(AsyncContext* ctx, void* data) {
    if (ctx) {
        ctx->data = data;
    }
}

void* ts_async_context_get_data(AsyncContext* ctx) {
    if (ctx) {
        return ctx->data;
    }
    return nullptr;
}

} // namespace ts
