// Readline module extension - extern "C" wrappers
// Class implementations stay in tsruntime (TsReadlineInterface inherits from TsEventEmitter)

#include "TsReadlineExt.h"
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

// Async Iterator support

void* ts_readline_get_async_iterator(void* rl) {
    if (!rl) return nullptr;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        ts::TsReadlineAsyncIterator* iter = iface->GetAsyncIterator();
        return ts_value_make_object(iter);
    }
    return nullptr;
}

void* ts_readline_async_iterator_next(void* iter) {
    if (!iter) return nullptr;

    void* rawIter = ts_value_get_object((TsValue*)iter);
    if (!rawIter) rawIter = iter;

    ts::TsReadlineAsyncIterator* asyncIter = dynamic_cast<ts::TsReadlineAsyncIterator*>((TsObject*)rawIter);
    if (asyncIter) {
        ts::TsPromise* promise = asyncIter->Next();
        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        res->type = ValueType::PROMISE_PTR;
        res->ptr_val = promise;
        return res;
    }
    return nullptr;
}

// Signal event emitters

void ts_readline_emit_sigint(void* rl) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        iface->EmitSIGINT();
    }
}

void ts_readline_emit_sigtstp(void* rl) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        iface->EmitSIGTSTP();
    }
}

void ts_readline_emit_sigcont(void* rl) {
    if (!rl) return;

    void* rawRl = ts_value_get_object((TsValue*)rl);
    if (!rawRl) rawRl = rl;

    ts::TsReadlineInterface* iface = dynamic_cast<ts::TsReadlineInterface*>((TsObject*)rawRl);
    if (iface) {
        iface->EmitSIGCONT();
    }
}

}  // extern "C"
