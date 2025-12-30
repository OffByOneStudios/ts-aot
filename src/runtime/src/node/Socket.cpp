#include "TsSocket.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "GC.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include <iostream>
#include <stdio.h>
#include <vector>
#include <string>

TsSocket::TsSocket() : connected(false) {
    this->magic = 0x534F434B; // "SOCK"
    flowing = false;
    reading = false;
    closed = false;
    bufferedAmount = 0;
    highWaterMark = 16384;
    needDrain = false;
    handle = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), handle);
    handle->data = this;
}

TsSocket::TsSocket(uv_tcp_t* h) : handle(h), connected(true) {
    this->magic = 0x534F434B; // "SOCK"
    flowing = false;
    reading = false;
    closed = false;
    bufferedAmount = 0;
    highWaterMark = 16384;
    needDrain = false;
    handle->data = this;
}

TsSocket::~TsSocket() {
    if (!closed) {
        uv_close((uv_handle_t*)handle, OnClose);
    }
}

void TsSocket::Connect(const char* host, int port, void* callback) {
    if (closed) return;

    struct ConnectContext {
        TsSocket* socket;
        void* callback;
        int port;
    };
    ConnectContext* ctx = (ConnectContext*)ts_alloc(sizeof(ConnectContext));
    ctx->socket = this;
    ctx->callback = callback;
    ctx->port = port;

    uv_getaddrinfo_t* dns_req = (uv_getaddrinfo_t*)ts_alloc(sizeof(uv_getaddrinfo_t));
    dns_req->data = ctx;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // Force IPv4 for now (server only binds to 0.0.0.0)
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    sprintf(port_str, "%d", port);

    int r = uv_getaddrinfo(uv_default_loop(), dns_req, [](uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
        ConnectContext* ctx = (ConnectContext*)req->data;
        TsSocket* self = ctx->socket;

        if (status < 0) {
            self->Emit("error", 0, nullptr);
            return;
        }

        uv_connect_t* connect_req = (uv_connect_t*)ts_alloc(sizeof(uv_connect_t));
        connect_req->data = ctx;

        int r = uv_tcp_connect(connect_req, self->handle, res->ai_addr, [](uv_connect_t* req, int status) {
            ConnectContext* ctx = (ConnectContext*)req->data;
            TsSocket* self = ctx->socket;
            
            if (status == 0) {
                self->connected = true;
                self->OnConnected();
                if (ctx->callback) {
                    // TODO: Implement calling JS function from C++
                }
            } else {
                self->Emit("error", 0, nullptr);
            }
        });

        if (r < 0) {
            self->Emit("error", 0, nullptr);
        }

        uv_freeaddrinfo(res);
    }, host, port_str, &hints);

    if (r < 0) {
        Emit("error", 0, nullptr);
    }
}

void TsSocket::OnConnected() {
    Resume();
    Emit("connect", 0, nullptr);
}

void TsSocket::On(const char* event, void* callback) {
    TsEventEmitter::On(event, callback);
    if (strcmp(event, "data") == 0) {
        Resume();
    }
}

void TsSocket::Pause() {
    if (flowing) {
        flowing = false;
        uv_read_stop((uv_stream_t*)handle);
    }
}

void TsSocket::Resume() {
    if (!flowing && connected && !closed) {
        flowing = true;
        uv_read_start((uv_stream_t*)handle, OnAlloc, OnRead);
    }
}

void TsSocket::OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggested_size);
    buf->len = (unsigned int)suggested_size;
}

void TsSocket::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TsSocket* self = (TsSocket*)stream->data;
    self->HandleRead(nread, buf);
}

void TsSocket::HandleRead(ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        TsBuffer* chunk = TsBuffer::Create(nread);
        memcpy(chunk->GetData(), buf->base, nread);
        
        TsValue* arg0 = ts_value_make_object(chunk);
        void* args[] = { arg0 };
        Emit("data", 1, args);
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            Emit("end", 0, nullptr);
        } else {
            Emit("error", 0, nullptr);
        }
        End();
    }
}

struct WriteContext {
    TsSocket* socket;
    size_t length;
};

