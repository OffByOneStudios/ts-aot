#include "TsChildProcess.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "GC.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include <sstream>

// Forward declarations for JSON functions
extern "C" {
    TsString* ts_json_stringify(TsValue* value);
    TsValue* ts_json_parse(TsValue* jsonStr);
}

#ifdef _WIN32
#include <windows.h>
#define SIGTERM 15
#define SIGKILL 9
#define SIGINT 2
#else
#include <signal.h>
#endif

// ============================================================================
// TsChildProcess Implementation
// ============================================================================

TsChildProcess::TsChildProcess()
    : processHandle_(nullptr)
    , stdinPipe_(nullptr)
    , stdoutPipe_(nullptr)
    , stderrPipe_(nullptr)
    , ipcPipe_(nullptr)
    , stdin_(nullptr)
    , stdout_(nullptr)
    , stderr_(nullptr)
    , pid_(-1)
    , connected_(false)
    , killed_(false)
    , exitCode_(-1)
    , signalCode_("")
    , spawnfile_("")
    , channel_(nullptr)
    , referenced_(true)
    , ipcConnected_(false)
    , ipcReadBuffer_("")
    , execCallback_(nullptr)
    , accumulatedStdout_("")
    , accumulatedStderr_("")
{
    this->magic = MAGIC;
}

TsChildProcess::~TsChildProcess() {
    CleanupHandles();
}

void TsChildProcess::CleanupHandles() {
    // Process handle is cleaned up in OnProcessExit
}

int TsChildProcess::Spawn(const char* file, const std::vector<std::string>& args,
                          const char* cwd, char** env, bool detached, bool windowsHide) {
    spawnfile_ = file ? file : "";
    spawnargs_.clear();
    for (const auto& arg : args) {
        spawnargs_.push_back(arg);
    }

    // Allocate process handle
    processHandle_ = (uv_process_t*)ts_alloc(sizeof(uv_process_t));
    memset(processHandle_, 0, sizeof(uv_process_t));
    processHandle_->data = this;

    // Set up process options
    uv_process_options_t options;
    memset(&options, 0, sizeof(options));

    options.exit_cb = OnProcessExit;
    options.file = file;

    // Build args array (first element must be the command itself)
    std::vector<char*> argsArray;
    argsArray.push_back(const_cast<char*>(file));
    for (const auto& arg : args) {
        argsArray.push_back(const_cast<char*>(arg.c_str()));
    }
    argsArray.push_back(nullptr);
    options.args = argsArray.data();

    // Set cwd if provided
    if (cwd && strlen(cwd) > 0) {
        options.cwd = cwd;
    }

    // Set environment if provided
    if (env) {
        options.env = env;
    }

    // Set flags
    if (detached) {
        options.flags |= UV_PROCESS_DETACHED;
    }
#ifdef _WIN32
    if (windowsHide) {
        options.flags |= UV_PROCESS_WINDOWS_HIDE;
    }
#endif

    // Set up stdio pipes
    stdinPipe_ = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));
    stdoutPipe_ = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));
    stderrPipe_ = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));

    uv_pipe_init(uv_default_loop(), stdinPipe_, 0);
    uv_pipe_init(uv_default_loop(), stdoutPipe_, 0);
    uv_pipe_init(uv_default_loop(), stderrPipe_, 0);

    stdinPipe_->data = this;
    stdoutPipe_->data = this;
    stderrPipe_->data = this;

    // Configure stdio containers
    uv_stdio_container_t stdio[3];
    stdio[0].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
    stdio[0].data.stream = (uv_stream_t*)stdinPipe_;
    stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[1].data.stream = (uv_stream_t*)stdoutPipe_;
    stdio[2].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[2].data.stream = (uv_stream_t*)stderrPipe_;

    options.stdio = stdio;
    options.stdio_count = 3;

    // Spawn the process
    int r = uv_spawn(uv_default_loop(), processHandle_, &options);
    if (r < 0) {
        // Emit error event
        TsValue* err = (TsValue*)ts_error_create(TsString::Create(uv_strerror(r)));
        void* errArgs[] = { err };
        Emit("error", 1, errArgs);
        return r;
    }

    pid_ = processHandle_->pid;
    connected_ = true;

    // Create wrapper stream objects
    stdin_ = new (ts_alloc(sizeof(TsChildProcessWritable)))
        TsChildProcessWritable(stdinPipe_, this);
    stdout_ = new (ts_alloc(sizeof(TsChildProcessReadable)))
        TsChildProcessReadable(stdoutPipe_, this);
    stderr_ = new (ts_alloc(sizeof(TsChildProcessReadable)))
        TsChildProcessReadable(stderrPipe_, this);

    // Start reading from stdout and stderr
    uv_read_start((uv_stream_t*)stdoutPipe_, OnAlloc, OnStdoutRead);
    uv_read_start((uv_stream_t*)stderrPipe_, OnAlloc, OnStderrRead);

    // Emit spawn event
    Emit("spawn", 0, nullptr);

    return 0;
}

