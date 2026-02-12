#include "TsUDPSocket.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "GC.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include <string>
#include <stdio.h>

// Helper to get TsUDPSocket from void* with proper NaN-box-aware unboxing
static TsUDPSocket* GetUDPSocket(void* param) {
    if (!param) return nullptr;
    void* rawPtr = ts_value_get_object((TsValue*)param);
    if (!rawPtr) rawPtr = param;
    return dynamic_cast<TsUDPSocket*>((TsObject*)rawPtr);
}

// Helper to get string from void* with proper unboxing
static const char* GetString(void* param) {
    if (!param) return nullptr;
    void* rawPtr = ts_value_get_object((TsValue*)param);
    TsString* str = (TsString*)(rawPtr ? rawPtr : param);
    return str ? str->ToUtf8() : nullptr;
}

// Helper to get int from void* (NaN-box-aware)
static int64_t GetInt(void* param) {
    if (!param) return 0;
    return ts_value_get_int((TsValue*)param);
}

// Helper to get bool from void* (NaN-box-aware)
static bool GetBool(void* param) {
    if (!param) return false;
    return ts_value_get_bool((TsValue*)param);
}

TsUDPSocket::TsUDPSocket(bool ipv6)
    : ipv6_(ipv6)
    , bound_(false)
    , connected_(false)
    , receiving_(false)
    , destroyed_(false)
    , closing_(false)
    , reuseAddr_(false)
    , ipv6Only_(false)
    , localPort_(0)
    , remotePort_(0)
    , bytesRead_(0)
    , bytesWritten_(0)
    , closeCallback_(nullptr) {
    this->magic = MAGIC;

    // Allocate and initialize UDP handle
    handle_ = (uv_udp_t*)ts_alloc(sizeof(uv_udp_t));
    int result = uv_udp_init(uv_default_loop(), handle_);
    if (result < 0) {
        handle_ = nullptr;
        return;
    }
    handle_->data = this;
}

TsUDPSocket::~TsUDPSocket() {
    if (handle_ && !destroyed_ && !closing_) {
        closing_ = true;
        uv_close((uv_handle_t*)handle_, nullptr);
    }
}

TsUDPSocket* TsUDPSocket::Create(const char* type) {
    bool ipv6 = false;
    if (type && strcmp(type, "udp6") == 0) {
        ipv6 = true;
    }

    void* mem = ts_alloc(sizeof(TsUDPSocket));
    return new (mem) TsUDPSocket(ipv6);
}

void TsUDPSocket::On(const char* event, void* callback) {
    TsEventEmitter::On(event, callback);

    // Start receiving when 'message' listener is added
    if (strcmp(event, "message") == 0 && bound_ && !receiving_) {
        StartReceiving();
    }
}

void TsUDPSocket::Bind(int port, const char* address, void* callback) {
    if (destroyed_ || bound_) {
        EmitError("Socket is already bound or destroyed");
        return;
    }

    struct sockaddr_storage addr;
    int result;

    if (ipv6_) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        const char* bindAddr = (address && strlen(address) > 0) ? address : "::";
        result = uv_ip6_addr(bindAddr, port, addr6);
    } else {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        const char* bindAddr = (address && strlen(address) > 0) ? address : "0.0.0.0";
        result = uv_ip4_addr(bindAddr, port, addr4);
    }

    if (result != 0) {
        EmitError(result);
        return;
    }

    // Build bind flags based on socket options
    unsigned int flags = 0;
    if (reuseAddr_) {
        flags |= UV_UDP_REUSEADDR;
    }
    if (ipv6Only_ && ipv6_) {
        flags |= UV_UDP_IPV6ONLY;
    }

    result = uv_udp_bind(handle_, (struct sockaddr*)&addr, flags);
    if (result != 0) {
        EmitError(result);
        return;
    }

    bound_ = true;
    UpdateLocalAddress();

    // Start receiving immediately if we have message listeners
    if (ListenerCount("message") > 0) {
        StartReceiving();
    }

    // Emit 'listening' event
    Emit("listening", 0, nullptr);

    // Call callback if provided
    if (callback) {
        ts_call_0((TsValue*)callback);
    }
}

