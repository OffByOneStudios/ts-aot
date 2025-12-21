#include "IO.h"
#include "TsString.h"
#include "TsPromise.h"
#include "TsMap.h"
#include "TsArray.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <uv.h>
#include <filesystem>

#define GC_THREADS
#include <gc/gc.h>

namespace fs = std::filesystem;

static TsValue* bool_return_helper(void* context) {
    bool val = (bool)(uintptr_t)context;
    return ts_value_make_bool(val);
}

static void add_stats_methods(TsMap* stats, bool isFile, bool isDirectory) {
    stats->Set(TsString::Create("isFile"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isFile));
    stats->Set(TsString::Create("isDirectory"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isDirectory));
}

extern "C" {

void* ts_fs_readFileSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();

    std::ifstream t(pathCStr);
    if (!t.is_open()) {
        std::cerr << "Error: Could not open file " << pathCStr << std::endl;
        return TsString::Create("");
    }

    std::stringstream buffer;
    buffer << t.rdbuf();
    
    return TsString::Create(buffer.str().c_str());
}

void ts_fs_writeFileSync(void* path, void* content) {
    TsString* pathStr = (TsString*)path;
    TsString* contentStr = (TsString*)content;
    const char* pathCStr = pathStr->ToUtf8();
    const char* contentCStr = contentStr->ToUtf8();

    std::ofstream t(pathCStr);
    if (!t.is_open()) {
        std::cerr << "Error: Could not open file for writing " << pathCStr << std::endl;
        return;
    }

    t << contentCStr;
}

bool ts_fs_existsSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    return fs::exists(pathCStr);
}

void ts_fs_mkdirSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    fs::create_directories(pathCStr);
}

void ts_fs_rmdirSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    fs::remove_all(pathCStr);
}

void ts_fs_unlinkSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    fs::remove(pathCStr);
}

void* ts_fs_statSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    
    try {
        auto status = fs::status(pathCStr);
        auto entry = fs::directory_entry(pathCStr);
        
        TsMap* stats = TsMap::Create();
        stats->Set(TsString::Create("size"), TsValue((int64_t)entry.file_size()));
        
        auto mtime = fs::last_write_time(pathCStr);
        auto mtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mtime.time_since_epoch()).count();
        stats->Set(TsString::Create("mtimeMs"), TsValue((double)mtime_ms));
        
        add_stats_methods(stats, fs::is_regular_file(status), fs::is_directory(status));
        
        return ts_value_make_object(stats);
    } catch (...) {
        return ts_value_make_undefined();
    }
}

void* ts_fs_readdirSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    
    TsArray* result = TsArray::Create();
    try {
        for (const auto& entry : fs::directory_iterator(pathCStr)) {
            result->Push((int64_t)ts_value_make_string(TsString::Create(entry.path().filename().string().c_str())));
        }
    } catch (...) {
        // Return empty array or undefined? Node returns error.
    }
    return ts_value_make_object(result);
}

struct ReadDirWork {
    ts::TsPromise* promise;
    std::string path;
    std::vector<std::string> entries;
    bool success;
};

static void readdir_worker(uv_work_t* req) {
    ReadDirWork* work = (ReadDirWork*)req->data;
    try {
        for (const auto& entry : fs::directory_iterator(work->path)) {
            work->entries.push_back(entry.path().filename().string());
        }
        work->success = true;
    } catch (const std::exception& e) {
        std::cerr << "readdir_worker failed: " << e.what() << std::endl;
        work->success = false;
    } catch (...) {
        std::cerr << "readdir_worker failed with unknown error" << std::endl;
        work->success = false;
    }
}