int TsChildProcess::SpawnWithIPC(const char* file, const std::vector<std::string>& args,
                                  const char* cwd, char** env, bool detached, bool windowsHide) {
    spawnfile_ = file ? file : "";
    spawnargs_.clear();
    for (const auto& arg : args) {
        spawnargs_.push_back(arg);
    }

    // Allocate process handle
    processHandle_ = (uv_process_t*)ts_alloc(sizeof(uv_process_t));
    memset(processHandle_, 0, sizeof(uv_process_t));
    processHandle_->data = this;

    // Set up process options
    uv_process_options_t options;
    memset(&options, 0, sizeof(options));

    options.exit_cb = OnProcessExit;
    options.file = file;

    // Build args array (first element must be the command itself)
    std::vector<char*> argsArray;
    argsArray.push_back(const_cast<char*>(file));
    for (const auto& arg : args) {
        argsArray.push_back(const_cast<char*>(arg.c_str()));
    }
    argsArray.push_back(nullptr);
    options.args = argsArray.data();

    // Set cwd if provided
    if (cwd && strlen(cwd) > 0) {
        options.cwd = cwd;
    }

    // Set environment if provided
    if (env) {
        options.env = env;
    }

    // Set flags
    if (detached) {
        options.flags |= UV_PROCESS_DETACHED;
    }
#ifdef _WIN32
    if (windowsHide) {
        options.flags |= UV_PROCESS_WINDOWS_HIDE;
    }
#endif

    // Set up stdio pipes (stdin, stdout, stderr, IPC)
    stdinPipe_ = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));
    stdoutPipe_ = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));
    stderrPipe_ = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));
    ipcPipe_ = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));

    uv_pipe_init(uv_default_loop(), stdinPipe_, 0);
    uv_pipe_init(uv_default_loop(), stdoutPipe_, 0);
    uv_pipe_init(uv_default_loop(), stderrPipe_, 0);
    uv_pipe_init(uv_default_loop(), ipcPipe_, 1);  // 1 = enable IPC

    stdinPipe_->data = this;
    stdoutPipe_->data = this;
    stderrPipe_->data = this;
    ipcPipe_->data = this;

    // Configure stdio containers: stdin(0), stdout(1), stderr(2), IPC(3)
    uv_stdio_container_t stdio[4];
    stdio[0].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
    stdio[0].data.stream = (uv_stream_t*)stdinPipe_;
    stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[1].data.stream = (uv_stream_t*)stdoutPipe_;
    stdio[2].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[2].data.stream = (uv_stream_t*)stderrPipe_;
    // IPC pipe on fd 3 - bidirectional
    stdio[3].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
    stdio[3].data.stream = (uv_stream_t*)ipcPipe_;

    options.stdio = stdio;
    options.stdio_count = 4;

    // Spawn the process
    int r = uv_spawn(uv_default_loop(), processHandle_, &options);
    if (r < 0) {
        // Emit error event
        TsValue* err = (TsValue*)ts_error_create(TsString::Create(uv_strerror(r)));
        void* errArgs[] = { err };
        Emit("error", 1, errArgs);
        return r;
    }

    pid_ = processHandle_->pid;
    connected_ = true;
    ipcConnected_ = true;

    // Create wrapper stream objects
    stdin_ = new (ts_alloc(sizeof(TsChildProcessWritable)))
        TsChildProcessWritable(stdinPipe_, this);
    stdout_ = new (ts_alloc(sizeof(TsChildProcessReadable)))
        TsChildProcessReadable(stdoutPipe_, this);
    stderr_ = new (ts_alloc(sizeof(TsChildProcessReadable)))
        TsChildProcessReadable(stderrPipe_, this);

    // Store IPC pipe as channel for external access
    channel_ = ipcPipe_;

    // Start reading from stdout, stderr, and IPC
    uv_read_start((uv_stream_t*)stdoutPipe_, OnAlloc, OnStdoutRead);
    uv_read_start((uv_stream_t*)stderrPipe_, OnAlloc, OnStderrRead);
    uv_read_start((uv_stream_t*)ipcPipe_, OnAlloc, OnIPCRead);

    // Emit spawn event
    Emit("spawn", 0, nullptr);

    return 0;
}

