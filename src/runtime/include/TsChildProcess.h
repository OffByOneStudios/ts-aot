#pragma once
#include "TsEventEmitter.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include <uv.h>
#include <string>
#include <vector>

/*
 * TsChildProcess - Node.js child_process.ChildProcess implementation
 *
 * Wraps libuv uv_process_t for spawning and managing child processes.
 * Inherits from TsEventEmitter to emit events:
 *   - 'spawn': Emitted when process is successfully spawned
 *   - 'exit': Emitted when process exits (code, signal)
 *   - 'close': Emitted when stdio streams are closed
 *   - 'error': Emitted on spawn or runtime errors
 *   - 'disconnect': Emitted when IPC channel closes
 *   - 'message': Emitted when IPC message received
 */

// Forward declaration for stdio streams
class TsChildProcessWritable;
class TsChildProcessReadable;

class TsChildProcess : public TsEventEmitter {
    // Friends for access to protected static callbacks and pipes
    friend class TsChildProcessWritable;
    friend class TsChildProcessReadable;

public:
    static constexpr uint32_t MAGIC = 0x43484C44; // "CHLD"

    TsChildProcess();
    virtual ~TsChildProcess();

    // Safe casting helper
    virtual TsChildProcess* AsChildProcess() { return this; }

    // Spawn a new process
    // Returns 0 on success, negative on error
    int Spawn(const char* file, const std::vector<std::string>& args,
              const char* cwd, char** env, bool detached, bool windowsHide);

    // Kill the process
    bool Kill(const char* signal = "SIGTERM");
    bool Kill(int signal);

    // Properties
    int GetPid() const { return pid_; }
    bool IsConnected() const { return connected_; }
    bool IsKilled() const { return killed_; }
    int GetExitCode() const { return exitCode_; }
    const char* GetSignalCode() const { return signalCode_.c_str(); }
    const char* GetSpawnfile() const { return spawnfile_.c_str(); }
    void* GetSpawnargs() const;  // Returns TsArray

    // Stdio streams (may be null if not piped)
    TsWritable* GetStdin() const { return stdin_; }
    TsReadable* GetStdout() const { return stdout_; }
    TsReadable* GetStderr() const { return stderr_; }

    // Channel for IPC (when using fork)
    void* GetChannel() const { return channel_; }
    bool Send(void* message, void* sendHandle = nullptr);
    void Disconnect();

    // Ref/Unref for keeping event loop alive
    void Ref();
    void Unref();

    // For exec() callback support
    void SetExecCallback(void* callback) { execCallback_ = callback; }
    void AppendStdout(const char* data, size_t len) { accumulatedStdout_.append(data, len); }
    void AppendStderr(const char* data, size_t len) { accumulatedStderr_.append(data, len); }

    // IPC support for fork()
    int SpawnWithIPC(const char* file, const std::vector<std::string>& args,
                     const char* cwd, char** env, bool detached, bool windowsHide);
    bool HasIPC() const { return ipcPipe_ != nullptr; }

protected:
    uv_process_t* processHandle_;
    uv_pipe_t* stdinPipe_;
    uv_pipe_t* stdoutPipe_;
    uv_pipe_t* stderrPipe_;
    uv_pipe_t* ipcPipe_;         // IPC pipe for fork()

    TsWritable* stdin_;
    TsReadable* stdout_;
    TsReadable* stderr_;

    int pid_;
    bool connected_;
    bool killed_;
    int exitCode_;
    std::string signalCode_;
    std::string spawnfile_;
    std::vector<std::string> spawnargs_;

    void* channel_;
    bool referenced_;
    bool ipcConnected_;          // IPC connection state
    std::string ipcReadBuffer_;  // Buffer for partial IPC messages

    // For exec() callback support
    void* execCallback_;
    std::string accumulatedStdout_;
    std::string accumulatedStderr_;

    // Internal methods
    void OnExit(int64_t exitStatus, int termSignal);
    void SetupStdio(int stdinMode, int stdoutMode, int stderrMode);
    void CleanupHandles();
    void ProcessIPCMessage(const char* data, size_t length);
    void OnIPCMessage(void* message);

    // Static callbacks for libuv
    static void OnProcessExit(uv_process_t* handle, int64_t exitStatus, int termSignal);
    static void OnAlloc(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnStdoutRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnStderrRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnStdinWrite(uv_write_t* req, int status);
    static void OnIPCRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnIPCWrite(uv_write_t* req, int status);
    static void OnClose(uv_handle_t* handle);
};

// Writable stream for child process stdin
class TsChildProcessWritable : public TsWritable {
public:
    TsChildProcessWritable(uv_pipe_t* pipe, TsChildProcess* parent);
    virtual ~TsChildProcessWritable();

    virtual TsWritable* AsWritable() override { return this; }
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    virtual void Destroy() override;

    uv_pipe_t* GetPipe() const { return pipe_; }

private:
    uv_pipe_t* pipe_;
    TsChildProcess* parent_;
};

// Readable stream for child process stdout/stderr
class TsChildProcessReadable : public TsReadable {
public:
    TsChildProcessReadable(uv_pipe_t* pipe, TsChildProcess* parent);
    virtual ~TsChildProcessReadable();

    virtual TsReadable* AsReadable() override { return this; }
    virtual void Pause() override;
    virtual void Resume() override;
    virtual void Destroy() override;

    void HandleData(void* data, size_t length);
    void HandleEnd();
    void HandleError(int status);

    uv_pipe_t* GetPipe() const { return pipe_; }

private:
    uv_pipe_t* pipe_;
    TsChildProcess* parent_;
};

// C API for child_process module
extern "C" {
    // spawn(command, args, options) - core process spawning
    void* ts_child_process_spawn(void* command, void* args, void* options);

    // exec(command, options, callback) - shell command execution
    void* ts_child_process_exec(void* command, void* options, void* callback);

    // execSync(command, options) - synchronous shell execution
    void* ts_child_process_exec_sync(void* command, void* options);

    // execFile(file, args, options, callback) - execute file directly
    void* ts_child_process_exec_file(void* file, void* args, void* options, void* callback);

    // execFileSync(file, args, options) - synchronous file execution
    void* ts_child_process_exec_file_sync(void* file, void* args, void* options);

    // fork(modulePath, args, options) - fork Node.js process with IPC
    void* ts_child_process_fork(void* modulePath, void* args, void* options);

    // spawnSync(command, args, options) - synchronous spawn
    void* ts_child_process_spawn_sync(void* command, void* args, void* options);

    // ChildProcess instance methods
    bool ts_child_process_kill(void* cp, void* signal);
    int64_t ts_child_process_get_pid(void* cp);
    bool ts_child_process_get_connected(void* cp);
    bool ts_child_process_get_killed(void* cp);
    void* ts_child_process_get_exit_code(void* cp);
    void* ts_child_process_get_signal_code(void* cp);
    void* ts_child_process_get_spawnfile(void* cp);
    void* ts_child_process_get_spawnargs(void* cp);

    // Stdio stream accessors
    void* ts_child_process_get_stdin(void* cp);
    void* ts_child_process_get_stdout(void* cp);
    void* ts_child_process_get_stderr(void* cp);

    // IPC channel and stdio array
    void* ts_child_process_get_channel(void* cp);
    void* ts_child_process_get_stdio(void* cp);
    bool ts_child_process_send(void* cp, void* message, void* sendHandle);
    void ts_child_process_disconnect(void* cp);

    // Ref/Unref
    void* ts_child_process_ref(void* cp);
    void* ts_child_process_unref(void* cp);
}
