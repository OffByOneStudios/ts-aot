#include "TsWritable.h"
#include "TsEventEmitter.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <string.h>

extern "C" {
    bool ts_writable_write(void* writable, TsValue* v) {
        if (!writable || !v) return false;
        
        // The pointer may be TsEventEmitter* (due to virtual inheritance),
        // so we use dynamic_cast to get TsWritable*
        TsEventEmitter* emitter = (TsEventEmitter*)writable;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) {
            // Try direct cast (for non-virtual cases)
            w = (TsWritable*)writable;
        }
        
        if (v->type == ValueType::STRING_PTR) {
            TsString* str = (TsString*)v->ptr_val;
            return w->Write((void*)str->ToUtf8(), str->Length());
        } else if (v->type == ValueType::OBJECT_PTR) {
            TsObject* obj = (TsObject*)v->ptr_val;
            if (obj && *(uint32_t*)obj == 0x42554646) { // TsBuffer::MAGIC
                TsBuffer* buf = (TsBuffer*)obj;
                return w->Write(buf->GetData(), buf->GetLength());
            }
        }
        return false;
    }

    __declspec(noinline) void ts_writable_end(void* writable, TsValue* data) {
        if (!writable) {
            return;
        }

        // Check if writable is actually a TsObject with magic
        TsObject* obj = (TsObject*)writable;

        // The pointer may be TsEventEmitter* (due to virtual inheritance),
        // so we use dynamic_cast to get TsWritable*
        TsEventEmitter* emitter = (TsEventEmitter*)writable;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) {
            // Fallback to direct cast (for non-virtual cases)
            w = (TsWritable*)writable;
        }

        if (data && data->type != ValueType::UNDEFINED) {
            ts_writable_write(writable, data);
        }
        w->End();
    }

    // Stream state property accessors
    bool ts_writable_destroyed(void* stream) {
        if (!stream) return true;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return true;
        return w->IsDestroyed();
    }

    bool ts_writable_writable(void* stream) {
        if (!stream) return false;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return false;
        return w->IsWritable();
    }

    bool ts_writable_writable_ended(void* stream) {
        if (!stream) return true;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return true;
        return w->IsWritableEnded();
    }

    bool ts_writable_writable_finished(void* stream) {
        if (!stream) return true;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return true;
        return w->IsWritableFinished();
    }

    bool ts_writable_writable_need_drain(void* stream) {
        if (!stream) return false;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return false;
        return w->IsWritableNeedDrain();
    }

    int64_t ts_writable_writable_high_water_mark(void* stream) {
        if (!stream) return 0;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return 0;
        return (int64_t)w->GetWritableHighWaterMark();
    }

    int64_t ts_writable_writable_length(void* stream) {
        if (!stream) return 0;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return 0;
        return (int64_t)w->GetWritableLength();
    }

    bool ts_writable_writable_object_mode(void* stream) {
        if (!stream) return false;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return false;
        return w->IsObjectMode();
    }

    void ts_writable_cork(void* stream) {
        if (!stream) return;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (w) w->Cork();
    }

    void ts_writable_uncork(void* stream) {
        if (!stream) return;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (w) w->Uncork();
    }

    bool ts_writable_writable_aborted(void* stream) {
        if (!stream) return false;
        TsEventEmitter* emitter = (TsEventEmitter*)stream;
        TsWritable* w = dynamic_cast<TsWritable*>(emitter);
        if (!w) return false;
        return w->IsWritableAborted();
    }

    void* ts_writable_set_default_encoding(void* stream, void* encoding) {
        if (!stream) return nullptr;

        // Unbox stream if needed
        void* rawStream = ts_value_get_object((TsValue*)stream);
        if (!rawStream) rawStream = stream;

        TsWritable* w = dynamic_cast<TsWritable*>((TsEventEmitter*)rawStream);
        if (!w) return stream;  // Return original for chaining

        // Get encoding string
        TsString* encStr = nullptr;
        void* rawEnc = ts_value_get_string((TsValue*)encoding);
        if (rawEnc) {
            encStr = (TsString*)rawEnc;
        } else if (encoding) {
            encStr = (TsString*)encoding;
        }

        if (encStr) {
            w->SetDefaultEncoding(encStr->ToUtf8());
        }

        return stream;  // Return for chaining
    }
}
