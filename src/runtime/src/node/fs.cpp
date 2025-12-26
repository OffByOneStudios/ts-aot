#include "IO.h"
#include "TsRuntime.h"
#include "TsString.h"
#include "TsPromise.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsBuffer.h"
#include "TsReadStream.h"
#include "TsWriteStream.h"
#include "TsDate.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <uv.h>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#define GC_THREADS
#include <gc/gc.h>

namespace fs = std::filesystem;

static TsValue* bool_return_helper(void* context) {
    bool val = (bool)(uintptr_t)context;
    return ts_value_make_bool(val);
}

static void add_stats_methods(TsMap* stats, bool isFile, bool isDirectory, const fs::path& p) {
    stats->Set(TsString::Create("isFile"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isFile));
    stats->Set(TsString::Create("isDirectory"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isDirectory));
    
    // Add more fields
    try {
        auto status = fs::status(p);
        uint64_t size = 0;
        if (isFile) size = fs::file_size(p);
        stats->Set(TsString::Create("size"), TsValue((int64_t)size));
        
        auto mtime = fs::last_write_time(p);
        auto mtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mtime.time_since_epoch()).count();
        stats->Set(TsString::Create("mtimeMs"), TsValue((double)mtime_ms));
        stats->Set(TsString::Create("mtime"), *ts_value_make_object(TsDate::Create(mtime_ms)));

        // On Windows, some of these might be limited or require different APIs
        // For now, we'll use std::filesystem where possible
#ifndef _WIN32
        struct stat st;
        if (stat(p.string().c_str(), &st) == 0) {
            stats->Set(TsString::Create("uid"), TsValue((int64_t)st.st_uid));
            stats->Set(TsString::Create("gid"), TsValue((int64_t)st.st_gid));
            stats->Set(TsString::Create("ino"), TsValue((int64_t)st.st_ino));
            stats->Set(TsString::Create("mode"), TsValue((int64_t)st.st_mode));
            stats->Set(TsString::Create("nlink"), TsValue((int64_t)st.st_nlink));
            stats->Set(TsString::Create("dev"), TsValue((int64_t)st.st_dev));
            stats->Set(TsString::Create("rdev"), TsValue((int64_t)st.st_rdev));
            stats->Set(TsString::Create("blksize"), TsValue((int64_t)st.st_blksize));
            stats->Set(TsString::Create("blocks"), TsValue((int64_t)st.st_blocks));
        }
#else
        // Minimal Windows support for now
        stats->Set(TsString::Create("uid"), TsValue((int64_t)0));
        stats->Set(TsString::Create("gid"), TsValue((int64_t)0));
#endif
    } catch (const std::exception& e) {
    } catch (...) {
    }
}

extern "C" {

void* ts_fs_readFileSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();

    std::ifstream t(pathCStr, std::ios::binary);
    if (!t.is_open()) {
        return TsString::Create("");
    }

    std::stringstream buffer;
    buffer << t.rdbuf();
    std::string s = buffer.str();
    
    return TsString::Create(s.c_str());
}

void ts_fs_unlinkSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    std::filesystem::remove(pathCStr);
}

void ts_fs_mkdirSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    std::filesystem::create_directories(pathCStr);
}

void ts_fs_rmdirSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    std::filesystem::remove_all(pathCStr);
}

void ts_fs_writeFileSync(void* path, void* content) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();

    TsString* contentStr = nullptr;
    TsValue* val = (TsValue*)content;
    if (val->type == ValueType::STRING_PTR) {
        contentStr = (TsString*)val->ptr_val;
    } else {
        contentStr = (TsString*)ts_string_from_value(val);
    }

    const char* contentCStr = contentStr->ToUtf8();

    std::ofstream t(pathCStr);
    if (!t.is_open()) {
        return;
    }

    t << contentCStr;
    t.close();
}

bool ts_fs_existsSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    return fs::exists(pathCStr);
}

