// TsReadline.cpp - Node.js readline module implementation
// Provides line-by-line reading from readable streams

#include "TsReadline.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsString.h"
#include "TsRuntime.h"
#include "GC.h"
#include <cstring>
#include <cstdio>

extern "C" TsValue* ts_object_get_property(void* obj, const char* keyStr);
extern "C" void* ts_value_get_object(TsValue* val);

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
        void* rawChunk = ts_value_get_object((TsValue*)chunk);
        if (!rawChunk) rawChunk = chunk;
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

}  // namespace ts

extern "C" {

void* ts_readline_create_interface(void* options) {
    ts::TsReadlineInterface* rl = ts::TsReadlineInterface::Create();

    if (options) {
        void* rawOpts = ts_value_get_object((TsValue*)options);
        if (!rawOpts) rawOpts = options;

        // Get input stream
        TsValue* inputVal = ts_object_get_property(rawOpts, "input");
        if (inputVal && inputVal->type == ValueType::OBJECT_PTR) {
            void* rawInput = ts_value_get_object(inputVal);
            if (!rawInput) rawInput = inputVal->ptr_val;
            TsReadable* input = dynamic_cast<TsReadable*>(((TsObject*)rawInput)->AsReadable());
            if (input) {
                rl->SetInput(input);
            }
        }

        // Get output stream
        TsValue* outputVal = ts_object_get_property(rawOpts, "output");
        if (outputVal && outputVal->type == ValueType::OBJECT_PTR) {
            void* rawOutput = ts_value_get_object(outputVal);
            if (!rawOutput) rawOutput = outputVal->ptr_val;
            TsWritable* output = dynamic_cast<TsWritable*>(((TsObject*)rawOutput)->AsWritable());
            if (output) {
                rl->SetOutput(output);
            }
        }

        // Get prompt if provided
        TsValue* promptVal = ts_object_get_property(rawOpts, "prompt");
        if (promptVal && promptVal->type == ValueType::STRING_PTR) {
            TsString* promptStr = (TsString*)promptVal->ptr_val;
            if (promptStr) {
                rl->SetPrompt(promptStr->ToUtf8());
            }
        }
    }

    return ts_value_make_object(rl);
}

void ts_readline_close(void* rl) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        iface->Close();
    }
}

void ts_readline_pause(void* rl) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        iface->Pause();
    }
}

void ts_readline_resume(void* rl) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        iface->Resume();
    }
}

void ts_readline_prompt(void* rl, void* preserveCursor) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        bool preserve = false;
        if (preserveCursor) {
            preserve = ts_value_get_bool((TsValue*)preserveCursor);
        }
        iface->Prompt(preserve);
    }
}

void ts_readline_set_prompt(void* rl, void* prompt) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface && prompt) {
        void* rawPrompt = ts_value_get_object((TsValue*)prompt);
        TsString* promptStr = nullptr;
        if (rawPrompt) {
            promptStr = dynamic_cast<TsString*>((TsObject*)rawPrompt);
        }
        if (!promptStr) {
            TsValue* val = (TsValue*)prompt;
            if (val->type == ValueType::STRING_PTR) {
                promptStr = (TsString*)val->ptr_val;
            }
        }
        if (promptStr) {
            iface->SetPrompt(promptStr->ToUtf8());
        }
    }
}

void ts_readline_question(void* rl, void* query, void* callback) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        const char* queryStr = "";
        if (query) {
            void* rawQuery = ts_value_get_object((TsValue*)query);
            TsString* qStr = nullptr;
            if (rawQuery) {
                qStr = dynamic_cast<TsString*>((TsObject*)rawQuery);
            }
            if (!qStr) {
                TsValue* val = (TsValue*)query;
                if (val->type == ValueType::STRING_PTR) {
                    qStr = (TsString*)val->ptr_val;
                }
            }
            if (qStr) {
                queryStr = qStr->ToUtf8();
            }
        }
        iface->Question(queryStr, callback);
    }
}