void TsUDPSocket::Send(void* msg, int offset, int length, int port, const char* address, void* callback) {
    if (destroyed_) {
        EmitError("Socket is destroyed");
        return;
    }

    const char* data = nullptr;
    size_t dataLen = 0;

    // Handle message - could be string, buffer, or NaN-boxed value
    // Use NaN-box-aware decoding
    TsValue val = nanbox_to_tagged((TsValue*)msg);

    if (val.type == ValueType::STRING_PTR && val.ptr_val) {
        TsString* str = (TsString*)val.ptr_val;
        data = str->ToUtf8();
        dataLen = strlen(data);
    } else if (val.type == ValueType::OBJECT_PTR && val.ptr_val) {
        // Object value - could be Buffer
        TsBuffer* buffer = dynamic_cast<TsBuffer*>((TsObject*)val.ptr_val);
        if (buffer) {
            data = (const char*)buffer->GetData();
            dataLen = buffer->GetLength();
        } else {
            // Check if it's a TsString via magic
            uint32_t magic = *(uint32_t*)val.ptr_val;
            if (magic == TsString::MAGIC) {
                TsString* str = (TsString*)val.ptr_val;
                data = str->ToUtf8();
                dataLen = strlen(data);
            }
        }
    } else {
        // Try as raw pointer (for unboxed heap pointers)
        void* rawPtr = ts_value_get_object((TsValue*)msg);
        if (!rawPtr) rawPtr = msg;

        // Check magic to determine type
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic == TsString::MAGIC) {
            TsString* str = (TsString*)rawPtr;
            data = str->ToUtf8();
            dataLen = strlen(data);
        } else {
            TsBuffer* buffer = dynamic_cast<TsBuffer*>((TsObject*)rawPtr);
            if (buffer) {
                data = (const char*)buffer->GetData();
                dataLen = buffer->GetLength();
            }
        }
    }

    if (!data) {
        EmitError("Invalid message data");
        return;
    }

    // Apply offset and length
    if (offset > 0) {
        if ((size_t)offset >= dataLen) {
            EmitError("Offset out of bounds");
            return;
        }
        data += offset;
        dataLen -= offset;
    }
    if (length > 0 && (size_t)length < dataLen) {
        dataLen = length;
    }

    // Prepare destination address
    struct sockaddr_storage destAddr;
    int result;

    if (connected_) {
        // Use connected address (no need to specify address/port)
        // For connected sockets, pass NULL to uv_udp_send
        result = 0;
    } else {
        if (!address || strlen(address) == 0) {
            EmitError("Address required for unconnected socket");
            return;
        }

        if (ipv6_) {
            result = uv_ip6_addr(address, port, (struct sockaddr_in6*)&destAddr);
        } else {
            result = uv_ip4_addr(address, port, (struct sockaddr_in*)&destAddr);
        }

        if (result != 0) {
            EmitError(result);
            return;
        }
    }

    // Create send context
    UdpSendContext* ctx = (UdpSendContext*)ts_alloc(sizeof(UdpSendContext));
    ctx->socket = this;
    ctx->callback = callback;
    ctx->length = dataLen;

    // Copy data to GC-allocated buffer (in case original is freed)
    char* sendData = (char*)ts_alloc(dataLen);
    memcpy(sendData, data, dataLen);
    ctx->buf = uv_buf_init(sendData, (unsigned int)dataLen);

    // Send the datagram
    const struct sockaddr* dest = connected_ ? nullptr : (struct sockaddr*)&destAddr;
    result = uv_udp_send(&ctx->req, handle_, &ctx->buf, 1, dest, OnSend);

    if (result != 0) {
        EmitError(result);
        return;
    }

    bytesWritten_ += dataLen;
}