void* ts_fs_statSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    
    try {
        fs::path p(pathCStr);
        auto status = fs::status(p);
        
        TsMap* stats = TsMap::Create();
        add_stats_methods(stats, fs::is_regular_file(status), fs::is_directory(status), p);
        
        return ts_value_make_object(stats);
    } catch (const std::exception& e) {
        return ts_value_make_undefined();
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

void ts_fs_accessSync(void* path, int32_t mode) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_access(NULL, &req, pathCStr, mode, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_chmodSync(void* path, int32_t mode) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_chmod(NULL, &req, pathCStr, mode, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_chownSync(void* path, int32_t uid, int32_t gid) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_chown(NULL, &req, pathCStr, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_utimesSync(void* path, double atime, double mtime) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_utime(NULL, &req, pathCStr, atime, mtime, NULL);
    uv_fs_req_cleanup(&req);
}

void* ts_fs_statfsSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    int r = uv_fs_statfs(NULL, &req, pathCStr, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return ts_value_make_undefined();
    }
    
    uv_statfs_t* s = (uv_statfs_t*)req.ptr;
    TsMap* result = TsMap::Create();
    result->Set(TsString::Create("type"), TsValue((int64_t)s->f_type));
    result->Set(TsString::Create("bsize"), TsValue((int64_t)s->f_bsize));
    result->Set(TsString::Create("blocks"), TsValue((int64_t)s->f_blocks));
    result->Set(TsString::Create("bfree"), TsValue((int64_t)s->f_bfree));
    result->Set(TsString::Create("bavail"), TsValue((int64_t)s->f_bavail));
    result->Set(TsString::Create("files"), TsValue((int64_t)s->f_files));
    result->Set(TsString::Create("ffree"), TsValue((int64_t)s->f_ffree));
    
    uv_fs_req_cleanup(&req);
    return ts_value_make_object(result);
}

void* ts_fs_get_constants() {
    TsMap* constants = TsMap::Create();
    constants->Set(TsString::Create("F_OK"), TsValue((int64_t)0));
    constants->Set(TsString::Create("R_OK"), TsValue((int64_t)4));
    constants->Set(TsString::Create("W_OK"), TsValue((int64_t)2));
    constants->Set(TsString::Create("X_OK"), TsValue((int64_t)1));
    return ts_value_make_object(constants);
}

static TsValue* readFile_promise_wrapper(void* context, TsValue* path) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_readFile_async(path->ptr_val);
}

static TsValue* writeFile_promise_wrapper(void* context, TsValue* path, TsValue* content) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!content || content->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_writeFile_async(path->ptr_val, content->ptr_val);
}

struct FSAsyncWork {
    ts::TsPromise* promise;
    std::string path;
    int mode;
    int uid;
    int gid;
    double atime;
    double mtime;
    int result;
    bool success;
    enum Type { ACCESS, CHMOD, CHOWN, UTIMES, STATFS } type;
    uv_statfs_t statfs_res;
};

static void fs_async_worker(uv_work_t* req) {
    FSAsyncWork* work = (FSAsyncWork*)req->data;
    uv_fs_t fs_req;
    int r = 0;
    switch (work->type) {
        case FSAsyncWork::ACCESS:
            r = uv_fs_access(NULL, &fs_req, work->path.c_str(), work->mode, NULL);
            break;
        case FSAsyncWork::CHMOD:
            r = uv_fs_chmod(NULL, &fs_req, work->path.c_str(), work->mode, NULL);
            break;
        case FSAsyncWork::CHOWN:
            r = uv_fs_chown(NULL, &fs_req, work->path.c_str(), work->uid, work->gid, NULL);
            break;
        case FSAsyncWork::UTIMES:
            r = uv_fs_utime(NULL, &fs_req, work->path.c_str(), work->atime, work->mtime, NULL);
            break;
        case FSAsyncWork::STATFS:
            r = uv_fs_statfs(NULL, &fs_req, work->path.c_str(), NULL);
            if (r >= 0) {
                work->statfs_res = *(uv_statfs_t*)fs_req.ptr;
            }
            break;
    }
    work->result = r;
    work->success = (r >= 0);
    uv_fs_req_cleanup(&fs_req);
}