bool TsSocket::Write(void* data, size_t length) {
    if (closed || !connected) return false;

    bufferedAmount += length;

    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    
    WriteContext* ctx = (WriteContext*)ts_alloc(sizeof(WriteContext));
    ctx->socket = this;
    ctx->length = length;
    req->data = ctx;

    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)length);
    
    uv_write(req, (uv_stream_t*)handle, &buf, 1, OnWrite);

    if (bufferedAmount >= highWaterMark) {
        needDrain = true;
        return false;
    }
    return true;
}

void TsSocket::OnWrite(uv_write_t* req, int status) {
    WriteContext* ctx = (WriteContext*)req->data;
    TsSocket* self = ctx->socket;
    self->HandleWrite(status, ctx->length);
}

void TsSocket::HandleWrite(int status, size_t length) {
    bufferedAmount -= length;
    
    if (needDrain && bufferedAmount < highWaterMark) {
        needDrain = false;
        Emit("drain", 0, nullptr);
    }
}

void TsSocket::End() {
    if (closed) return;
    closed = true;
    uv_close((uv_handle_t*)handle, OnClose);
}

void TsSocket::OnClose(uv_handle_t* handle) {
    TsSocket* self = (TsSocket*)handle->data;
    self->Emit("close", 0, nullptr);
}

extern "C" {
    void* ts_net_create_socket() {
        void* mem = ts_alloc(sizeof(TsSocket));
        return new (mem) TsSocket();
    }

    void ts_net_socket_connect(void* socket, void* port, void* host, void* callback) {
        TsSocket* s = (TsSocket*)socket;
        TsValue* p = (TsValue*)port;
        TsString* h = (TsString*)host;
        s->Connect(h->ToUtf8(), (int)p->i_val, callback);
    }

    // Global state for auto-select family settings
    static bool g_default_auto_select_family = true;
    static int64_t g_default_auto_select_family_attempt_timeout = 250; // ms

    // net.isIP(input) -> 0 (invalid), 4 (IPv4), or 6 (IPv6)
    int64_t ts_net_is_ip(void* input) {
        if (!input) return 0;
        
        TsString* str = nullptr;
        TsValue* val = (TsValue*)input;
        if (val->type == ValueType::STRING_PTR) {
            str = (TsString*)val->ptr_val;
        } else if (val->type == ValueType::OBJECT_PTR) {
            str = dynamic_cast<TsString*>((TsObject*)val->ptr_val);
        } else {
            str = (TsString*)input;
        }
        if (!str) return 0;
        
        std::string s = str->ToUtf8();
        if (s.empty()) return 0;
        
        // Try IPv4
        struct sockaddr_in sa4;
        if (uv_inet_pton(AF_INET, s.c_str(), &sa4.sin_addr) == 0) {
            return 4;
        }
        
        // Try IPv6
        struct sockaddr_in6 sa6;
        if (uv_inet_pton(AF_INET6, s.c_str(), &sa6.sin6_addr) == 0) {
            return 6;
        }
        
        return 0;
    }

    bool ts_net_is_ipv4(void* input) {
        return ts_net_is_ip(input) == 4;
    }

    bool ts_net_is_ipv6(void* input) {
        return ts_net_is_ip(input) == 6;
    }

    bool ts_net_get_default_auto_select_family() {
        return g_default_auto_select_family;
    }

    void ts_net_set_default_auto_select_family(bool value) {
        g_default_auto_select_family = value;
    }

    int64_t ts_net_get_default_auto_select_family_attempt_timeout() {
        return g_default_auto_select_family_attempt_timeout;
    }

    void ts_net_set_default_auto_select_family_attempt_timeout(int64_t value) {
        g_default_auto_select_family_attempt_timeout = value;
    }
}

// =========================================================================
// SocketAddress class implementation
// =========================================================================
struct TsSocketAddress : public TsObject {
    TsString* address;
    TsString* family;
    int64_t flowlabel;
    int64_t port;
    
    TsSocketAddress() : address(nullptr), family(nullptr), flowlabel(0), port(0) {
        this->magic = 0x53414444; // "SADD"
    }
};

