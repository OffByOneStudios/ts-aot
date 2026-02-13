// Child Process extension for ts-aot
// Migrated from src/runtime/src/TsChildProcess.cpp

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
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define SIGTERM 15
#define SIGKILL 9
#define SIGINT 2
#else
#include <signal.h>
#include <sys/wait.h>
#endif

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
        void* rawArgs = ts_nanbox_safe_unbox(args);

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
        void* rawOpts = ts_nanbox_safe_unbox(options);

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
    // Get the shell command - use ts_value_get_string for STRING_PTR type
    TsString* cmdStr = (TsString*)ts_value_get_string((TsValue*)command);
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

    // Set the callback to be invoked when process exits
    if (callback) {
        cp->SetExecCallback(callback);
    }

    cp->Spawn(shell, args, nullptr, nullptr, false, false);

    return ts_value_make_object(cp);
}

void* ts_child_process_exec_sync(void* command, void* options) {
    // Get the shell command
    void* rawCmd = ts_nanbox_safe_unbox(command);
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
    // Unbox file string
    TsString* fileStr = (TsString*)ts_value_get_string((TsValue*)file);
    const char* fileCmd = fileStr ? fileStr->ToUtf8() : nullptr;
    if (!fileCmd) {
        return ts_error_create(TsString::Create("Invalid file path"));
    }

    // Unbox args array
    std::vector<std::string> argsVec;
    if (args) {
        void* rawArgs = ts_nanbox_safe_unbox(args);

        uint32_t magic = *(uint32_t*)rawArgs;
        if (magic == 0x41525259) { // TsArray::MAGIC
            TsArray* argsArr = (TsArray*)rawArgs;
            for (int64_t i = 0; i < argsArr->Length(); i++) {
                int64_t val = argsArr->Get(i);
                TsString* argStr = (TsString*)ts_value_get_string((TsValue*)(void*)val);
                if (argStr) {
                    argsVec.push_back(argStr->ToUtf8());
                }
            }
        }
    }

    // Build command line - execFile executes directly without shell
#ifdef _WIN32
    std::string fullCmd = fileCmd;
    for (const auto& arg : argsVec) {
        fullCmd += " ";
        // Quote arguments that contain spaces
        if (arg.find(' ') != std::string::npos) {
            fullCmd += "\"" + arg + "\"";
        } else {
            fullCmd += arg;
        }
    }
#else
    std::string fullCmd = fileCmd;
    for (const auto& arg : argsVec) {
        fullCmd += " ";
        // Escape single quotes and wrap in single quotes
        std::string escaped = arg;
        size_t pos = 0;
        while ((pos = escaped.find("'", pos)) != std::string::npos) {
            escaped.replace(pos, 1, "'\\''");
            pos += 4;
        }
        fullCmd += "'" + escaped + "'";
    }
#endif

    // Use popen for synchronous execution
#ifdef _WIN32
    FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
    FILE* pipe = popen(fullCmd.c_str(), "r");
#endif

    if (!pipe) {
        return ts_error_create(TsString::Create("Failed to execute file"));
    }

    // Read stdout
    std::string stdoutStr;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        stdoutStr += buffer;
    }

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
#endif
    (void)status;  // execFileSync throws on non-zero, but we'll just return output for now

    // Return output as Buffer
    TsBuffer* result = TsBuffer::Create(stdoutStr.length());
    if (stdoutStr.length() > 0) {
        memcpy(result->GetData(), stdoutStr.c_str(), stdoutStr.length());
    }
    return ts_value_make_object(result);
}

