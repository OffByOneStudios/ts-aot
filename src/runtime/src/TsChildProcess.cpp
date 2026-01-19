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
    // IPC not implemented yet
    return false;
}

void TsChildProcess::Disconnect() {
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

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

void* ts_child_process_spawn(void* command, void* args, void* options) {
    // Unbox command string - use ts_value_get_string for STRING_PTR type
    TsString* cmdStr = (TsString*)ts_value_get_string((TsValue*)command);
    const char* cmd = cmdStr ? cmdStr->ToUtf8() : nullptr;
    if (!cmd) return ts_value_make_null();

    // Unbox args array
    std::vector<std::string> argsVec;
    if (args) {
        void* rawArgs = ts_value_get_object((TsValue*)args);
        if (!rawArgs) rawArgs = args;

        // Check if it's a TsArray by magic number
        uint32_t magic = *(uint32_t*)rawArgs;
        if (magic == 0x41525259) { // TsArray::MAGIC
            TsArray* argsArr = (TsArray*)rawArgs;
            for (int64_t i = 0; i < argsArr->Length(); i++) {
                int64_t val = argsArr->Get(i);
                // Array elements are stored as raw int64_t which may be boxed TsValue* or raw TsString*
                TsString* argStr = (TsString*)ts_value_get_string((TsValue*)(void*)val);
                if (argStr) {
                    argsVec.push_back(argStr->ToUtf8());
                }
            }
        }
    }

    // Parse options if provided
    const char* cwd = nullptr;
    char** env = nullptr;
    bool detached = false;
    bool windowsHide = false;

    if (options) {
        void* rawOpts = ts_value_get_object((TsValue*)options);
        if (!rawOpts) rawOpts = options;

        // Check for TsMap by magic number at various offsets
        uint32_t magic16 = *(uint32_t*)((char*)rawOpts + 16);
        if (magic16 == 0x4D415053) { // TsMap::MAGIC "MAPS"
            TsMap* optsMap = (TsMap*)rawOpts;

            // Get cwd option
            TsValue cwdKey(TsString::Create("cwd"));
            TsValue cwdVal = optsMap->Get(cwdKey);
            if (cwdVal.type == ValueType::STRING_PTR) {
                TsString* cwdStr = (TsString*)cwdVal.ptr_val;
                if (cwdStr) cwd = cwdStr->ToUtf8();
            }

            // Get detached option
            TsValue detachedKey(TsString::Create("detached"));
            TsValue detachedVal = optsMap->Get(detachedKey);
            if (detachedVal.type == ValueType::BOOLEAN) {
                detached = detachedVal.b_val;
            }

            // Get windowsHide option
            TsValue hideKey(TsString::Create("windowsHide"));
            TsValue hideVal = optsMap->Get(hideKey);
            if (hideVal.type == ValueType::BOOLEAN) {
                windowsHide = hideVal.b_val;
            }
        }
    }

    // Create and spawn the process
    TsChildProcess* cp = new (ts_alloc(sizeof(TsChildProcess))) TsChildProcess();
    int result = cp->Spawn(cmd, argsVec, cwd, env, detached, windowsHide);

    if (result < 0) {
        // Spawn failed - error already emitted, but return the object anyway
        // (matches Node.js behavior where ChildProcess is returned even on error)
    }

    return ts_value_make_object(cp);
}

void* ts_child_process_exec(void* command, void* options, void* callback) {
    // Get the shell command
    void* rawCmd = ts_value_get_object((TsValue*)command);
    if (!rawCmd) rawCmd = command;
    TsString* cmdStr = (TsString*)rawCmd;
    const char* cmd = cmdStr ? cmdStr->ToUtf8() : nullptr;
    if (!cmd) return ts_value_make_null();

#ifdef _WIN32
    const char* shell = "cmd.exe";
    std::vector<std::string> args = { "/c", cmd };
#else
    const char* shell = "/bin/sh";
    std::vector<std::string> args = { "-c", cmd };
#endif

    TsChildProcess* cp = new (ts_alloc(sizeof(TsChildProcess))) TsChildProcess();
    cp->Spawn(shell, args, nullptr, nullptr, false, false);

    // Store callback for when process exits (to be called with stdout/stderr)
    // For now, just return the ChildProcess
    return ts_value_make_object(cp);
}

void* ts_child_process_exec_sync(void* command, void* options) {
    // Get the shell command
    void* rawCmd = ts_value_get_object((TsValue*)command);
    if (!rawCmd) rawCmd = command;
    TsString* cmdStr = (TsString*)rawCmd;
    const char* cmd = cmdStr ? cmdStr->ToUtf8() : nullptr;
    if (!cmd) return ts_value_make_null();

#ifdef _WIN32
    const char* shell = "cmd.exe";
    std::string fullCmd = std::string(shell) + " /c " + cmd;
#else
    const char* shell = "/bin/sh";
    std::string fullCmd = std::string(shell) + " -c \"" + cmd + "\"";
#endif

    // Use popen for synchronous execution
#ifdef _WIN32
    FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
    FILE* pipe = popen(fullCmd.c_str(), "r");
#endif
    if (!pipe) {
        return ts_error_create(TsString::Create("Failed to execute command"));
    }

    // Read output
    std::string output;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
#endif
    (void)status;  // TODO: Check exit status

    // Return output as Buffer
    TsBuffer* result = TsBuffer::Create(output.length());
    memcpy(result->GetData(), output.c_str(), output.length());
    return ts_value_make_object(result);
}

void* ts_child_process_exec_file(void* file, void* args, void* options, void* callback) {
    // Similar to spawn but returns ChildProcess with callback support
    return ts_child_process_spawn(file, args, options);
}

void* ts_child_process_exec_file_sync(void* file, void* args, void* options) {
    // TODO: Implement synchronous file execution
    return ts_value_make_null();
}

void* ts_child_process_fork(void* modulePath, void* args, void* options) {
    // TODO: Implement fork with IPC
    return ts_value_make_null();
}

void* ts_child_process_spawn_sync(void* command, void* args, void* options) {
    // TODO: Implement synchronous spawn
    return ts_value_make_null();
}

bool ts_child_process_kill(void* cp, void* signal) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return false;

    const char* sig = "SIGTERM";
    if (signal) {
        void* rawSig = ts_value_get_object((TsValue*)signal);
        if (!rawSig) rawSig = signal;
        TsString* sigStr = (TsString*)rawSig;
        if (sigStr) sig = sigStr->ToUtf8();
    }

    return proc->Kill(sig);
}

