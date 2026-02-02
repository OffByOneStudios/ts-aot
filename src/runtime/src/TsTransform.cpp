#include "TsTransform.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "GC.h"
#include "TsString.h"
#include <cstring>

// External function call declaration
extern "C" TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv);

TsTransform::TsTransform()
    : TsDuplex(),
      transformCallback_(nullptr),
      flushCallback_(nullptr),
      flushing_(false),
      readableEnded_(false) {
}

TsTransform::~TsTransform() {
}

void TsTransform::SetTransformCallback(void* callback) {
    transformCallback_ = callback;
}

void TsTransform::SetFlushCallback(void* callback) {
    flushCallback_ = callback;
}

bool TsTransform::Write(void* data, size_t length) {
    if (TsWritable::destroyed_ || TsWritable::ended_) {
        return false;
    }

    // Call the transform callback if set
    if (transformCallback_) {
        DoTransform(data);
    } else {
        // Default passthrough behavior - just push the data
        Push(data);
    }

    return true;
}

void TsTransform::End() {
    if (TsWritable::ended_) return;

    TsDuplex::End();

    // Call flush callback if set
    if (flushCallback_ && !flushing_) {
        DoFlush();
    } else {
        // No flush callback, just end the readable side
        PushNull();
    }
}

void TsTransform::Push(void* data) {
    if (readableEnded_) return;

    // Emit 'data' event to push data to readable side
    void* args[] = { data };
    Emit("data", 1, args);
}

void TsTransform::PushNull() {
    if (readableEnded_) return;
    readableEnded_ = true;
    TsReadable::ended_ = true;
    TsWritable::finished_ = true;

    Emit("end", 0, nullptr);
}

void TsTransform::DoTransform(void* chunk) {
    if (!transformCallback_) return;

    // The transform callback receives (chunk, encoding, callback)
    // For now we pass the chunk and null for encoding and callback
    TsValue* chunkVal = (TsValue*)chunk;
    TsValue* nullVal = ts_value_make_undefined();
    TsValue* args[] = { chunkVal, nullVal, nullVal };

    ts_function_call((TsValue*)transformCallback_, 3, args);
}

void TsTransform::DoFlush() {
    if (!flushCallback_ || flushing_) return;
    flushing_ = true;

    // Flush callback receives (callback)
    TsValue* nullVal = ts_value_make_undefined();
    TsValue* args[] = { nullVal };

    ts_function_call((TsValue*)flushCallback_, 1, args);

    // After flush, end the readable stream
    PushNull();
}

// extern "C" wrappers moved to extensions/node/stream/src/stream.cpp
#if 0  // Moved to ts_stream extension
// C API implementations
extern "C" {

void* ts_stream_transform_create(void* options) {
    void* mem = ts_alloc(sizeof(TsTransform));
    TsTransform* transform = new (mem) TsTransform();

    // If options contains transform/flush callbacks, set them
    // Options handling would go here

    return transform;
}

void ts_stream_transform_push(void* transformPtr, void* data) {
    void* rawPtr = ts_value_get_object((TsValue*)transformPtr);
    if (!rawPtr) rawPtr = transformPtr;

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->Push(data);
}

void ts_stream_transform_push_null(void* transformPtr) {
    void* rawPtr = ts_value_get_object((TsValue*)transformPtr);
    if (!rawPtr) rawPtr = transformPtr;

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->PushNull();
}

void ts_stream_transform_set_transform(void* transformPtr, void* callback) {
    void* rawPtr = ts_value_get_object((TsValue*)transformPtr);
    if (!rawPtr) rawPtr = transformPtr;

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->SetTransformCallback(callback);
}

void ts_stream_transform_set_flush(void* transformPtr, void* callback) {
    void* rawPtr = ts_value_get_object((TsValue*)transformPtr);
    if (!rawPtr) rawPtr = transformPtr;

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->SetFlushCallback(callback);
}

} // extern "C"
#endif  // Moved to ts_stream extension