bool TsChildProcess::Kill(const char* signal) {
    if (!signal) signal = "SIGTERM";

    int signum = SIGTERM;  // Default
    if (strcmp(signal, "SIGKILL") == 0 || strcmp(signal, "9") == 0) {
        signum = SIGKILL;
    } else if (strcmp(signal, "SIGINT") == 0 || strcmp(signal, "2") == 0) {
        signum = SIGINT;
    } else if (strcmp(signal, "SIGTERM") == 0 || strcmp(signal, "15") == 0) {
        signum = SIGTERM;
    }

    return Kill(signum);
}

bool TsChildProcess::Kill(int signal) {
    if (!processHandle_ || killed_) {
        return false;
    }

    int r = uv_process_kill(processHandle_, signal);
    if (r == 0) {
        killed_ = true;
        return true;
    }
    return false;
}

void* TsChildProcess::GetSpawnargs() const {
    TsArray* arr = TsArray::Create();
    for (const auto& arg : spawnargs_) {
        arr->Push((int64_t)TsString::Create(arg.c_str()));
    }
    return arr;
}

bool TsChildProcess::Send(void* message, void* sendHandle) {
    if (!ipcPipe_ || !ipcConnected_) {
        return false;
    }

    // Serialize message to JSON
    TsValue* msgVal = (TsValue*)message;
    TsString* jsonStr = ts_json_stringify(msgVal);
    if (!jsonStr) {
        return false;
    }

    const char* jsonData = jsonStr->ToUtf8();
    size_t jsonLen = strlen(jsonData);

    // Create length-prefixed message: 4-byte little-endian length + JSON data
    size_t totalLen = 4 + jsonLen;
    char* buffer = (char*)ts_alloc(totalLen);

    // Write length as 4-byte little-endian
    buffer[0] = (char)(jsonLen & 0xFF);
    buffer[1] = (char)((jsonLen >> 8) & 0xFF);
    buffer[2] = (char)((jsonLen >> 16) & 0xFF);
    buffer[3] = (char)((jsonLen >> 24) & 0xFF);

    // Copy JSON data
    memcpy(buffer + 4, jsonData, jsonLen);

    // Write to IPC pipe
    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    req->data = this;

    uv_buf_t buf = uv_buf_init(buffer, (unsigned int)totalLen);
    int r = uv_write(req, (uv_stream_t*)ipcPipe_, &buf, 1, OnIPCWrite);

    return r == 0;
}