void TsUDPSocket::OnSend(uv_udp_send_t* req, int status) {
    UdpSendContext* ctx = (UdpSendContext*)((char*)req - offsetof(UdpSendContext, req));
    TsUDPSocket* self = ctx->socket;

    if (status != 0) {
        self->EmitError(status);
    }

    // Call callback if provided
    if (ctx->callback) {
        if (status != 0) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create(uv_strerror(status)));
            ts_call_1((TsValue*)ctx->callback, err);
        } else {
            ts_call_0((TsValue*)ctx->callback);
        }
    }
}

void TsUDPSocket::Close(void* callback) {
    if (destroyed_ || closing_) {
        return;
    }

    closing_ = true;
    closeCallback_ = callback;

    // Stop receiving
    if (receiving_) {
        StopReceiving();
    }

    // Close handle
    uv_close((uv_handle_t*)handle_, OnClose);
}

void TsUDPSocket::OnClose(uv_handle_t* handle) {
    TsUDPSocket* self = (TsUDPSocket*)handle->data;
    self->destroyed_ = true;
    self->closing_ = false;
    self->bound_ = false;
    self->connected_ = false;

    // Emit 'close' event
    self->Emit("close", 0, nullptr);

    // Call callback if provided
    if (self->closeCallback_) {
        ts_call_0((TsValue*)self->closeCallback_);
    }
}

void* TsUDPSocket::Address() {
    if (!bound_) {
        return nullptr;
    }

    // Create object with { address, family, port }
    TsMap* obj = TsMap::Create();

    TsString* addrStr = TsString::Create(localAddress_.c_str());
    TsString* familyStr = TsString::Create(localFamily_.c_str());

    // NOTE: TsMap::Set takes TsValue by value. If we pass TsString* directly,
    // the implicit TsValue(TsString*) constructor correctly creates STRING_PTR.
    // However, passing ts_value_make_string() result (TsValue*) would match
    // the TsValue(void*) constructor, creating OBJECT_PTR - WRONG!
    // So we pass TsString* directly to leverage the correct implicit conversion.
    obj->Set(TsString::Create("address"), addrStr);
    obj->Set(TsString::Create("family"), familyStr);

    // For int, we need to construct TsValue explicitly
    TsValue portVal;
    portVal.type = ValueType::NUMBER_INT;
    portVal.i_val = localPort_;
    obj->Set(TsString::Create("port"), portVal);

    return obj;  // Return raw TsMap*, property access will handle unboxing
}

void TsUDPSocket::SetBroadcast(bool flag) {
    if (handle_) {
        uv_udp_set_broadcast(handle_, flag ? 1 : 0);
    }
}

void TsUDPSocket::SetMulticastTTL(int ttl) {
    if (handle_) {
        uv_udp_set_multicast_ttl(handle_, ttl);
    }
}

void TsUDPSocket::SetMulticastLoopback(bool flag) {
    if (handle_) {
        uv_udp_set_multicast_loop(handle_, flag ? 1 : 0);
    }
}

void TsUDPSocket::SetMulticastInterface(const char* interfaceAddr) {
    if (handle_ && interfaceAddr) {
        uv_udp_set_multicast_interface(handle_, interfaceAddr);
    }
}

void TsUDPSocket::AddMembership(const char* multicastAddr, const char* interfaceAddr) {
    if (!handle_ || !multicastAddr) return;

    int result = uv_udp_set_membership(handle_, multicastAddr, interfaceAddr, UV_JOIN_GROUP);
    if (result != 0) {
        EmitError(result);
    }
}

void TsUDPSocket::DropMembership(const char* multicastAddr, const char* interfaceAddr) {
    if (!handle_ || !multicastAddr) return;

    int result = uv_udp_set_membership(handle_, multicastAddr, interfaceAddr, UV_LEAVE_GROUP);
    if (result != 0) {
        EmitError(result);
    }
}