int64_t ts_child_process_get_pid(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    return proc ? proc->GetPid() : -1;
}

bool ts_child_process_get_connected(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    return proc ? proc->IsConnected() : false;
}

bool ts_child_process_get_killed(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    return proc ? proc->IsKilled() : false;
}

void* ts_child_process_get_exit_code(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return ts_value_make_null();
    int code = proc->GetExitCode();
    return (code >= 0) ? ts_value_make_int(code) : ts_value_make_null();
}

void* ts_child_process_get_signal_code(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return ts_value_make_null();
    const char* sig = proc->GetSignalCode();
    return (sig && strlen(sig) > 0) ? ts_value_make_string(TsString::Create(sig)) : ts_value_make_null();
}

void* ts_child_process_get_spawnfile(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return nullptr;
    const char* file = proc->GetSpawnfile();
    // Return raw TsString* since the analyzer types spawnfile as TypeKind::String
    return TsString::Create(file);
}

void* ts_child_process_get_spawnargs(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return ts_value_make_null();
    return ts_value_make_object(proc->GetSpawnargs());
}

void* ts_child_process_get_stdin(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetStdin()) return ts_value_make_null();
    return ts_value_make_object(proc->GetStdin());
}

void* ts_child_process_get_stdout(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetStdout()) return ts_value_make_null();
    return ts_value_make_object(proc->GetStdout());
}

void* ts_child_process_get_stderr(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetStderr()) return ts_value_make_null();
    return ts_value_make_object(proc->GetStderr());
}

void* ts_child_process_get_channel(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetChannel()) return ts_value_make_null();
    return ts_value_make_object(proc->GetChannel());
}

bool ts_child_process_send(void* cp, void* message, void* sendHandle) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return false;
    return proc->Send(message, sendHandle);
}

void ts_child_process_disconnect(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (proc) proc->Disconnect();
}

void* ts_child_process_ref(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (proc) proc->Ref();
    return cp;
}

void* ts_child_process_unref(void* cp) {
    void* rawCp = ts_value_get_object((TsValue*)cp);
    if (!rawCp) rawCp = cp;
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (proc) proc->Unref();
    return cp;
}

} // extern "C"
