// TsReadline.h - Node.js readline module implementation
// Provides line-by-line reading from readable streams

#ifndef TS_READLINE_H
#define TS_READLINE_H

#include "TsObject.h"
#include "TsEventEmitter.h"
#include "TsString.h"

class TsReadable;
class TsWritable;

namespace ts {

// Readline Interface class
// Transforms a readable stream into line-oriented events
class TsReadlineInterface : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x524C494E; // "RLIN"

    static TsReadlineInterface* Create();

    // Configure the interface with input/output streams
    void SetInput(TsReadable* input);
    void SetOutput(TsWritable* output);

    // Core methods
    void Close();
    void Pause();
    void Resume();
    void Prompt(bool preserveCursor = false);
    void SetPrompt(const char* prompt);
    void Question(const char* query, void* callback);
    void Write(const char* data);

    // Property getters
    TsString* GetLine() const { return lineBuffer_; }
    int64_t GetCursor() const { return cursorPos_; }
    bool IsClosed() const { return closed_; }
    bool IsPaused() const { return paused_; }

    // Internal: called when input emits 'data'
    void OnData(void* chunk);

    // Internal: called when input emits 'end' or 'close'
    void OnInputEnd();

private:
    TsReadlineInterface();

    // Process accumulated input, emit 'line' events for complete lines
    void ProcessBuffer();

    TsReadable* input_ = nullptr;
    TsWritable* output_ = nullptr;
    TsString* prompt_ = nullptr;
    TsString* lineBuffer_ = nullptr;

    bool closed_ = false;
    bool paused_ = false;
    bool questionPending_ = false;
    void* questionCallback_ = nullptr;

    int64_t cursorPos_ = 0;

    // Track if we've set up the input listener
    bool inputListenerSet_ = false;
};

}  // namespace ts

extern "C" {

// Create a readline interface from options object
// options: { input: Readable, output?: Writable, terminal?: boolean, ... }
void* ts_readline_create_interface(void* options);

// Close the interface
void ts_readline_close(void* rl);

// Pause the input stream
void ts_readline_pause(void* rl);

// Resume the input stream
void ts_readline_resume(void* rl);

// Display the prompt
void ts_readline_prompt(void* rl, void* preserveCursor);

// Set the prompt string
void ts_readline_set_prompt(void* rl, void* prompt);

// Ask a question and get a response via callback
void ts_readline_question(void* rl, void* query, void* callback);

// Write data to the output stream
void ts_readline_write(void* rl, void* data);

// Property getters
void* ts_readline_get_line(void* rl);
int64_t ts_readline_get_cursor(void* rl);

// Utility functions (module-level)
void ts_readline_clear_line(void* stream, void* dir);
void ts_readline_clear_screen_down(void* stream);
void ts_readline_cursor_to(void* stream, void* x, void* y);
void ts_readline_move_cursor(void* stream, void* dx, void* dy);

}

#endif  // TS_READLINE_H