void TsUDPSocket::Connect(int port, const char* address, void* callback) {
    if (destroyed_) {
        EmitError("Socket is destroyed");
        return;
    }

    struct sockaddr_storage addr;
    int result;

    if (ipv6_) {
        result = uv_ip6_addr(address, port, (struct sockaddr_in6*)&addr);
    } else {
        result = uv_ip4_addr(address, port, (struct sockaddr_in*)&addr);
    }

    if (result != 0) {
        EmitError(result);
        return;
    }

    result = uv_udp_connect(handle_, (struct sockaddr*)&addr);
    if (result != 0) {
        EmitError(result);
        return;
    }

    connected_ = true;
    remoteAddress_ = address;
    remotePort_ = port;

    // Emit 'connect' event
    Emit("connect", 0, nullptr);

    // Call callback if provided
    if (callback) {
        ts_call_0((TsValue*)callback);
    }
}

void TsUDPSocket::Disconnect() {
    if (!connected_ || destroyed_) return;

    int result = uv_udp_connect(handle_, nullptr);
    if (result != 0) {
        EmitError(result);
        return;
    }

    connected_ = false;
    remoteAddress_.clear();
    remotePort_ = 0;
}

void TsUDPSocket::Ref() {
    if (handle_) {
        uv_ref((uv_handle_t*)handle_);
    }
}

void TsUDPSocket::Unref() {
    if (handle_) {
        uv_unref((uv_handle_t*)handle_);
    }
}

void TsUDPSocket::SetRecvBufferSize(int size) {
    if (handle_) {
        int value = size;
        uv_recv_buffer_size((uv_handle_t*)handle_, &value);
    }
}

void TsUDPSocket::SetSendBufferSize(int size) {
    if (handle_) {
        int value = size;
        uv_send_buffer_size((uv_handle_t*)handle_, &value);
    }
}

int TsUDPSocket::GetRecvBufferSize() {
    if (!handle_) return 0;
    int value = 0;
    uv_recv_buffer_size((uv_handle_t*)handle_, &value);
    return value;
}

int TsUDPSocket::GetSendBufferSize() {
    if (!handle_) return 0;
    int value = 0;
    uv_send_buffer_size((uv_handle_t*)handle_, &value);
    return value;
}

void TsUDPSocket::SetTTL(int ttl) {
    if (handle_) {
        uv_udp_set_ttl(handle_, ttl);
    }
}

void TsUDPSocket::StartReceiving() {
    if (receiving_ || destroyed_ || !handle_) return;

    int result = uv_udp_recv_start(handle_, OnAlloc, OnRecv);
    if (result == 0) {
        receiving_ = true;
    }
}

void TsUDPSocket::StopReceiving() {
    if (!receiving_ || !handle_) return;

    uv_udp_recv_stop(handle_);
    receiving_ = false;
}

void TsUDPSocket::UpdateLocalAddress() {
    if (!handle_) return;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_udp_getsockname(handle_, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) {
        return;
    }

    char ipStr[INET6_ADDRSTRLEN];

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        uv_inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
        localPort_ = ntohs(addr4->sin_port);
        localFamily_ = "IPv4";
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        uv_inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        localPort_ = ntohs(addr6->sin6_port);
        localFamily_ = "IPv6";
    } else {
        return;
    }

    localAddress_ = ipStr;
}

void TsUDPSocket::OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    // Allocate buffer for incoming datagram
    // UDP datagrams have max size of 65535 bytes, but 64KB is reasonable
    size_t size = suggested_size > 65536 ? 65536 : suggested_size;
    buf->base = (char*)ts_alloc(size);
    buf->len = (unsigned int)size;
}

void TsUDPSocket::OnRecv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                         const struct sockaddr* addr, unsigned flags) {
    TsUDPSocket* self = (TsUDPSocket*)handle->data;
    self->HandleReceive(nread, buf, addr, flags);
}