// Helper to extract string from boxed value
static TsString* extractString(void* input) {
    if (!input) return nullptr;
    TsValue* val = (TsValue*)input;
    if (val->type == ValueType::STRING_PTR) {
        return (TsString*)val->ptr_val;
    } else if (val->type == ValueType::OBJECT_PTR) {
        return dynamic_cast<TsString*>((TsObject*)val->ptr_val);
    }
    return (TsString*)input;
}

extern "C" {
    void* ts_net_socket_address_create(void* options) {
        void* mem = ts_alloc(sizeof(TsSocketAddress));
        TsSocketAddress* addr = new (mem) TsSocketAddress();
        
        // Default values based on Node.js docs
        addr->family = TsString::Create("ipv4");
        addr->address = TsString::Create("127.0.0.1");
        addr->port = 0;
        addr->flowlabel = 0;
        
        // TODO: Parse options object if provided
        // For now, just use defaults
        
        return addr;
    }
    
    void* ts_net_socket_address_parse(void* input) {
        TsString* str = extractString(input);
        if (!str) return nullptr;
        
        std::string s = str->ToUtf8();
        if (s.empty()) return nullptr;
        
        void* mem = ts_alloc(sizeof(TsSocketAddress));
        TsSocketAddress* addr = new (mem) TsSocketAddress();
        
        // Parse format: "ip:port" or "[ip]:port" for IPv6
        size_t colonPos;
        std::string ipPart;
        int portVal = 0;
        
        if (s[0] == '[') {
            // IPv6 format: [ip]:port
            size_t closeBracket = s.find(']');
            if (closeBracket == std::string::npos) return nullptr;
            ipPart = s.substr(1, closeBracket - 1);
            if (closeBracket + 1 < s.length() && s[closeBracket + 1] == ':') {
                portVal = atoi(s.c_str() + closeBracket + 2);
            }
            addr->family = TsString::Create("ipv6");
        } else {
            // IPv4 format: ip:port
            colonPos = s.rfind(':');
            if (colonPos != std::string::npos) {
                ipPart = s.substr(0, colonPos);
                portVal = atoi(s.c_str() + colonPos + 1);
            } else {
                ipPart = s;
            }
            
            // Determine family by validating
            struct sockaddr_in sa4;
            struct sockaddr_in6 sa6;
            if (uv_inet_pton(AF_INET, ipPart.c_str(), &sa4.sin_addr) == 0) {
                addr->family = TsString::Create("ipv4");
            } else if (uv_inet_pton(AF_INET6, ipPart.c_str(), &sa6.sin6_addr) == 0) {
                addr->family = TsString::Create("ipv6");
            } else {
                return nullptr; // Invalid IP
            }
        }
        
        addr->address = TsString::Create(ipPart.c_str());
        addr->port = portVal;
        addr->flowlabel = 0;
        
        return addr;
    }
    
    void* ts_net_socket_address_get_address(void* ptr) {
        if (!ptr) return TsString::Create("");
        TsSocketAddress* addr = (TsSocketAddress*)ptr;
        return addr->address ? addr->address : TsString::Create("");
    }
    
    void* ts_net_socket_address_get_family(void* ptr) {
        if (!ptr) return TsString::Create("ipv4");
        TsSocketAddress* addr = (TsSocketAddress*)ptr;
        return addr->family ? addr->family : TsString::Create("ipv4");
    }
    
    int64_t ts_net_socket_address_get_flowlabel(void* ptr) {
        if (!ptr) return 0;
        TsSocketAddress* addr = (TsSocketAddress*)ptr;
        return addr->flowlabel;
    }
    
    int64_t ts_net_socket_address_get_port(void* ptr) {
        if (!ptr) return 0;
        TsSocketAddress* addr = (TsSocketAddress*)ptr;
        return addr->port;
    }
}

// =========================================================================
// BlockList class implementation
// =========================================================================
struct BlockListRule {
    enum Type { ADDRESS, RANGE, SUBNET };
    Type type;
    std::string start;       // For ADDRESS: the address; for RANGE: start addr; for SUBNET: network
    std::string end;         // For RANGE: end addr
    int prefix;              // For SUBNET: prefix bits
    bool isIPv6;
    