void* ts_child_process_fork(void* modulePath, void* args, void* options) {
    // Get the executable path (pre-compiled ts-aot binary)
    TsString* pathStr = (TsString*)ts_value_get_string((TsValue*)modulePath);
    const char* execPath = pathStr ? pathStr->ToUtf8() : nullptr;
    if (!execPath) {
        return ts_value_make_null();
    }

    // Unbox args array
    std::vector<std::string> argsVec;
    if (args) {
        void* rawArgs = ts_nanbox_safe_unbox(args);

        uint32_t magic = *(uint32_t*)rawArgs;
        if (magic == 0x41525259) { // TsArray::MAGIC
            TsArray* argsArr = (TsArray*)rawArgs;
            for (int64_t i = 0; i < argsArr->Length(); i++) {
                int64_t val = argsArr->Get(i);
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

    if (options) {
        void* rawOpts = ts_nanbox_safe_unbox(options);

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
        }
    }

    // Create and spawn the process with IPC
    TsChildProcess* cp = new (ts_alloc(sizeof(TsChildProcess))) TsChildProcess();
    int result = cp->SpawnWithIPC(execPath, argsVec, cwd, env, detached, false);

    if (result < 0) {
        // Spawn failed - error already emitted
    }

    return ts_value_make_object(cp);
}

void* ts_child_process_spawn_sync(void* command, void* args, void* options) {
    // Unbox command string
    TsString* cmdStr = (TsString*)ts_value_get_string((TsValue*)command);
    const char* cmd = cmdStr ? cmdStr->ToUtf8() : nullptr;
    if (!cmd) {
        TsMap* result = TsMap::Create();
        TsValue errKey(TsString::Create("error"));
        TsValue errVal;
        errVal.type = ValueType::OBJECT_PTR;
        errVal.ptr_val = ts_error_create(TsString::Create("Invalid command"));
        result->Set(errKey, errVal);
        return ts_value_make_object(result);
    }

    // Unbox args array
    std::vector<std::string> argsVec;
    if (args) {
        void* rawArgs = ts_nanbox_safe_unbox(args);

        uint32_t magic = *(uint32_t*)rawArgs;
        if (magic == 0x41525259) { // TsArray::MAGIC
            TsArray* argsArr = (TsArray*)rawArgs;
            for (int64_t i = 0; i < argsArr->Length(); i++) {
                int64_t val = argsArr->Get(i);
                TsString* argStr = (TsString*)ts_value_get_string((TsValue*)(void*)val);
                if (argStr) {
                    argsVec.push_back(argStr->ToUtf8());
                }
            }
        }
    }

    // Build command line for shell execution
#ifdef _WIN32
    std::string fullCmd = cmd;
    for (const auto& arg : argsVec) {
        fullCmd += " ";
        // Quote arguments that contain spaces
        if (arg.find(' ') != std::string::npos) {
            fullCmd += "\"" + arg + "\"";
        } else {
            fullCmd += arg;
        }
    }
#else
    std::string fullCmd = cmd;
    for (const auto& arg : argsVec) {
        fullCmd += " ";
        // Escape single quotes and wrap in single quotes
        std::string escaped = arg;
        size_t pos = 0;
        while ((pos = escaped.find("'", pos)) != std::string::npos) {
            escaped.replace(pos, 1, "'\\''");
            pos += 4;
        }
        fullCmd += "'" + escaped + "'";
    }
#endif

    // Use popen for synchronous execution with output capture
#ifdef _WIN32
    FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
    FILE* pipe = popen(fullCmd.c_str(), "r");
#endif

    // Create result object
    TsMap* result = TsMap::Create();

    if (!pipe) {
        TsValue errKey(TsString::Create("error"));
        TsValue errVal;
        errVal.type = ValueType::OBJECT_PTR;
        errVal.ptr_val = ts_error_create(TsString::Create("Failed to spawn process"));
        result->Set(errKey, errVal);
        return ts_value_make_object(result);
    }

    // Read stdout
    std::string stdoutStr;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        stdoutStr += buffer;
    }

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
#endif

    // Set pid (not available from popen, use 0)
    TsValue pidKey(TsString::Create("pid"));
    TsValue pidVal;
    pidVal.type = ValueType::NUMBER_INT;
    pidVal.i_val = 0;
    result->Set(pidKey, pidVal);

    // Set stdout as Buffer
    TsValue stdoutKey(TsString::Create("stdout"));
    TsBuffer* stdoutBuf = TsBuffer::Create(stdoutStr.length());
    if (stdoutStr.length() > 0) {
        memcpy(stdoutBuf->GetData(), stdoutStr.c_str(), stdoutStr.length());
    }
    TsValue stdoutVal;
    stdoutVal.type = ValueType::OBJECT_PTR;
    stdoutVal.ptr_val = stdoutBuf;
    result->Set(stdoutKey, stdoutVal);

    // Set stderr as empty Buffer (popen doesn't capture stderr separately)
    TsValue stderrKey(TsString::Create("stderr"));
    TsBuffer* stderrBuf = TsBuffer::Create(0);
    TsValue stderrVal;
    stderrVal.type = ValueType::OBJECT_PTR;
    stderrVal.ptr_val = stderrBuf;
    result->Set(stderrKey, stderrVal);

    // Set status (exit code)
    TsValue statusKey(TsString::Create("status"));
    TsValue statusVal;
#ifdef _WIN32
    statusVal.type = ValueType::NUMBER_INT;
    statusVal.i_val = status;
#else
    if (WIFEXITED(status)) {
        statusVal.type = ValueType::NUMBER_INT;
        statusVal.i_val = WEXITSTATUS(status);
    } else {
        statusVal.type = ValueType::UNDEFINED;
    }
#endif
    result->Set(statusKey, statusVal);

    // Set signal (null if exited normally)
    TsValue signalKey(TsString::Create("signal"));
    TsValue signalVal;
#ifndef _WIN32
    if (WIFSIGNALED(status)) {
        signalVal.type = ValueType::STRING_PTR;
        signalVal.ptr_val = TsString::Create(strsignal(WTERMSIG(status)));
    } else {
        signalVal.type = ValueType::UNDEFINED;
    }
#else
    signalVal.type = ValueType::UNDEFINED;
#endif
    result->Set(signalKey, signalVal);

    // Set output array [null, stdout, stderr]
    TsValue outputKey(TsString::Create("output"));
    TsArray* outputArr = TsArray::Create();
    outputArr->Push(0);  // null for stdin
    outputArr->Push((int64_t)stdoutBuf);
    outputArr->Push((int64_t)stderrBuf);
    TsValue outputVal;
    outputVal.type = ValueType::ARRAY_PTR;
    outputVal.ptr_val = outputArr;
    result->Set(outputKey, outputVal);

    return ts_value_make_object(result);
}

bool ts_child_process_kill(void* cp, void* signal) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return false;

    const char* sig = "SIGTERM";
    if (signal) {
        void* rawSig = ts_nanbox_safe_unbox(signal);
        TsString* sigStr = (TsString*)rawSig;
        if (sigStr) sig = sigStr->ToUtf8();
    }

    return proc->Kill(sig);
}