void TsUDPSocket::HandleReceive(ssize_t nread, const uv_buf_t* buf,
                                const struct sockaddr* addr, unsigned flags) {
    if (nread < 0) {
        // Error
        EmitError((int)nread);
        return;
    }

    if (nread == 0) {
        // No data, possibly empty datagram or no more data
        return;
    }

    bytesRead_ += nread;

    // Create Buffer with received data
    TsBuffer* msgBuffer = TsBuffer::Create(nread);
    memcpy(msgBuffer->GetData(), buf->base, nread);

    // Create rinfo object: { address, family, port, size }
    TsMap* rinfo = TsMap::Create();

    char ipStr[INET6_ADDRSTRLEN];
    int port = 0;
    const char* family = "IPv4";

    if (addr) {
        if (addr->sa_family == AF_INET) {
            struct sockaddr_in* addr4 = (struct sockaddr_in*)addr;
            uv_inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
            port = ntohs(addr4->sin_port);
            family = "IPv4";
        } else if (addr->sa_family == AF_INET6) {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)addr;
            uv_inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
            port = ntohs(addr6->sin6_port);
            family = "IPv6";
        }
    }

    // NOTE: TsMap::Set takes TsValue by value. Pass TsString* directly to use
    // the correct implicit constructor (STRING_PTR). For integers, construct
    // TsValue explicitly to avoid the void* constructor creating OBJECT_PTR.
    rinfo->Set(TsString::Create("address"), TsString::Create(ipStr));
    rinfo->Set(TsString::Create("family"), TsString::Create(family));

    TsValue portVal;
    portVal.type = ValueType::NUMBER_INT;
    portVal.i_val = port;
    rinfo->Set(TsString::Create("port"), portVal);

    TsValue sizeVal;
    sizeVal.type = ValueType::NUMBER_INT;
    sizeVal.i_val = nread;
    rinfo->Set(TsString::Create("size"), sizeVal);

    // Emit 'message' event with (msg, rinfo)
    void* args[] = { ts_value_make_object(msgBuffer), ts_value_make_object(rinfo) };
    Emit("message", 2, args);
}

void TsUDPSocket::EmitError(const char* message) {
    TsValue* err = (TsValue*)ts_error_create(TsString::Create(message));
    void* args[] = { err };
    Emit("error", 1, args);
}

void TsUDPSocket::EmitError(int uvError) {
    EmitError(uv_strerror(uvError));
}

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

void* ts_dgram_create_socket(void* type) {
    const char* typeStr = GetString(type);
    TsUDPSocket* socket = TsUDPSocket::Create(typeStr ? typeStr : "udp4");
    return ts_value_make_object(socket);
}

void* ts_dgram_create_socket_options(void* type, int64_t reuseAddr, int64_t ipv6Only,
                                     int64_t recvBufferSize, int64_t sendBufferSize) {
    const char* typeStr = GetString(type);
    TsUDPSocket* socket = TsUDPSocket::Create(typeStr ? typeStr : "udp4");

    // Store socket options (applied during bind)
    if (reuseAddr) {
        socket->SetReuseAddr(true);
    }
    if (ipv6Only) {
        socket->SetIPv6Only(true);
    }

    // Apply buffer sizes if specified (> 0)
    if (recvBufferSize > 0) {
        socket->SetRecvBufferSize((int)recvBufferSize);
    }
    if (sendBufferSize > 0) {
        socket->SetSendBufferSize((int)sendBufferSize);
    }

    return ts_value_make_object(socket);
}

void ts_udp_socket_bind(void* socket, void* port, void* address, void* callback) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    int portNum = (int)GetInt(port);
    const char* addrStr = GetString(address);

    s->Bind(portNum, addrStr, callback);
}

void ts_udp_socket_send(void* socket, void* msg, void* offset, void* length,
                        void* port, void* address, void* callback) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    int offsetNum = (int)GetInt(offset);
    int lengthNum = (int)GetInt(length);
    int portNum = (int)GetInt(port);
    const char* addrStr = GetString(address);

    s->Send(msg, offsetNum, lengthNum, portNum, addrStr, callback);
}

void ts_udp_socket_close(void* socket, void* callback) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->Close(callback);
}