    std::string toString() const {
        if (type == ADDRESS) {
            return std::string("Address: ") + (isIPv6 ? "IPv6 " : "IPv4 ") + start;
        } else if (type == RANGE) {
            return std::string("Range: ") + (isIPv6 ? "IPv6 " : "IPv4 ") + start + "-" + end;
        } else {
            return std::string("Subnet: ") + (isIPv6 ? "IPv6 " : "IPv4 ") + start + "/" + std::to_string(prefix);
        }
    }
};

struct TsBlockList : public TsObject {
    std::vector<BlockListRule>* rules;
    
    TsBlockList() : rules(new std::vector<BlockListRule>()) {
        this->magic = 0x424C5354; // "BLST"
    }
};

// Helper to convert IP string to bytes
static bool ipToBytes(const char* ip, bool isIPv6, uint8_t* out, size_t outLen) {
    if (isIPv6) {
        struct sockaddr_in6 sa6;
        if (uv_inet_pton(AF_INET6, ip, &sa6.sin6_addr) != 0) return false;
        if (outLen < 16) return false;
        memcpy(out, &sa6.sin6_addr, 16);
    } else {
        struct sockaddr_in sa4;
        if (uv_inet_pton(AF_INET, ip, &sa4.sin_addr) != 0) return false;
        if (outLen < 4) return false;
        memcpy(out, &sa4.sin_addr, 4);
    }
    return true;
}