static void fs_async_after_worker(uv_work_t* req, int status) {
    FSAsyncWork* work = (FSAsyncWork*)req->data;
    if (work->success) {
        if (work->type == FSAsyncWork::STATFS) {
            TsMap* res = TsMap::Create();
            res->Set(TsString::Create("type"), TsValue((int64_t)work->statfs_res.f_type));
            res->Set(TsString::Create("bsize"), TsValue((int64_t)work->statfs_res.f_bsize));
            res->Set(TsString::Create("blocks"), TsValue((int64_t)work->statfs_res.f_blocks));
            res->Set(TsString::Create("bfree"), TsValue((int64_t)work->statfs_res.f_bfree));
            res->Set(TsString::Create("bavail"), TsValue((int64_t)work->statfs_res.f_bavail));
            res->Set(TsString::Create("files"), TsValue((int64_t)work->statfs_res.f_files));
            res->Set(TsString::Create("ffree"), TsValue((int64_t)work->statfs_res.f_ffree));
            ts::ts_promise_resolve_internal(work->promise, ts_value_make_object(res));
        } else {
            ts::ts_promise_resolve_internal(work->promise, ts_value_make_undefined());
        }
    } else {
        ts::ts_promise_reject_internal(work->promise, ts_value_make_string(ts_string_create("FS operation failed")));
    }
    work->~FSAsyncWork();
    GC_free(work);
    free(req);
}

void* ts_fs_access_async(void* path, double mode) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->path = ((TsString*)path)->ToUtf8();
    work->mode = (int)mode;
    work->type = FSAsyncWork::ACCESS;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_chmod_async(void* path, double mode) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->path = ((TsString*)path)->ToUtf8();
    work->mode = (int)mode;
    work->type = FSAsyncWork::CHMOD;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_chown_async(void* path, double uid, double gid) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->path = ((TsString*)path)->ToUtf8();
    work->uid = (int)uid;
    work->gid = (int)gid;
    work->type = FSAsyncWork::CHOWN;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_utimes_async(void* path, double atime, double mtime) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->path = ((TsString*)path)->ToUtf8();
    work->atime = atime;
    work->mtime = mtime;
    work->type = FSAsyncWork::UTIMES;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_statfs_async(void* path) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->path = ((TsString*)path)->ToUtf8();
    work->type = FSAsyncWork::STATFS;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

static TsValue* access_promise_wrapper(void* context, TsValue* path, TsValue* mode) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    double m = 0;
    if (mode && (mode->type == ValueType::NUMBER_INT || mode->type == ValueType::NUMBER_DBL)) {
        m = (mode->type == ValueType::NUMBER_INT) ? (double)mode->i_val : mode->d_val;
    }
    return (TsValue*)ts_fs_access_async(path->ptr_val, m);
}

static TsValue* chmod_promise_wrapper(void* context, TsValue* path, TsValue* mode) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!mode || (mode->type != ValueType::NUMBER_INT && mode->type != ValueType::NUMBER_DBL)) return ts_value_make_undefined();
    double m = (mode->type == ValueType::NUMBER_INT) ? (double)mode->i_val : mode->d_val;
    return (TsValue*)ts_fs_chmod_async(path->ptr_val, m);
}

static TsValue* chown_promise_wrapper(void* context, TsValue* path, TsValue* uid, TsValue* gid) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!uid || (uid->type != ValueType::NUMBER_INT && uid->type != ValueType::NUMBER_DBL)) return ts_value_make_undefined();
    if (!gid || (gid->type != ValueType::NUMBER_INT && gid->type != ValueType::NUMBER_DBL)) return ts_value_make_undefined();
    double u = (uid->type == ValueType::NUMBER_INT) ? (double)uid->i_val : uid->d_val;
    double g = (gid->type == ValueType::NUMBER_INT) ? (double)gid->i_val : gid->d_val;
    return (TsValue*)ts_fs_chown_async(path->ptr_val, u, g);
}

static TsValue* utimes_promise_wrapper(void* context, TsValue* path, TsValue* atime, TsValue* mtime) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!atime || (atime->type != ValueType::NUMBER_INT && atime->type != ValueType::NUMBER_DBL)) return ts_value_make_undefined();
    if (!mtime || (mtime->type != ValueType::NUMBER_INT && mtime->type != ValueType::NUMBER_DBL)) return ts_value_make_undefined();
    double a = (atime->type == ValueType::NUMBER_INT) ? (double)atime->i_val : atime->d_val;
    double m = (mtime->type == ValueType::NUMBER_INT) ? (double)mtime->i_val : mtime->d_val;
    return (TsValue*)ts_fs_utimes_async(path->ptr_val, a, m);
}

static TsValue* statfs_promise_wrapper(void* context, TsValue* path) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_statfs_async(path->ptr_val);
}

