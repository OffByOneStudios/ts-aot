#include "TsConsString.h"
#include "TsString.h"
#include "GC.h"
#include "TsGC.h"
#include <cstring>
#include <new>
#include <vector>

// Get the stored depth of a string tree node in O(1).
// TsString = depth 0, TsConsString = stored depth (0 if flattened).
static int node_depth(void* node) {
    if (!node) return 0;
    uint32_t m = *(uint32_t*)node;
    if (m == TsString::MAGIC) return 0;
    if (m != TsConsString::MAGIC) return 0;
    TsConsString* cons = (TsConsString*)node;
    if (cons->flattened) return 0;
    return cons->depth;
}

// Get the length of a string-like node without flattening.
static uint32_t node_length(void* node) {
    if (!node) return 0;
    uint32_t m = *(uint32_t*)node;
    if (m == TsString::MAGIC) return (uint32_t)((TsString*)node)->Length();
    if (m == TsConsString::MAGIC) return ((TsConsString*)node)->totalLength;
    return 0;
}

void* TsConsString::Create(void* left, void* right) {
    uint32_t leftLen = node_length(left);
    uint32_t rightLen = node_length(right);
    uint32_t totalLen = leftLen + rightLen;

    // Check depth - if too deep, flatten eagerly (O(1) lookup)
    int ld = node_depth(left);
    int rd = node_depth(right);
    int newDepth = 1 + (ld > rd ? ld : rd);

    if (newDepth > MAX_DEPTH) {
        // Flatten both sides, then do a real concat
        TsString* flatLeft = ts_ensure_flat(left);
        TsString* flatRight = ts_ensure_flat(right);
        // Use a direct concat (will hit the small-string fast path or ICU path)
        // We create the result directly to avoid infinite recursion
        return TsString::Concat(flatLeft, flatRight);
    }

    void* mem = ts_alloc(sizeof(TsConsString));
    TsConsString* cons = new(mem) TsConsString();
    cons->magic = MAGIC;
    cons->totalLength = totalLen;
    cons->left = left;
    cons->right = right;
    cons->flattened = nullptr;
    cons->depth = (uint16_t)newDepth;
    return cons;
}

TsString* TsConsString::Flatten() {
    // Return cached result if already flattened
    if (flattened) return (TsString*)flattened;

    // Iterative in-order traversal to collect all leaf TsString* nodes.
    // We use an explicit stack to avoid recursion (rope can be deep).
    std::vector<void*> stack;
    std::vector<const char*> pieces;
    std::vector<uint32_t> lengths;
    uint32_t totalBytes = 0;

    stack.push_back(this);

    while (!stack.empty()) {
        void* node = stack.back();
        stack.pop_back();

        if (!node) continue;
        uint32_t m = *(uint32_t*)node;

        if (m == TsString::MAGIC) {
            TsString* s = (TsString*)node;
            const char* utf8 = s->ToUtf8();
            uint32_t len = (uint32_t)std::strlen(utf8);
            pieces.push_back(utf8);
            lengths.push_back(len);
            totalBytes += len;
        } else if (m == TsConsString::MAGIC) {
            TsConsString* cons = (TsConsString*)node;
            // If this sub-cons is already flattened, use its cached result
            if (cons->flattened) {
                TsString* s = (TsString*)cons->flattened;
                const char* utf8 = s->ToUtf8();
                uint32_t len = (uint32_t)std::strlen(utf8);
                pieces.push_back(utf8);
                lengths.push_back(len);
                totalBytes += len;
            } else {
                // Push right first so left is processed first (stack is LIFO)
                stack.push_back(cons->right);
                stack.push_back(cons->left);
            }
        }
        // Unknown magic: skip
    }

    // Single allocation + memcpy for all pieces
    char* buf = (char*)ts_alloc(totalBytes + 1);
    char* cursor = buf;
    bool allAscii = true;
    for (size_t i = 0; i < pieces.size(); i++) {
        std::memcpy(cursor, pieces[i], lengths[i]);
        // Check ASCII while copying
        if (allAscii) {
            for (uint32_t j = 0; j < lengths[i]; j++) {
                if ((unsigned char)cursor[j] > 127) {
                    allAscii = false;
                    break;
                }
            }
        }
        cursor += lengths[i];
    }
    *cursor = '\0';

    TsString* result;
    if (allAscii && totalBytes < 64) {
        // Small ASCII: use inline constructor (no ICU needed)
        result = TsString::Create(buf);
    } else if (allAscii) {
        // Large ASCII: create TsString bypassing ICU
        result = TsString::CreateFromAsciiBuffer(buf, totalBytes);
    } else {
        // Non-ASCII: go through Create which uses ICU
        result = TsString::Create(buf);
    }
    flattened = result;
    // Write barrier: this TsConsString (possibly old-gen) now points to result (possibly nursery)
    ts_gc_write_barrier(&flattened, flattened);
    return result;
}

TsString* ts_ensure_flat(void* ptr) {
    if (!ptr) return nullptr;
    uint32_t m = *(uint32_t*)ptr;
    if (m == TsString::MAGIC) return (TsString*)ptr;
    if (m == TsConsString::MAGIC) return ((TsConsString*)ptr)->Flatten();
    return nullptr;
}

int64_t ts_string_like_length(void* ptr) {
    if (!ptr) return 0;
    uint32_t m = *(uint32_t*)ptr;
    if (m == TsString::MAGIC) return ((TsString*)ptr)->Length();
    if (m == TsConsString::MAGIC) return ((TsConsString*)ptr)->totalLength;
    return 0;
}
