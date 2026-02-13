// TsReadline.cpp - Node.js readline module implementation
// Provides line-by-line reading from readable streams

#include "TsReadline.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsString.h"
#include "TsRuntime.h"
#include "TsPromise.h"
#include "GC.h"
#include <cstring>
#include <cstdio>

extern "C" TsValue* ts_object_get_property(void* obj, const char* keyStr);
extern "C" void* ts_value_get_object(TsValue* val);

// Helper to create iterator result object {value, done}
static TsValue* create_readline_iter_result(TsValue* value, bool done) {
    TsMap* result = TsMap::Create();
    if (value) {
        result->Set(TsString::Create("value"), *value);
    } else {
        TsValue undef;
        undef.type = ValueType::UNDEFINED;
        undef.i_val = 0;
        result->Set(TsString::Create("value"), undef);
    }
    TsValue doneVal;
    doneVal.type = ValueType::BOOLEAN;
    doneVal.i_val = done ? 1 : 0;
    result->Set(TsString::Create("done"), doneVal);
    return ts_value_make_object(result);
}

namespace ts {

TsReadlineInterface::TsReadlineInterface() : TsEventEmitter() {
    magic = MAGIC;
    prompt_ = TsString::Create("> ");
    lineBuffer_ = TsString::Create("");
}

TsReadlineInterface* TsReadlineInterface::Create() {
    void* mem = ts_alloc(sizeof(TsReadlineInterface));
    return new (mem) TsReadlineInterface();
}

void TsReadlineInterface::SetInput(TsReadable* input) {
    input_ = input;
}

void TsReadlineInterface::SetOutput(TsWritable* output) {
    output_ = output;
}

void TsReadlineInterface::Close() {
    if (closed_) return;
    closed_ = true;

    // Notify async iterators that we're closing
    for (TsReadlineAsyncIterator* iter : asyncIterators_) {
        iter->OnClose();
    }

    // Emit 'close' event
    Emit("close", 0, nullptr);
}

void TsReadlineInterface::Pause() {
    if (paused_ || closed_) return;
    paused_ = true;

    if (input_) {
        input_->Pause();
    }

    // Emit 'pause' event
    Emit("pause", 0, nullptr);
}

void TsReadlineInterface::Resume() {
    if (!paused_ || closed_) return;
    paused_ = false;

    if (input_) {
        input_->Resume();
    }

    // Emit 'resume' event
    Emit("resume", 0, nullptr);
}

void TsReadlineInterface::Prompt(bool preserveCursor) {
    if (closed_ || !output_) return;

    // Write the prompt to output
    if (prompt_) {
        const char* promptStr = prompt_->ToUtf8();
        // Create a TsValue for the write call
        TsString* promptToWrite = TsString::Create(promptStr);
        void* args[] = { promptToWrite };

        // Call output's write method if available
        // For now, we'll emit directly to stdout via console
        ts_console_log(promptToWrite);
    }
}

void TsReadlineInterface::SetPrompt(const char* prompt) {
    if (prompt) {
        prompt_ = TsString::Create(prompt);
    }
}

void TsReadlineInterface::Question(const char* query, void* callback) {
    if (closed_) return;

    questionPending_ = true;
    questionCallback_ = callback;

    // Display the query (acts as a one-time prompt)
    if (output_ && query) {
        ts_console_log(TsString::Create(query));
    }

    // The answer will come via OnData when the user enters a line
}

void TsReadlineInterface::Write(const char* data) {
    if (closed_ || !output_ || !data) return;

    // Write to output stream
    ts_console_log(TsString::Create(data));
}

void TsReadlineInterface::OnData(void* chunk) {
    if (closed_) return;

    // Get the chunk as a string
    TsString* chunkStr = nullptr;
    if (chunk) {
        void* rawChunk = ts_nanbox_safe_unbox(chunk);
        chunkStr = dynamic_cast<TsString*>((TsObject*)rawChunk);
    }

    if (!chunkStr) return;

    // Append to line buffer
    const char* existing = lineBuffer_ ? lineBuffer_->ToUtf8() : "";
    const char* newData = chunkStr->ToUtf8();

    size_t existingLen = strlen(existing);
    size_t newLen = strlen(newData);

    char* combined = (char*)ts_alloc(existingLen + newLen + 1);
    strcpy(combined, existing);
    strcat(combined, newData);

    lineBuffer_ = TsString::Create(combined);

    // Process the buffer for complete lines
    ProcessBuffer();
}

void TsReadlineInterface::ProcessBuffer() {
    if (!lineBuffer_) return;

    const char* buffer = lineBuffer_->ToUtf8();
    const char* lineStart = buffer;
    const char* pos = buffer;

    while (*pos) {
        // Look for \n or \r\n
        if (*pos == '\n') {
            // Found end of line
            size_t lineLen = pos - lineStart;

            // Handle \r\n (remove trailing \r)
            if (lineLen > 0 && *(pos - 1) == '\r') {
                lineLen--;
            }

            // Create the line string
            char* line = (char*)ts_alloc(lineLen + 1);
            strncpy(line, lineStart, lineLen);
            line[lineLen] = '\0';

            TsString* lineStr = TsString::Create(line);

            // If we have a pending question, call its callback
            if (questionPending_ && questionCallback_) {
                questionPending_ = false;
                void* cb = questionCallback_;
                questionCallback_ = nullptr;

                // Call the callback with the line
                TsValue* lineArg = ts_value_make_string(lineStr);
                ts_call_1((TsValue*)cb, lineArg);
            } else {
                // Notify async iterators
                for (TsReadlineAsyncIterator* iter : asyncIterators_) {
                    iter->OnLine(lineStr);
                }

                // Emit 'line' event
                void* args[] = { ts_value_make_string(lineStr) };
                Emit("line", 1, args);
            }

            lineStart = pos + 1;
        }
        pos++;
    }

    // Keep any remaining incomplete line in the buffer
    if (lineStart != buffer) {
        lineBuffer_ = TsString::Create(lineStart);
    }
}

void TsReadlineInterface::OnInputEnd() {
    if (closed_) return;

    // Process any remaining data in the buffer
    if (lineBuffer_) {
        const char* remaining = lineBuffer_->ToUtf8();
        if (remaining && strlen(remaining) > 0) {
            TsString* lineStr = TsString::Create(remaining);

            if (questionPending_ && questionCallback_) {
                questionPending_ = false;
                void* cb = questionCallback_;
                questionCallback_ = nullptr;

                TsValue* lineArg = ts_value_make_string(lineStr);
                ts_call_1((TsValue*)cb, lineArg);
            } else {
                void* args[] = { ts_value_make_string(lineStr) };
                Emit("line", 1, args);
            }
        }
    }

    Close();
}

// ============================================================================
// TsReadlineAsyncIterator implementation
// ============================================================================

TsReadlineAsyncIterator::TsReadlineAsyncIterator(TsReadlineInterface* rl)
    : TsObject(), rl_(rl) {
    magic = MAGIC;
}

TsReadlineAsyncIterator* TsReadlineAsyncIterator::Create(TsReadlineInterface* rl) {
    void* mem = ts_alloc(sizeof(TsReadlineAsyncIterator));
    return new (mem) TsReadlineAsyncIterator(rl);
}

TsPromise* TsReadlineAsyncIterator::Next() {
    TsPromise* promise = ts_promise_create();

    if (closed_) {
        // Already closed, return done immediately
        TsValue* result = create_readline_iter_result(nullptr, true);
        ts_promise_resolve_internal(promise, result);
        return promise;
    }

    if (!pendingLines_.empty()) {
        // We have a line available, resolve immediately
        TsString* line = pendingLines_.front();
        pendingLines_.erase(pendingLines_.begin());

        TsValue* lineVal = ts_value_make_string(line);
        TsValue* result = create_readline_iter_result(lineVal, false);
        ts_promise_resolve_internal(promise, result);
        return promise;
    }

    // No line available yet, queue the promise
    pendingPromises_.push_back(promise);
    return promise;
}

void TsReadlineAsyncIterator::OnLine(TsString* line) {
    if (closed_) return;

    if (!pendingPromises_.empty()) {
        // We have a waiting promise, resolve it
        TsPromise* promise = pendingPromises_.front();
        pendingPromises_.erase(pendingPromises_.begin());

        TsValue* lineVal = ts_value_make_string(line);
        TsValue* result = create_readline_iter_result(lineVal, false);
        ts_promise_resolve_internal(promise, result);
    } else {
        // No waiting promise, queue the line
        pendingLines_.push_back(line);
    }
}

void TsReadlineAsyncIterator::OnClose() {
    closed_ = true;

    // Resolve all pending promises with done=true
    for (TsPromise* promise : pendingPromises_) {
        TsValue* result = create_readline_iter_result(nullptr, true);
        ts_promise_resolve_internal(promise, result);
    }
    pendingPromises_.clear();
}

// ============================================================================
// TsReadlineInterface async iterator and signal support
// ============================================================================

TsReadlineAsyncIterator* TsReadlineInterface::GetAsyncIterator() {
    TsReadlineAsyncIterator* iter = TsReadlineAsyncIterator::Create(this);
    asyncIterators_.push_back(iter);
    return iter;
}

void TsReadlineInterface::EmitSIGINT() {
    Emit("SIGINT", 0, nullptr);
}

void TsReadlineInterface::EmitSIGTSTP() {
    // SIGTSTP typically pauses the process
    Emit("SIGTSTP", 0, nullptr);
}

void TsReadlineInterface::EmitSIGCONT() {
    // SIGCONT typically resumes after SIGTSTP
    Emit("SIGCONT", 0, nullptr);
}

void TsReadlineInterface::AddToHistory(TsString* line) {
    if (!line) return;

    // Add to history
    history_.push_back(line);

    // Emit 'history' event with the history array
    // Node.js passes the history array
    TsArray* histArray = TsArray::Create();
    for (TsString* h : history_) {
        histArray->Push((int64_t)ts_value_make_string(h));
    }
    void* args[] = { ts_value_make_object(histArray) };
    Emit("history", 1, args);
}

}  // namespace ts