void* ts_fs_get_promises() {
    TsMap* promises = TsMap::Create();
    promises->Set(TsString::Create("readFile"), *ts_value_make_function((void*)readFile_promise_wrapper, nullptr));
    promises->Set(TsString::Create("writeFile"), *ts_value_make_function((void*)writeFile_promise_wrapper, nullptr));
    promises->Set(TsString::Create("access"), *ts_value_make_function((void*)access_promise_wrapper, nullptr));
    promises->Set(TsString::Create("chmod"), *ts_value_make_function((void*)chmod_promise_wrapper, nullptr));
    promises->Set(TsString::Create("chown"), *ts_value_make_function((void*)chown_promise_wrapper, nullptr));
    promises->Set(TsString::Create("utimes"), *ts_value_make_function((void*)utimes_promise_wrapper, nullptr));
    promises->Set(TsString::Create("statfs"), *ts_value_make_function((void*)statfs_promise_wrapper, nullptr));
    return ts_value_make_object(promises);
}

int64_t ts_fs_openSync(void* path, void* flags) {
    TsString* pathStr = (TsString*)path;
    TsString* flagsStr = (TsString*)flags;
    const char* pathCStr = pathStr->ToUtf8();
    const char* flagsCStr = flagsStr->ToUtf8();

    int openFlags = 0;
    std::string f = flagsCStr;
    if (f == "r") openFlags = O_RDONLY;
    else if (f == "r+") openFlags = O_RDWR;
    else if (f == "w") openFlags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (f == "w+") openFlags = O_RDWR | O_CREAT | O_TRUNC;
    else if (f == "a") openFlags = O_WRONLY | O_CREAT | O_APPEND;
    else if (f == "a+") openFlags = O_RDWR | O_CREAT | O_APPEND;
    else openFlags = O_RDONLY;

#ifdef _WIN32
    openFlags |= _O_BINARY;
#endif

    uv_fs_t req;
    uv_fs_open(NULL, &req, pathCStr, openFlags, 0666, NULL);
    int result = (int)req.result;
    uv_fs_req_cleanup(&req);
    return (int64_t)result;
}

void ts_fs_closeSync(int64_t fd) {
    uv_fs_t req;
    uv_fs_close(NULL, &req, (uv_file)fd, NULL);
    uv_fs_req_cleanup(&req);
}

int64_t ts_fs_readSync(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position) {
    TsBuffer* buf = (TsBuffer*)buffer;
    uint8_t* data = buf->GetData() + offset;
    
    uv_fs_t req;
    uv_buf_t uv_buf = uv_buf_init((char*)data, (unsigned int)length);
    uv_fs_read(NULL, &req, (uv_file)fd, &uv_buf, 1, position, NULL);
    int result = (int)req.result;
    uv_fs_req_cleanup(&req);
    return (int64_t)result;
}