void TsChildProcess::Disconnect() {
    if (ipcConnected_ && ipcPipe_) {
        ipcConnected_ = false;
        uv_read_stop((uv_stream_t*)ipcPipe_);
        uv_close((uv_handle_t*)ipcPipe_, OnClose);
        ipcPipe_ = nullptr;
        channel_ = nullptr;
    }
    connected_ = false;
    Emit("disconnect", 0, nullptr);
}

void TsChildProcess::Ref() {
    if (!referenced_ && processHandle_) {
        uv_ref((uv_handle_t*)processHandle_);
        referenced_ = true;
    }
}

void TsChildProcess::Unref() {
    if (referenced_ && processHandle_) {
        uv_unref((uv_handle_t*)processHandle_);
        referenced_ = false;
    }
}

void TsChildProcess::OnExit(int64_t exitStatus, int termSignal) {
    exitCode_ = (int)exitStatus;
    connected_ = false;

    if (termSignal != 0) {
        // Convert signal number to name
        switch (termSignal) {
            case SIGTERM: signalCode_ = "SIGTERM"; break;
            case SIGKILL: signalCode_ = "SIGKILL"; break;
            case SIGINT: signalCode_ = "SIGINT"; break;
            default: signalCode_ = ""; break;
        }
    }

    // Call exec callback if set (error, stdout, stderr)
    if (execCallback_) {
        TsValue* error = nullptr;
        if (exitCode_ != 0) {
            std::string errMsg = "Command failed with exit code " + std::to_string(exitCode_);
            error = (TsValue*)ts_error_create(TsString::Create(errMsg.c_str()));
        }

        // Create stdout buffer
        TsBuffer* stdoutBuf = TsBuffer::Create(accumulatedStdout_.length());
        if (accumulatedStdout_.length() > 0) {
            memcpy(stdoutBuf->GetData(), accumulatedStdout_.c_str(), accumulatedStdout_.length());
        }

        // Create stderr buffer
        TsBuffer* stderrBuf = TsBuffer::Create(accumulatedStderr_.length());
        if (accumulatedStderr_.length() > 0) {
            memcpy(stderrBuf->GetData(), accumulatedStderr_.c_str(), accumulatedStderr_.length());
        }

        TsValue* cbArgs[] = {
            error ? error : ts_value_make_null(),
            ts_value_make_object(stdoutBuf),
            ts_value_make_object(stderrBuf)
        };
        ts_function_call((TsValue*)execCallback_, 3, cbArgs);
        execCallback_ = nullptr;  // Clear callback after calling
    }

    // Call exit callback if set (used by cluster)
    if (exitCallback_) {
        exitCallback_(exitCallbackContext_, exitCode_, signalCode_.empty() ? nullptr : signalCode_.c_str());
    }

    // Emit exit event with (code, signal)
    TsValue* codeArg = (exitCode_ >= 0) ? ts_value_make_int(exitCode_) : ts_value_make_null();
    TsValue* signalArg = signalCode_.empty() ? ts_value_make_null() :
        ts_value_make_string(TsString::Create(signalCode_.c_str()));
    void* exitArgs[] = { codeArg, signalArg };
    Emit("exit", 2, exitArgs);

    // Emit close event after stdio closes
    Emit("close", 2, exitArgs);
}

// Static callbacks
void TsChildProcess::OnProcessExit(uv_process_t* handle, int64_t exitStatus, int termSignal) {
    TsChildProcess* self = (TsChildProcess*)handle->data;
    if (self) {
        self->OnExit(exitStatus, termSignal);
    }

    // Close the process handle
    uv_close((uv_handle_t*)handle, OnClose);
}

void TsChildProcess::OnAlloc(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggestedSize);
    buf->len = (unsigned int)suggestedSize;
}

