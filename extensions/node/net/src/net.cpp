// Net extension for ts-aot
// extern "C" wrappers for TsSocket, TsServer
// Plus TsSocketAddress and TsBlockList implementations (self-contained)
// Class implementations for TsSocket/TsServer remain in src/runtime/src/node/

#include "TsSocket.h"
#include "TsServer.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "TsNanBox.h"
#include "TsArray.h"
#include "TsMap.h"
#include "GC.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include <vector>
#include <string>

// =============================================================================
// Socket extern "C" Implementation
// =============================================================================

extern "C" {
    void* ts_net_create_socket() {
        void* mem = ts_alloc(sizeof(TsSocket));
        return new (mem) TsSocket();
    }

    void ts_net_socket_connect(void* socket, void* port, void* host, void* callback) {
        TsSocket* s = (TsSocket*)socket;
        int portNum = 0;
        if (port) {
            TsValue pv = nanbox_to_tagged((TsValue*)port);
            if (pv.type == ValueType::NUMBER_INT) portNum = (int)pv.i_val;
            else if (pv.type == ValueType::NUMBER_DBL) portNum = (int)pv.d_val;
        }
        TsString* h = (TsString*)host;
        s->Connect(h->ToUtf8(), portNum, callback);
    }

    // Socket address property getters
    void* ts_net_socket_get_remote_address(void* socket) {
        void* rawPtr = ts_value_get_object((TsValue*)socket);
        if (!rawPtr) rawPtr = socket;
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);
        if (!s) return ts_value_make_undefined();
        const char* addr = s->GetRemoteAddress();
        if (!addr) return ts_value_make_undefined();
        return TsString::Create(addr);
    }

    int64_t ts_net_socket_get_remote_port(void* socket) {
        void* rawPtr = ts_value_get_object((TsValue*)socket);
        if (!rawPtr) rawPtr = socket;
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);
        if (!s) return 0;
        return s->GetRemotePort();
    }

    void* ts_net_socket_get_remote_family(void* socket) {
        void* rawPtr = ts_value_get_object((TsValue*)socket);
        if (!rawPtr) rawPtr = socket;
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);
        if (!s) return ts_value_make_undefined();
        const char* family = s->GetRemoteFamily();
        if (!family) return ts_value_make_undefined();
        return TsString::Create(family);
    }

    void* ts_net_socket_get_local_address(void* socket) {
        void* rawPtr = ts_value_get_object((TsValue*)socket);
        if (!rawPtr) rawPtr = socket;
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);
        if (!s) return ts_value_make_undefined();
        const char* addr = s->GetLocalAddress();
        if (!addr) return ts_value_make_undefined();
        return TsString::Create(addr);
    }

    int64_t ts_net_socket_get_local_port(void* socket) {
        void* rawPtr = ts_value_get_object((TsValue*)socket);
        if (!rawPtr) rawPtr = socket;
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);
        if (!s) return 0;
        return s->GetLocalPort();
    }

    void* ts_net_socket_get_local_family(void* socket) {
        void* rawPtr = ts_value_get_object((TsValue*)socket);
        if (!rawPtr) rawPtr = socket;
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);
        if (!s) return ts_value_make_undefined();
        const char* family = s->GetLocalFamily();
        if (!family) return ts_value_make_undefined();
        return TsString::Create(family);
    }

    // Helper to get TsSocket from void* (handles both raw and boxed)
    static TsSocket* getSocketFromVoid(void* socket) {
        if (!socket) return nullptr;
        return (TsSocket*)socket;
    }

    // Socket state property getters
    int64_t ts_net_socket_get_bytes_read(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return 0;
        return (int64_t)s->GetBytesRead();
    }

    int64_t ts_net_socket_get_bytes_written(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return 0;
        return (int64_t)s->GetBytesWritten();
    }

    bool ts_net_socket_get_connecting(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return false;
        return s->IsConnecting();
    }

    bool ts_net_socket_get_destroyed(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return true;
        return s->IsDestroyed();
    }

    bool ts_net_socket_get_pending(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return false;
        return s->IsPending();
    }

    void* ts_net_socket_get_ready_state(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return TsString::Create("closed");
        const char* state = s->GetReadyState();
        return TsString::Create(state);
    }

    // Socket configuration methods - return socket for chaining
    void* ts_net_socket_set_timeout(void* socket, void* msecs, void* callback) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return socket;
        int ms = 0;
        if (msecs) {
            TsValue mv = nanbox_to_tagged((TsValue*)msecs);
            if (mv.type == ValueType::NUMBER_INT) ms = (int)mv.i_val;
            else if (mv.type == ValueType::NUMBER_DBL) ms = (int)mv.d_val;
        }
        s->SetTimeout(ms, callback);
        return socket;
    }

    void* ts_net_socket_set_no_delay(void* socket, void* noDelay) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return socket;
        bool nd = true;
        if (noDelay) {
            TsValue v = nanbox_to_tagged((TsValue*)noDelay);
            if (v.type == ValueType::BOOLEAN) {
                nd = v.b_val;
            } else if (v.type == ValueType::NUMBER_INT) {
                nd = v.i_val != 0;
            }
        }
        s->SetNoDelay(nd);
        return socket;
    }

    void* ts_net_socket_set_keep_alive(void* socket, void* enable, void* initialDelay) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return socket;
        bool en = true;
        if (enable) {
            TsValue v = nanbox_to_tagged((TsValue*)enable);
            if (v.type == ValueType::BOOLEAN) {
                en = v.b_val;
            } else if (v.type == ValueType::NUMBER_INT) {
                en = v.i_val != 0;
            }
        }
        int delay = 0;
        if (initialDelay) {
            TsValue dv = nanbox_to_tagged((TsValue*)initialDelay);
            if (dv.type == ValueType::NUMBER_INT) delay = (int)dv.i_val;
            else if (dv.type == ValueType::NUMBER_DBL) delay = (int)dv.d_val;
        }
        s->SetKeepAlive(en, delay);
        return socket;
    }

    void* ts_net_socket_address(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return nullptr;
        return s->Address();
    }

    void* ts_net_socket_ref(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (s) s->Ref();
        return socket;
    }

    void* ts_net_socket_unref(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (s) s->Unref();
        return socket;
    }

    // Global state for auto-select family settings
    static bool g_default_auto_select_family = true;
    static int64_t g_default_auto_select_family_attempt_timeout = 250;

    // net.isIP(input) -> 0 (invalid), 4 (IPv4), or 6 (IPv6)
    int64_t ts_net_is_ip(void* input) {
        if (!input) return 0;

        TsString* str = nullptr;
        TsValue vd = nanbox_to_tagged((TsValue*)input);
        if (vd.type == ValueType::STRING_PTR) {
            str = (TsString*)vd.ptr_val;
        } else if (vd.type == ValueType::OBJECT_PTR) {
            str = dynamic_cast<TsString*>((TsObject*)vd.ptr_val);
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

// =============================================================================
// SocketAddress class implementation
// =============================================================================
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
    TsValue vd = nanbox_to_tagged((TsValue*)input);
    if (vd.type == ValueType::STRING_PTR) {
        return (TsString*)vd.ptr_val;
    } else if (vd.type == ValueType::OBJECT_PTR) {
        return dynamic_cast<TsString*>((TsObject*)vd.ptr_val);
    }
    return (TsString*)input;
}

extern "C" {
    void* ts_net_socket_address_create(void* options) {
        void* mem = ts_alloc(sizeof(TsSocketAddress));
        TsSocketAddress* addr = new (mem) TsSocketAddress();

        addr->family = TsString::Create("ipv4");
        addr->address = TsString::Create("127.0.0.1");
        addr->port = 0;
        addr->flowlabel = 0;

        return addr;
    }

    void* ts_net_socket_address_parse(void* input) {
        TsString* str = extractString(input);
        if (!str) return nullptr;

        std::string s = str->ToUtf8();
        if (s.empty()) return nullptr;

        void* mem = ts_alloc(sizeof(TsSocketAddress));
        TsSocketAddress* addr = new (mem) TsSocketAddress();

        size_t colonPos;
        std::string ipPart;
        int portVal = 0;

        if (s[0] == '[') {
            size_t closeBracket = s.find(']');
            if (closeBracket == std::string::npos) return nullptr;
            ipPart = s.substr(1, closeBracket - 1);
            if (closeBracket + 1 < s.length() && s[closeBracket + 1] == ':') {
                portVal = atoi(s.c_str() + closeBracket + 2);
            }
            addr->family = TsString::Create("ipv6");
        } else {
            colonPos = s.rfind(':');
            if (colonPos != std::string::npos) {
                ipPart = s.substr(0, colonPos);
                portVal = atoi(s.c_str() + colonPos + 1);
            } else {
                ipPart = s;
            }

            struct sockaddr_in sa4;
            struct sockaddr_in6 sa6;
            if (uv_inet_pton(AF_INET, ipPart.c_str(), &sa4.sin_addr) == 0) {
                addr->family = TsString::Create("ipv4");
            } else if (uv_inet_pton(AF_INET6, ipPart.c_str(), &sa6.sin6_addr) == 0) {
                addr->family = TsString::Create("ipv6");
            } else {
                return nullptr;
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

// =============================================================================
// BlockList class implementation
// =============================================================================
struct BlockListRule {
    enum Type { ADDRESS, RANGE, SUBNET };
    Type type;
    std::string start;
    std::string end;
    int prefix;
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

    for (int i = 0; i < fullBytes && i < len; i++) {
        if (ip[i] != net[i]) return false;
    }

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

        TsValue vd2 = nanbox_to_tagged((TsValue*)value);
        TsObject* obj = nullptr;
        if (vd2.type == ValueType::OBJECT_PTR) {
            obj = (TsObject*)vd2.ptr_val;
        } else {
            obj = (TsObject*)value;
        }

        if (!obj) return false;
        return obj->magic == 0x424C5354;
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
        // Stub
    }
}

// =============================================================================
// Server extern "C" Implementation
// =============================================================================

extern "C" {
    void* ts_net_create_server(void* callback) {
        void* mem = ts_alloc(sizeof(TsServer));
        TsServer* server = new (mem) TsServer();
        if (callback) {
            server->On("connection", callback);
        }
        return server;
    }

    void ts_net_server_listen(void* server, void* port, void* host, void* callback) {
        TsServer* s = (TsServer*)server;
        TsValue* p = (TsValue*)port;
        // Extract host string if provided
        const char* hostStr = nullptr;
        if (host && !ts_value_is_null((TsValue*)host) && !ts_value_is_undefined((TsValue*)host)) {
            void* raw = ts_value_get_string((TsValue*)host);
            if (raw) {
                TsString* str = (TsString*)raw;
                hostStr = str->ToUtf8();
            }
        }
        s->Listen((int)ts_value_get_int((TsValue*)p), hostStr, callback);
    }

    void ts_net_server_close(void* server) {
        TsServer* s = (TsServer*)server;
        if (s) s->Close();
    }

    void* ts_net_server_address(void* server) {
        TsServer* s = (TsServer*)server;
        if (!s) return nullptr;
        return s->Address();
    }

    void* ts_net_server_ref(void* server) {
        TsServer* s = (TsServer*)server;
        if (s) s->Ref();
        return server;
    }

    void* ts_net_server_unref(void* server) {
        TsServer* s = (TsServer*)server;
        if (s) s->Unref();
        return server;
    }

    void ts_net_server_get_connections(void* server, void* callback) {
        TsServer* s = (TsServer*)server;
        if (s) s->GetConnections(callback);
    }

    int64_t ts_net_server_get_maxConnections(void* server) {
        TsServer* s = (TsServer*)server;
        if (!s) return -1;
        return s->maxConnections;
    }

    void ts_net_server_set_maxConnections(void* server, void* value) {
        TsServer* s = (TsServer*)server;
        if (!s) return;
        if (value) {
            TsValue vd3 = nanbox_to_tagged((TsValue*)value);
            if (vd3.type == ValueType::NUMBER_INT) {
                s->maxConnections = (int)vd3.i_val;
            }
        }
    }

    void ts_net_socket_end(void* socket) {
        TsSocket* s = getSocketFromVoid(socket);
        if (!s) return;
        s->End();
    }
}
