#pragma once
#include "TsEventEmitter.h"

class TsWritable : public virtual TsEventEmitter {
public:
    TsWritable() : closed(false), bufferedAmount(0), highWaterMark(16384), needDrain(false),
                   destroyed_(false), ended_(false), finished_(false), objectMode_(false),
                   corked_(false) {}
    virtual ~TsWritable() {}

    // Safe casting helper
    virtual TsWritable* AsWritable() override { return this; }

    virtual bool Write(void* data, size_t length) = 0;
    virtual void End() {
        if (ended_) return;
        ended_ = true;
    }

    // State methods
    virtual void Destroy() {
        if (destroyed_) return;
        destroyed_ = true;
        Emit("close", 0, nullptr);
    }

    // State property getters
    bool IsDestroyed() const { return destroyed_; }
    bool IsWritable() const { return !destroyed_ && !ended_; }
    bool IsWritableEnded() const { return ended_; }
    bool IsWritableFinished() const { return finished_; }
    bool IsWritableNeedDrain() const { return needDrain; }
    size_t GetWritableHighWaterMark() const { return highWaterMark; }
    size_t GetWritableLength() const { return bufferedAmount; }
    bool IsObjectMode() const { return objectMode_; }
    void SetObjectMode(bool mode) { objectMode_ = mode; }

    // Cork/Uncork methods
    void Cork() { corked_ = true; }
    void Uncork() { corked_ = false; }
    bool IsCorked() const { return corked_; }

protected:
    bool closed;
    size_t bufferedAmount;
    size_t highWaterMark;
    bool needDrain;
    bool destroyed_;
    bool ended_;
    bool finished_;
    bool objectMode_;
    bool corked_;
};

// C API for stream properties
extern "C" {
    bool ts_writable_destroyed(void* stream);
    bool ts_writable_writable(void* stream);
    bool ts_writable_writable_ended(void* stream);
    bool ts_writable_writable_finished(void* stream);
    bool ts_writable_writable_need_drain(void* stream);
    int64_t ts_writable_writable_high_water_mark(void* stream);
    int64_t ts_writable_writable_length(void* stream);
    bool ts_writable_writable_object_mode(void* stream);
    void ts_writable_cork(void* stream);
    void ts_writable_uncork(void* stream);
}
