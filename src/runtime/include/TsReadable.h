#pragma once
#include "TsEventEmitter.h"
#include "TsArray.h"

class TsWritable;  // Forward declaration for pipe targets

class TsReadable : public virtual TsEventEmitter {
public:
    TsReadable() : flowing(false), reading(false), destroyed_(false),
                   ended_(false), paused_(true), pipeDest_(nullptr),
                   highWaterMark_(16384), objectMode_(false), aborted_(false),
                   didRead_(false), encoding_(nullptr), unshiftBuffer_(nullptr) {}  // Default 16KB highWaterMark
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
    int64_t GetHighWaterMark() const { return highWaterMark_; }
    void SetHighWaterMark(int64_t hwm) { highWaterMark_ = hwm; }
    virtual int64_t GetReadableLength() const { return 0; }  // No internal buffer by default
    bool IsObjectMode() const { return objectMode_; }
    void SetObjectMode(bool mode) { objectMode_ = mode; }
    bool IsReadableAborted() const { return aborted_; }
    void SetReadableAborted(bool aborted) { aborted_ = aborted; }
    bool IsReadableDidRead() const { return didRead_; }
    void SetReadableDidRead(bool didRead) { didRead_ = didRead; }

    // Encoding support
    const char* GetEncoding() const { return encoding_; }
    TsReadable* SetEncoding(const char* encoding) {
        encoding_ = encoding;
        return this;  // For chaining
    }

    // Pipe management
    void SetPipeDest(TsWritable* dest) { pipeDest_ = dest; }
    TsWritable* GetPipeDest() const { return pipeDest_; }
    void Unpipe() { pipeDest_ = nullptr; }

    // Unshift - push data back to the front of the internal buffer
    // This allows reading code to "un-consume" data if it doesn't need it
    void Unshift(void* chunk) {
        if (!chunk) return;
        // Create unshift buffer if it doesn't exist
        if (!unshiftBuffer_) {
            unshiftBuffer_ = TsArray::Create();
        }
        // Insert at the front (unshift semantics)
        // Store pointer as int64_t (TsArray stores int64_t values)
        unshiftBuffer_->Unshift(reinterpret_cast<int64_t>(chunk));

        // Mark that we've read from this stream
        didRead_ = true;
    }

    // Get the next chunk from the unshift buffer (returns nullptr if empty)
    void* GetUnshiftedChunk() {
        if (!unshiftBuffer_ || unshiftBuffer_->Length() == 0) {
            return nullptr;
        }
        // Remove and return the first element (shift)
        // Convert int64_t back to pointer
        return reinterpret_cast<void*>(unshiftBuffer_->Shift());
    }

    // Check if there are unshifted chunks
    bool HasUnshiftedData() const { return unshiftBuffer_ && unshiftBuffer_->Length() > 0; }

protected:
    bool flowing;
    bool reading;
    bool destroyed_;
    bool ended_;
    bool paused_;
    TsWritable* pipeDest_;
    int64_t highWaterMark_;
    bool objectMode_;
    bool aborted_;
    bool didRead_;
    const char* encoding_;
    TsArray* unshiftBuffer_;  // Buffer for unshifted data (used as a deque)
};

// C API for stream properties
extern "C" {
    bool ts_readable_destroyed(void* stream);
    bool ts_readable_readable(void* stream);
    bool ts_readable_is_paused(void* stream);
    bool ts_readable_readable_ended(void* stream);
    bool ts_readable_readable_flowing(void* stream);
    void ts_readable_unpipe(void* stream);
    int64_t ts_readable_readable_high_water_mark(void* stream);
    int64_t ts_readable_readable_length(void* stream);
    bool ts_readable_readable_object_mode(void* stream);
    bool ts_readable_readable_aborted(void* stream);
    bool ts_readable_readable_did_read(void* stream);

    // Readable.from() - create a readable stream from an iterable (array)
    void* ts_readable_from(void* iterable);

    // Set encoding - returns the stream for chaining
    void* ts_readable_set_encoding(void* stream, void* encoding);
    // Get encoding - returns null if not set
    void* ts_readable_readable_encoding(void* stream);

    // Unshift - push data back to the front of the internal buffer
    void ts_readable_unshift(void* stream, void* chunk);

    // read() - pull data from the internal buffer (paused mode)
    void* ts_readable_read(void* stream, int64_t size);

    // wrap() - wrap an old-style stream in a Readable class
    void* ts_readable_wrap(void* readable, void* oldStream);
}