static void readdir_after_worker(uv_work_t* req, int status) {
    ReadDirWork* work = (ReadDirWork*)req->data;
    if (work->success) {
        TsArray* arr = TsArray::Create();
        for (const auto& entry : work->entries) {
            arr->Push((int64_t)ts_value_make_string(TsString::Create(entry.c_str())));
        }
        ts::ts_promise_resolve_internal(work->promise, ts_value_make_object(arr));
    } else {
        void* tsStr = ts_string_create("Could not read directory");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    work->~ReadDirWork();
    GC_free(work);
    free(req);
}

void* ts_fs_readdir_async(void* path) {
    TsString* pathStr = (TsString*)path;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    ReadDirWork* work = (ReadDirWork*)GC_malloc_uncollectable(sizeof(ReadDirWork));
    new (work) ReadDirWork();
    work->promise = promise;
    work->path = pathStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, readdir_worker, readdir_after_worker);
    
    return ts_value_make_promise(promise);
}

static TsString* UnboxString(void* ptr) {
    if (!ptr) return nullptr;
    uintptr_t val = *(uintptr_t*)ptr;
    // Heuristic: pointers are large, TsValue header (type + padding) is small
    if (val < 0x10000) {
        TsValue* v = (TsValue*)ptr;
        if (v->type == ValueType::STRING_PTR) {
            return (TsString*)v->ptr_val;
        }
        return nullptr; 
    }
    return (TsString*)ptr;
}

void* ts_path_join(void* path1, void* path2) {
    TsString* p1 = UnboxString(path1);
    TsString* p2 = UnboxString(path2);
    
    if (!p1 || !p2) {
        std::cerr << "ts_path_join: Invalid arguments" << std::endl;
        return TsString::Create("");
    }
    
    const char* s1 = p1->ToUtf8();
    const char* s2 = p2->ToUtf8();
    std::cerr << "ts_path_join: '" << s1 << "' + '" << s2 << "'" << std::endl;

    fs::path path = fs::path(s1) / fs::path(s2);
    std::string res = path.string();
    std::cerr << "ts_path_join result: '" << res << "'" << std::endl;
    return TsString::Create(res.c_str());
}

struct ReadFileWork {
    ts::TsPromise* promise;
    std::string path;
    std::string result;
    bool success;
};

static void read_file_worker(uv_work_t* req) {
    ReadFileWork* work = (ReadFileWork*)req->data;
    std::ifstream t(work->path);
    if (!t.is_open()) {
        std::cerr << "read_file_worker failed to open: " << work->path << std::endl;
        work->success = false;
        return;
    }
    std::stringstream buffer;
    buffer << t.rdbuf();
    work->result = buffer.str();
    work->success = true;
}

static void read_file_after_worker(uv_work_t* req, int status) {
    ReadFileWork* work = (ReadFileWork*)req->data;
    if (work->success) {
        void* tsStr = ts_string_create(work->result.c_str());
        TsValue* result = ts_value_make_string(tsStr);
        ts::ts_promise_resolve_internal(work->promise, result);
    } else {
        void* tsStr = ts_string_create("Could not open file");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    work->~ReadFileWork();
    GC_free(work);
    free(req);
}

void* ts_fs_readFile_async(void* path) {
    TsString* pathStr = (TsString*)path;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    ReadFileWork* work = (ReadFileWork*)GC_malloc_uncollectable(sizeof(ReadFileWork));
    new (work) ReadFileWork();
    work->promise = promise;
    work->path = pathStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, read_file_worker, read_file_after_worker);
    
    return ts_value_make_promise(promise);
}

struct WriteFileWork {
    ts::TsPromise* promise;
    std::string path;
    std::string content;
    bool success;
};

static void write_file_worker(uv_work_t* req) {
    WriteFileWork* work = (WriteFileWork*)req->data;
    std::ofstream t(work->path);
    if (!t.is_open()) {
        work->success = false;
        return;
    }
    t << work->content;
    work->success = true;
}