int64_t ts_child_process_get_pid(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    return proc ? proc->GetPid() : -1;
}

bool ts_child_process_get_connected(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    return proc ? proc->IsConnected() : false;
}

bool ts_child_process_get_killed(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    return proc ? proc->IsKilled() : false;
}

void* ts_child_process_get_exit_code(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return ts_value_make_null();
    int code = proc->GetExitCode();
    return (code >= 0) ? ts_value_make_int(code) : ts_value_make_null();
}

void* ts_child_process_get_signal_code(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return ts_value_make_null();
    const char* sig = proc->GetSignalCode();
    return (sig && strlen(sig) > 0) ? ts_value_make_string(TsString::Create(sig)) : ts_value_make_null();
}

void* ts_child_process_get_spawnfile(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return nullptr;
    const char* file = proc->GetSpawnfile();
    // Return raw TsString* since the analyzer types spawnfile as TypeKind::String
    return TsString::Create(file);
}

void* ts_child_process_get_spawnargs(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return ts_value_make_null();
    return ts_value_make_object(proc->GetSpawnargs());
}

void* ts_child_process_get_stdin(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetStdin()) return ts_value_make_null();
    return ts_value_make_object(proc->GetStdin());
}

void* ts_child_process_get_stdout(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetStdout()) return ts_value_make_null();
    return ts_value_make_object(proc->GetStdout());
}

void* ts_child_process_get_stderr(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetStderr()) return ts_value_make_null();
    return ts_value_make_object(proc->GetStderr());
}

void* ts_child_process_get_channel(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc || !proc->GetChannel()) return ts_value_make_null();
    return ts_value_make_object(proc->GetChannel());
}

void* ts_child_process_get_stdio(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return ts_value_make_null();

    // Create array [stdin, stdout, stderr]
    TsArray* stdioArr = TsArray::Create();

    // Add stdin (index 0)
    if (proc->GetStdin()) {
        stdioArr->Push((int64_t)ts_value_make_object(proc->GetStdin()));
    } else {
        stdioArr->Push((int64_t)ts_value_make_null());
    }

    // Add stdout (index 1)
    if (proc->GetStdout()) {
        stdioArr->Push((int64_t)ts_value_make_object(proc->GetStdout()));
    } else {
        stdioArr->Push((int64_t)ts_value_make_null());
    }

    // Add stderr (index 2)
    if (proc->GetStderr()) {
        stdioArr->Push((int64_t)ts_value_make_object(proc->GetStderr()));
    } else {
        stdioArr->Push((int64_t)ts_value_make_null());
    }

    return ts_value_make_object(stdioArr);
}

bool ts_child_process_send(void* cp, void* message, void* sendHandle) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (!proc) return false;
    return proc->Send(message, sendHandle);
}

void ts_child_process_disconnect(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (proc) proc->Disconnect();
}

void* ts_child_process_ref(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (proc) proc->Ref();
    return cp;
}

void* ts_child_process_unref(void* cp) {
    void* rawCp = ts_nanbox_safe_unbox(cp);
    TsChildProcess* proc = dynamic_cast<TsChildProcess*>((TsObject*)rawCp);
    if (proc) proc->Unref();
    return cp;
}

} // extern "C"