static int compareBytes(const uint8_t* a, const uint8_t* b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static bool matchesSubnet(const uint8_t* ip, const uint8_t* net, int prefix, int len) {
    int fullBytes = prefix / 8;
    int remainingBits = prefix % 8;
    
    // Compare full bytes
    for (int i = 0; i < fullBytes && i < len; i++) {
        if (ip[i] != net[i]) return false;
    }
    
    // Compare remaining bits
    if (remainingBits > 0 && fullBytes < len) {
        uint8_t mask = (uint8_t)(0xFF << (8 - remainingBits));
        if ((ip[fullBytes] & mask) != (net[fullBytes] & mask)) return false;
    }
    
    return true;
}

extern "C" {
    void* ts_net_block_list_create() {
        void* mem = ts_alloc(sizeof(TsBlockList));
        return new (mem) TsBlockList();
    }
    
    bool ts_net_block_list_is_block_list(void* value) {
        if (!value) return false;
        
        // Try to unbox if it's a TsValue
        TsValue* val = (TsValue*)value;
        TsObject* obj = nullptr;
        if (val->type == ValueType::OBJECT_PTR) {
            obj = (TsObject*)val->ptr_val;
        } else {
            obj = (TsObject*)value;
        }
        
        if (!obj) return false;
        return obj->magic == 0x424C5354; // "BLST"
    }
    
    void ts_net_block_list_add_address(void* blockListPtr, void* addressPtr, void* typePtr) {
        if (!blockListPtr) return;
        TsBlockList* bl = (TsBlockList*)blockListPtr;
        
        TsString* addrStr = extractString(addressPtr);
        if (!addrStr) return;
        
        TsString* typeStr = extractString(typePtr);
        bool isIPv6 = false;
        if (typeStr) {
            std::string t = typeStr->ToUtf8();
            isIPv6 = (t == "ipv6");
        }
        
        BlockListRule rule;
        rule.type = BlockListRule::ADDRESS;
        rule.start = addrStr->ToUtf8();
        rule.isIPv6 = isIPv6;
        bl->rules->push_back(rule);
    }
    
    void ts_net_block_list_add_range(void* blockListPtr, void* startPtr, void* endPtr, void* typePtr) {
        if (!blockListPtr) return;
        TsBlockList* bl = (TsBlockList*)blockListPtr;
        
        TsString* startStr = extractString(startPtr);
        TsString* endStr = extractString(endPtr);
        if (!startStr || !endStr) return;
        
        TsString* typeStr = extractString(typePtr);
        bool isIPv6 = false;
        if (typeStr) {
            std::string t = typeStr->ToUtf8();
            isIPv6 = (t == "ipv6");
        }
        
        BlockListRule rule;
        rule.type = BlockListRule::RANGE;
        rule.start = startStr->ToUtf8();
        rule.end = endStr->ToUtf8();
        rule.isIPv6 = isIPv6;
        bl->rules->push_back(rule);
    }
    
    void ts_net_block_list_add_subnet(void* blockListPtr, void* netPtr, int64_t prefix, void* typePtr) {
        if (!blockListPtr) return;
        TsBlockList* bl = (TsBlockList*)blockListPtr;
        
        TsString* netStr = extractString(netPtr);
        if (!netStr) return;
        
        TsString* typeStr = extractString(typePtr);
        bool isIPv6 = false;
        if (typeStr) {
            std::string t = typeStr->ToUtf8();
            isIPv6 = (t == "ipv6");
        }
        
        BlockListRule rule;
        rule.type = BlockListRule::SUBNET;
        rule.start = netStr->ToUtf8();
        rule.prefix = (int)prefix;
        rule.isIPv6 = isIPv6;
        bl->rules->push_back(rule);
    }
    
    bool ts_net_block_list_check(void* blockListPtr, void* addressPtr, void* typePtr) {
        if (!blockListPtr) return false;
        TsBlockList* bl = (TsBlockList*)blockListPtr;
        
        TsString* addrStr = extractString(addressPtr);
        if (!addrStr) return false;
        
        std::string addr = addrStr->ToUtf8();
        
        TsString* typeStr = extractString(typePtr);
        bool isIPv6 = false;
        if (typeStr) {
            std::string t = typeStr->ToUtf8();
            isIPv6 = (t == "ipv6");
        } else {
            // Auto-detect
            struct sockaddr_in6 sa6;
            isIPv6 = (uv_inet_pton(AF_INET6, addr.c_str(), &sa6.sin6_addr) == 0);
        }
        
        int len = isIPv6 ? 16 : 4;
        uint8_t addrBytes[16];
        if (!ipToBytes(addr.c_str(), isIPv6, addrBytes, 16)) return false;
        
        for (const auto& rule : *bl->rules) {
            if (rule.isIPv6 != isIPv6) continue;
            
            if (rule.type == BlockListRule::ADDRESS) {
                uint8_t ruleBytes[16];
                if (ipToBytes(rule.start.c_str(), isIPv6, ruleBytes, 16)) {
                    if (compareBytes(addrBytes, ruleBytes, len) == 0) return true;
                }
            } else if (rule.type == BlockListRule::RANGE) {
                uint8_t startBytes[16], endBytes[16];
                if (ipToBytes(rule.start.c_str(), isIPv6, startBytes, 16) &&
                    ipToBytes(rule.end.c_str(), isIPv6, endBytes, 16)) {
                    if (compareBytes(addrBytes, startBytes, len) >= 0 &&
                        compareBytes(addrBytes, endBytes, len) <= 0) {
                        return true;
                    }
                }
            } else if (rule.type == BlockListRule::SUBNET) {
                uint8_t netBytes[16];
                if (ipToBytes(rule.start.c_str(), isIPv6, netBytes, 16)) {
                    if (matchesSubnet(addrBytes, netBytes, rule.prefix, len)) return true;
                }
            }
        }
        
        return false;
    }
    
    void* ts_net_block_list_get_rules(void* blockListPtr) {
        if (!blockListPtr) return TsArray::Create();
        TsBlockList* bl = (TsBlockList*)blockListPtr;
        
        TsArray* arr = TsArray::Create();
        for (const auto& rule : *bl->rules) {
            std::string s = rule.toString();
            arr->Push((int64_t)TsString::Create(s.c_str()));
        }
        return arr;
    }
    
    void* ts_net_block_list_to_json(void* blockListPtr) {
        return ts_net_block_list_get_rules(blockListPtr);
    }
    
    void ts_net_block_list_from_json(void* blockListPtr, void* value) {
        // TODO: Parse JSON format rules and add them
        // For now, this is a stub
    }
}