void* ts_udp_socket_address(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return nullptr;

    return s->Address();
}

void ts_udp_socket_set_broadcast(void* socket, void* flag) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->SetBroadcast(GetBool(flag));
}

void ts_udp_socket_set_multicast_ttl(void* socket, void* ttl) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->SetMulticastTTL((int)GetInt(ttl));
}

void ts_udp_socket_set_multicast_loopback(void* socket, void* flag) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->SetMulticastLoopback(GetBool(flag));
}

void ts_udp_socket_set_multicast_interface(void* socket, void* interfaceAddr) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->SetMulticastInterface(GetString(interfaceAddr));
}

void ts_udp_socket_add_membership(void* socket, void* multicastAddr, void* interfaceAddr) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->AddMembership(GetString(multicastAddr), GetString(interfaceAddr));
}

void ts_udp_socket_drop_membership(void* socket, void* multicastAddr, void* interfaceAddr) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->DropMembership(GetString(multicastAddr), GetString(interfaceAddr));
}

void ts_udp_socket_connect(void* socket, void* port, void* address, void* callback) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->Connect((int)GetInt(port), GetString(address), callback);
}

void ts_udp_socket_disconnect(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->Disconnect();
}

void ts_udp_socket_ref(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->Ref();
}

void ts_udp_socket_unref(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->Unref();
}

void* ts_udp_socket_get_local_address(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return ts_value_make_string(TsString::Create(""));

    return ts_value_make_string(TsString::Create(s->GetLocalAddress()));
}

int64_t ts_udp_socket_get_local_port(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return 0;

    return s->GetLocalPort();
}

void* ts_udp_socket_get_local_family(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return ts_value_make_string(TsString::Create(""));

    return ts_value_make_string(TsString::Create(s->GetLocalFamily()));
}

void* ts_udp_socket_get_remote_address(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return ts_value_make_string(TsString::Create(""));

    return ts_value_make_string(TsString::Create(s->GetRemoteAddress()));
}

int64_t ts_udp_socket_get_remote_port(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return 0;

    return s->GetRemotePort();
}

int64_t ts_udp_socket_get_bytes_read(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return 0;

    return s->GetBytesRead();
}

int64_t ts_udp_socket_get_bytes_written(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return 0;

    return s->GetBytesWritten();
}

bool ts_udp_socket_is_bound(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return false;

    return s->IsBound();
}

bool ts_udp_socket_is_connected(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return false;

    return s->IsConnected();
}

bool ts_udp_socket_is_destroyed(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return true;

    return s->IsDestroyed();
}

void ts_udp_socket_set_recv_buffer_size(void* socket, void* size) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->SetRecvBufferSize((int)GetInt(size));
}

void ts_udp_socket_set_send_buffer_size(void* socket, void* size) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->SetSendBufferSize((int)GetInt(size));
}

int64_t ts_udp_socket_get_recv_buffer_size(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return 0;

    return s->GetRecvBufferSize();
}

int64_t ts_udp_socket_get_send_buffer_size(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return 0;

    return s->GetSendBufferSize();
}

void ts_udp_socket_set_ttl(void* socket, void* ttl) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s) return;

    s->SetTTL((int)GetInt(ttl));
}

void* ts_udp_socket_remote_address(void* socket) {
    TsUDPSocket* s = GetUDPSocket(socket);
    if (!s || !s->IsConnected()) {
        return nullptr;
    }

    // Create { address, family, port } object
    TsMap* obj = TsMap::Create();

    TsString* addrStr = TsString::Create(s->GetRemoteAddress());
    TsString* familyStr = TsString::Create("IPv4");  // TODO: track actual family

    obj->Set(TsString::Create("address"), addrStr);
    obj->Set(TsString::Create("family"), familyStr);

    TsValue portVal;
    portVal.type = ValueType::NUMBER_INT;
    portVal.i_val = s->GetRemotePort();
    obj->Set(TsString::Create("port"), portVal);

    return obj;  // Return raw TsMap*
}

} // extern "C"
