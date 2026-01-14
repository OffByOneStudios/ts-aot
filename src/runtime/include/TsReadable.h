#pragma once
#include "TsEventEmitter.h"

class TsWritable;  // Forward declaration for pipe targets

class TsReadable : public virtual TsEventEmitter {
public:
    TsReadable() : flowing(false), reading(false), destroyed_(false),
                   ended_(false), paused_(true), pipeDest_(nullptr) {}
    virtual ~TsReadable() {}

    // Safe casting helper
    virtual TsReadable* AsReadable() override { return this; }

    // State methods
    virtual void Pause() {
        flowing = false;
        paused_ = true;
    }
    virtual void Resume() {
        flowing = true;
        paused_ = false;
    }
    virtual void Destroy() {
        if (destroyed_) return;
        destroyed_ = true;
        Emit("close", 0, nullptr);
    }

    // State property getters
    bool IsDestroyed() const { return destroyed_; }
    bool IsReadable() const { return !destroyed_ && !ended_; }
    bool IsPaused() const { return paused_; }
    bool IsReadableEnded() const { return ended_; }
    bool IsReadableFlowing() const { return flowing; }

    // Pipe management
    void SetPipeDest(TsWritable* dest) { pipeDest_ = dest; }
    TsWritable* GetPipeDest() const { return pipeDest_; }
    void Unpipe() { pipeDest_ = nullptr; }

protected:
    bool flowing;
    bool reading;
    bool destroyed_;
    bool ended_;
    bool paused_;
    TsWritable* pipeDest_;
};

// C API for stream properties
extern "C" {
    bool ts_readable_destroyed(void* stream);
    bool ts_readable_readable(void* stream);
    bool ts_readable_is_paused(void* stream);
    bool ts_readable_readable_ended(void* stream);
    bool ts_readable_readable_flowing(void* stream);
    void ts_readable_unpipe(void* stream);
}