int64_t ts_fs_writeSync(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position) {
    TsBuffer* buf = (TsBuffer*)buffer;
    uint8_t* data = buf->GetData() + offset;
    
    uv_fs_t req;
    uv_buf_t uv_buf = uv_buf_init((char*)data, (unsigned int)length);
    uv_fs_write(NULL, &req, (uv_file)fd, &uv_buf, 1, position, NULL);
    int result = (int)req.result;
    uv_fs_req_cleanup(&req);
    return (int64_t)result;
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
        
        add_stats_methods(stats, work->isFile, work->isDirectory, work->path);
        
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

struct OpenWork {
    ts::TsPromise* promise;
    std::string path;
    std::string flags;
    int fd;
    bool success;
};

static void open_worker(uv_work_t* req) {
    OpenWork* work = (OpenWork*)req->data;
    int openFlags = 0;
    if (work->flags == "r") openFlags = O_RDONLY;
    else if (work->flags == "r+") openFlags = O_RDWR;
    else if (work->flags == "w") openFlags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (work->flags == "w+") openFlags = O_RDWR | O_CREAT | O_TRUNC;
    else if (work->flags == "a") openFlags = O_WRONLY | O_CREAT | O_APPEND;
    else if (work->flags == "a+") openFlags = O_RDWR | O_CREAT | O_APPEND;
    else openFlags = O_RDONLY;

#ifdef _WIN32
    openFlags |= _O_BINARY;
#endif

    uv_fs_t fs_req;
    uv_fs_open(NULL, &fs_req, work->path.c_str(), openFlags, 0666, NULL);
    work->fd = (int)fs_req.result;
    work->success = (work->fd >= 0);
    uv_fs_req_cleanup(&fs_req);
}

static void open_after_worker(uv_work_t* req, int status) {
    OpenWork* work = (OpenWork*)req->data;
    if (work->success) {
        ts::ts_promise_resolve_internal(work->promise, ts_value_make_int(work->fd));
    } else {
        void* tsStr = ts_string_create("Could not open file");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    work->~OpenWork();
    GC_free(work);
    free(req);
}

void* ts_fs_open_async(void* path, void* flags) {
    TsString* pathStr = (TsString*)path;
    TsString* flagsStr = (TsString*)flags;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    OpenWork* work = (OpenWork*)GC_malloc_uncollectable(sizeof(OpenWork));
    new (work) OpenWork();
    work->promise = promise;
    work->path = pathStr->ToUtf8();
    work->flags = flagsStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, open_worker, open_after_worker);
    
    return ts_value_make_promise(promise);
}

struct CloseWork {
    ts::TsPromise* promise;
    int fd;
    bool success;
};

struct ReadWriteWork {
    ts::TsPromise* promise;
    int fd;
    TsBuffer* buffer;
    int64_t offset;
    int64_t length;
    int64_t position;
    int64_t result;
    bool isWrite;
};

static void read_write_worker(uv_work_t* req) {
    ReadWriteWork* work = (ReadWriteWork*)req->data;
    uint8_t* data = work->buffer->GetData() + work->offset;
    
    uv_fs_t fs_req;
    uv_buf_t uv_buf = uv_buf_init((char*)data, (unsigned int)work->length);
    
    if (work->isWrite) {
        uv_fs_write(NULL, &fs_req, (uv_file)work->fd, &uv_buf, 1, work->position, NULL);
    } else {
        uv_fs_read(NULL, &fs_req, (uv_file)work->fd, &uv_buf, 1, work->position, NULL);
    }
    
    work->result = (int64_t)fs_req.result;
    uv_fs_req_cleanup(&fs_req);
}

static void read_write_after_worker(uv_work_t* req, int status) {
    ReadWriteWork* work = (ReadWriteWork*)req->data;
    if (work->result >= 0) {
        ts::ts_promise_resolve_internal(work->promise, ts_value_make_int(work->result));
    } else {
        void* tsStr = ts_string_create("IO error");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    GC_free(work);
    free(req);
}

void* ts_fs_read_async(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position) {
    ts::TsPromise* promise = ts::ts_promise_create();
    ReadWriteWork* work = (ReadWriteWork*)GC_malloc_uncollectable(sizeof(ReadWriteWork));
    work->promise = promise;
    work->fd = (int)fd;
    work->buffer = (TsBuffer*)buffer;
    work->offset = offset;
    work->length = length;
    work->position = position;
    work->isWrite = false;
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, read_write_worker, read_write_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_write_async(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position) {
    ts::TsPromise* promise = ts::ts_promise_create();
    ReadWriteWork* work = (ReadWriteWork*)GC_malloc_uncollectable(sizeof(ReadWriteWork));
    work->promise = promise;
    work->fd = (int)fd;
    work->buffer = (TsBuffer*)buffer;
    work->offset = offset;
    work->length = length;
    work->position = position;
    work->isWrite = true;
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, read_write_worker, read_write_after_worker);
    return ts_value_make_promise(promise);
}

static void close_worker(uv_work_t* req) {
    CloseWork* work = (CloseWork*)req->data;
    uv_fs_t fs_req;
    uv_fs_close(NULL, &fs_req, (uv_file)work->fd, NULL);
    work->success = (fs_req.result >= 0);
    uv_fs_req_cleanup(&fs_req);
}

static void close_after_worker(uv_work_t* req, int status) {
    CloseWork* work = (CloseWork*)req->data;
    if (work->success) {
        ts::ts_promise_resolve_internal(work->promise, ts_value_make_undefined());
    } else {
        void* tsStr = ts_string_create("Could not close file");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    work->~CloseWork();
    GC_free(work);
    free(req);
}

void* ts_fs_close_async(int64_t fd) {
    ts::TsPromise* promise = ts::ts_promise_create();
    
    CloseWork* work = (CloseWork*)GC_malloc_uncollectable(sizeof(CloseWork));
    new (work) CloseWork();
    work->promise = promise;
    work->fd = (int)fd;
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, close_worker, close_after_worker);
    
    return ts_value_make_promise(promise);
}

} // extern "C"
