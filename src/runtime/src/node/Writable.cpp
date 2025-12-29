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
}