void ts_readline_write(void* rl, void* data) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface && data) {
        void* rawData = ts_value_get_object((TsValue*)data);
        TsString* dataStr = nullptr;
        if (rawData) {
            dataStr = dynamic_cast<TsString*>((TsObject*)rawData);
        }
        if (!dataStr) {
            TsValue* val = (TsValue*)data;
            if (val->type == ValueType::STRING_PTR) {
                dataStr = (TsString*)val->ptr_val;
            }
        }
        if (dataStr) {
            iface->Write(dataStr->ToUtf8());
        }
    }
}

void* ts_readline_get_line(void* rl) {
    if (!rl) return ts_value_make_string(TsString::Create(""));

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        TsString* line = iface->GetLine();
        return ts_value_make_string(line ? line : TsString::Create(""));
    }
    return ts_value_make_string(TsString::Create(""));
}

int64_t ts_readline_get_cursor(void* rl) {
    if (!rl) return 0;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        return iface->GetCursor();
    }
    return 0;
}

void* ts_readline_get_prompt(void* rl) {
    if (!rl) return TsString::Create("");

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        TsString* prompt = iface->GetPrompt();
        return prompt ? prompt : TsString::Create("");
    }
    return TsString::Create("");
}

// Utility functions - ANSI escape sequences for cursor control

void ts_readline_clear_line(void* stream, void* dir) {
    // dir: -1 = left, 0 = entire line, 1 = right
    int direction = 0;
    if (dir) {
        direction = (int)ts_value_get_int((TsValue*)dir);
    }

    const char* escSeq;
    switch (direction) {
        case -1: escSeq = "\x1b[1K"; break;  // Clear to left
        case 1:  escSeq = "\x1b[0K"; break;  // Clear to right
        default: escSeq = "\x1b[2K"; break;  // Clear entire line
    }

    // Write escape sequence directly to stdout
    printf("%s", escSeq);
    fflush(stdout);
}

void ts_readline_clear_screen_down(void* stream) {
    // Clear from cursor to end of screen
    printf("\x1b[0J");
    fflush(stdout);
}

void ts_readline_cursor_to(void* stream, void* x, void* y) {
    int xPos = 0;
    int yPos = -1;  // -1 means don't move vertically

    if (x) {
        xPos = (int)ts_value_get_int((TsValue*)x);
    }
    if (y) {
        yPos = (int)ts_value_get_int((TsValue*)y);
    }

    if (yPos >= 0) {
        // Move to absolute position (1-based in ANSI)
        printf("\x1b[%d;%dH", yPos + 1, xPos + 1);
    } else {
        // Move to column only
        printf("\x1b[%dG", xPos + 1);
    }
    fflush(stdout);
}

void ts_readline_move_cursor(void* stream, void* dx, void* dy) {
    int deltaX = 0;
    int deltaY = 0;

    if (dx) {
        deltaX = (int)ts_value_get_int((TsValue*)dx);
    }
    if (dy) {
        deltaY = (int)ts_value_get_int((TsValue*)dy);
    }

    // Move horizontally
    if (deltaX > 0) {
        printf("\x1b[%dC", deltaX);  // Move right
    } else if (deltaX < 0) {
        printf("\x1b[%dD", -deltaX);  // Move left
    }

    // Move vertically
    if (deltaY > 0) {
        printf("\x1b[%dB", deltaY);  // Move down
    } else if (deltaY < 0) {
        printf("\x1b[%dA", -deltaY);  // Move up
    }
    fflush(stdout);
}

void ts_readline_emit_keypress_events(void* stream, void* iface) {
    // Stub: In a full implementation, this would set up the stream to emit
    // 'keypress' events with { sequence, name, ctrl, meta, shift } info.
    // For now, this is a no-op since we don't have full terminal handling.
    (void)stream;
    (void)iface;
}

}  // extern "C"
