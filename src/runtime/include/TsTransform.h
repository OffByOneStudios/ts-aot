#pragma once
#include "TsDuplex.h"

/**
 * TsTransform - Transform stream implementation
 *
 * Transform streams are Duplex streams where the output is computed from the input.
 * They implement _transform() which is called for each chunk of data written.
 */
class TsTransform : public TsDuplex {
public:
    TsTransform();
    virtual ~TsTransform();

    // Safe casting helper
    virtual TsTransform* AsTransform() override { return this; }

    // Duplex/Writable implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    // Set the transform and flush callbacks (from JS)
    void SetTransformCallback(void* callback);
    void SetFlushCallback(void* callback);

    // Called by _transform implementation to push data to readable side
    void Push(void* data);
    void PushNull();  // Signal end of readable stream

protected:
    void* transformCallback_;  // User-provided _transform function
    void* flushCallback_;      // User-provided _flush function
    bool flushing_;
    bool readableEnded_;

    // Internal transform processing
    void DoTransform(void* chunk);
    void DoFlush();
};

// C API
extern "C" {
    void* ts_stream_transform_create(void* options);
    void ts_stream_transform_push(void* transform, void* data);
    void ts_stream_transform_push_null(void* transform);
    void ts_stream_transform_set_transform(void* transform, void* callback);
    void ts_stream_transform_set_flush(void* transform, void* callback);
}