void TsChildProcess::OnStdoutRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TsChildProcess* self = (TsChildProcess*)stream->data;
    if (!self || !self->stdout_) return;

    TsChildProcessReadable* readable = (TsChildProcessReadable*)self->stdout_;
    if (nread > 0) {
        // Accumulate for exec callback if set
        if (self->execCallback_) {
            self->AppendStdout(buf->base, nread);
        }
        readable->HandleData(buf->base, nread);
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            readable->HandleEnd();
        } else {
            readable->HandleError((int)nread);
        }
    }
}

void TsChildProcess::OnStderrRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TsChildProcess* self = (TsChildProcess*)stream->data;
    if (!self || !self->stderr_) return;

    TsChildProcessReadable* readable = (TsChildProcessReadable*)self->stderr_;
    if (nread > 0) {
        // Accumulate for exec callback if set
        if (self->execCallback_) {
            self->AppendStderr(buf->base, nread);
        }
        readable->HandleData(buf->base, nread);
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            readable->HandleEnd();
        } else {
            readable->HandleError((int)nread);
        }
    }
}

void TsChildProcess::OnStdinWrite(uv_write_t* req, int status) {
    // Write completed - req->data contains context if needed
}

void TsChildProcess::OnIPCRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TsChildProcess* self = (TsChildProcess*)stream->data;
    if (!self) return;

    if (nread > 0) {
        // Append to IPC read buffer
        self->ipcReadBuffer_.append(buf->base, nread);

        // Process complete messages from buffer
        self->ProcessIPCMessage(nullptr, 0);
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            // IPC pipe closed - emit disconnect
            self->ipcConnected_ = false;
            self->Emit("disconnect", 0, nullptr);
        } else {
            // Error reading IPC
            TsValue* err = (TsValue*)ts_error_create(TsString::Create(uv_strerror((int)nread)));
            void* errArgs[] = { err };
            self->Emit("error", 1, errArgs);
        }
    }
}

void TsChildProcess::OnIPCWrite(uv_write_t* req, int status) {
    // IPC write completed
    if (status < 0) {
        TsChildProcess* self = (TsChildProcess*)req->data;
        if (self) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create(uv_strerror(status)));
            void* errArgs[] = { err };
            self->Emit("error", 1, errArgs);
        }
    }
}

void TsChildProcess::ProcessIPCMessage(const char* data, size_t length) {
    // Process length-prefixed messages from ipcReadBuffer_
    // Format: 4-byte little-endian length + JSON data

    while (ipcReadBuffer_.length() >= 4) {
        // Read message length (little-endian)
        uint32_t msgLen =
            ((uint8_t)ipcReadBuffer_[0]) |
            (((uint8_t)ipcReadBuffer_[1]) << 8) |
            (((uint8_t)ipcReadBuffer_[2]) << 16) |
            (((uint8_t)ipcReadBuffer_[3]) << 24);

        // Check if we have the full message
        if (ipcReadBuffer_.length() < 4 + msgLen) {
            break;  // Wait for more data
        }

        // Extract JSON string
        std::string jsonStr = ipcReadBuffer_.substr(4, msgLen);

        // Remove processed message from buffer
        ipcReadBuffer_.erase(0, 4 + msgLen);

        // Parse JSON and emit 'message' event
        TsString* jsonTsStr = TsString::Create(jsonStr.c_str());
        TsValue* jsonVal = ts_value_make_string(jsonTsStr);
        TsValue* parsedMsg = ts_json_parse(jsonVal);

        if (parsedMsg) {
            OnIPCMessage(parsedMsg);
        }
    }
}

void TsChildProcess::OnIPCMessage(void* message) {
    // Call message callback if set (used by cluster)
    if (messageCallback_) {
        messageCallback_(messageCallbackContext_, message);
    }

    // Emit 'message' event with the parsed message
    void* args[] = { message };
    Emit("message", 1, args);
}

void TsChildProcess::OnClose(uv_handle_t* handle) {
    // Handle closed
}

// ============================================================================
// TsChildProcessWritable Implementation
// ============================================================================