static void write_file_after_worker(uv_work_t* req, int status) {
    WriteFileWork* work = (WriteFileWork*)req->data;
    if (work->success) {
        ts::ts_promise_resolve_internal(work->promise, ts_value_make_undefined());
    } else {
        void* tsStr = ts_string_create("Could not open file for writing");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    work->~WriteFileWork();
    GC_free(work);
    free(req);
}

void* ts_fs_writeFile_async(void* path, void* content) {
    TsString* pathStr = (TsString*)path;
    TsString* contentStr = (TsString*)content;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    WriteFileWork* work = (WriteFileWork*)GC_malloc_uncollectable(sizeof(WriteFileWork));
    new (work) WriteFileWork();
    work->promise = promise;
    work->path = pathStr->ToUtf8();
    work->content = contentStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, write_file_worker, write_file_after_worker);
    
    return ts_value_make_promise(promise);
}

struct MkdirWork {
    ts::TsPromise* promise;
    std::string path;
    bool success;
};

static void mkdir_worker(uv_work_t* req) {
    MkdirWork* work = (MkdirWork*)req->data;
    try {
        fs::create_directories(work->path);
        work->success = true;
    } catch (...) {
        work->success = false;
    }
}

static void mkdir_after_worker(uv_work_t* req, int status) {
    MkdirWork* work = (MkdirWork*)req->data;
    if (work->success) {
        ts::ts_promise_resolve_internal(work->promise, ts_value_make_undefined());
    } else {
        void* tsStr = ts_string_create("Could not create directory");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    work->~MkdirWork();
    GC_free(work);
    free(req);
}

void* ts_fs_mkdir_async(void* path) {
    TsString* pathStr = (TsString*)path;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    MkdirWork* work = (MkdirWork*)GC_malloc_uncollectable(sizeof(MkdirWork));
    new (work) MkdirWork();
    work->promise = promise;
    work->path = pathStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, mkdir_worker, mkdir_after_worker);
    
    return ts_value_make_promise(promise);
}

struct StatWork {
    ts::TsPromise* promise;
    std::string path;
    bool success;
    int64_t size;
    double mtimeMs;
    bool isFile;
    bool isDirectory;
};

static void stat_worker(uv_work_t* req) {
    StatWork* work = (StatWork*)req->data;
    try {
        auto status = fs::status(work->path);
        auto entry = fs::directory_entry(work->path);
        work->size = (int64_t)entry.file_size();
        auto mtime = fs::last_write_time(work->path);
        work->mtimeMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(mtime.time_since_epoch()).count();
        work->isFile = fs::is_regular_file(status);
        work->isDirectory = fs::is_directory(status);
        work->success = true;
    } catch (const std::exception& e) {
        std::cerr << "stat_worker failed for " << work->path << ": " << e.what() << std::endl;
        work->success = false;
    } catch (...) {
        std::cerr << "stat_worker failed for " << work->path << " with unknown error" << std::endl;
        work->success = false;
    }
}

static void stat_after_worker(uv_work_t* req, int status) {
    StatWork* work = (StatWork*)req->data;
    if (work->success) {
        TsMap* stats = TsMap::Create();
        stats->Set(TsString::Create("size"), TsValue(work->size));
        stats->Set(TsString::Create("mtimeMs"), TsValue(work->mtimeMs));
        // stats->Set(TsString::Create("isFile"), TsValue(work->isFile));
        // stats->Set(TsString::Create("isDirectory"), TsValue(work->isDirectory));
        
        add_stats_methods(stats, work->isFile, work->isDirectory);
        
        ts::ts_promise_resolve_internal(work->promise, ts_value_make_object(stats));
    } else {
        void* tsStr = ts_string_create("Could not stat path");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    work->~StatWork();
    GC_free(work);
    free(req);
}

void* ts_fs_stat_async(void* path) {
    TsString* pathStr = (TsString*)path;
    std::cerr << "ts_fs_stat_async: '" << pathStr->ToUtf8() << "'" << std::endl;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    StatWork* work = (StatWork*)GC_malloc_uncollectable(sizeof(StatWork));
    new (work) StatWork();
    work->promise = promise;
    work->path = pathStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, stat_worker, stat_after_worker);
    
    return ts_value_make_promise(promise);
}

} // extern "C"