TsChildProcessWritable::TsChildProcessWritable(uv_pipe_t* pipe, TsChildProcess* parent)
    : pipe_(pipe)
    , parent_(parent)
{
    this->magic = 0x43505753; // "CPWS" - Child Process Writable Stream
}

TsChildProcessWritable::~TsChildProcessWritable() {
    if (pipe_ && !destroyed_) {
        Destroy();
    }
}

bool TsChildProcessWritable::Write(void* data, size_t length) {
    if (destroyed_ || ended_ || !pipe_) return false;

    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    req->data = this;

    // Copy data to ensure it stays valid
    char* dataCopy = (char*)ts_alloc(length);
    memcpy(dataCopy, data, length);

    uv_buf_t buf = uv_buf_init(dataCopy, (unsigned int)length);
    int r = uv_write(req, (uv_stream_t*)pipe_, &buf, 1, TsChildProcess::OnStdinWrite);

    return r == 0;
}

void TsChildProcessWritable::End() {
    if (ended_ || destroyed_) return;
    ended_ = true;

    // Close the write end of the pipe
    if (pipe_) {
        uv_close((uv_handle_t*)pipe_, TsChildProcess::OnClose);
        pipe_ = nullptr;
    }

    finished_ = true;
    Emit("finish", 0, nullptr);
}

void TsChildProcessWritable::Destroy() {
    if (destroyed_) return;
    destroyed_ = true;

    if (pipe_) {
        uv_close((uv_handle_t*)pipe_, TsChildProcess::OnClose);
        pipe_ = nullptr;
    }

    Emit("close", 0, nullptr);
}

// ============================================================================
// TsChildProcessReadable Implementation
// ============================================================================

TsChildProcessReadable::TsChildProcessReadable(uv_pipe_t* pipe, TsChildProcess* parent)
    : pipe_(pipe)
    , parent_(parent)
{
    this->magic = 0x43505253; // "CPRS" - Child Process Readable Stream
    flowing = true;  // Start in flowing mode
}

TsChildProcessReadable::~TsChildProcessReadable() {
    if (pipe_ && !destroyed_) {
        Destroy();
    }
}

void TsChildProcessReadable::Pause() {
    if (!flowing || !pipe_) return;
    flowing = false;
    paused_ = true;
    uv_read_stop((uv_stream_t*)pipe_);
}

void TsChildProcessReadable::Resume() {
    if (flowing || !pipe_ || destroyed_) return;
    flowing = true;
    paused_ = false;
    uv_read_start((uv_stream_t*)pipe_, TsChildProcess::OnAlloc,
        (pipe_ == parent_->stdoutPipe_) ? TsChildProcess::OnStdoutRead : TsChildProcess::OnStderrRead);
}

void TsChildProcessReadable::Destroy() {
    if (destroyed_) return;
    destroyed_ = true;

    if (pipe_) {
        uv_read_stop((uv_stream_t*)pipe_);
        uv_close((uv_handle_t*)pipe_, TsChildProcess::OnClose);
        pipe_ = nullptr;
    }

    Emit("close", 0, nullptr);
}

void TsChildProcessReadable::HandleData(void* data, size_t length) {
    if (destroyed_) return;

    // Create a Buffer with the data
    TsBuffer* buffer = TsBuffer::Create(length);
    memcpy(buffer->GetData(), data, length);

    TsValue* arg = ts_value_make_object(buffer);
    void* args[] = { arg };
    Emit("data", 1, args);
}

void TsChildProcessReadable::HandleEnd() {
    if (destroyed_ || ended_) return;
    ended_ = true;
    Emit("end", 0, nullptr);
}

void TsChildProcessReadable::HandleError(int status) {
    if (destroyed_) return;

    TsValue* err = (TsValue*)ts_error_create(TsString::Create(uv_strerror(status)));
    void* errArgs[] = { err };
    Emit("error", 1, errArgs);
}

