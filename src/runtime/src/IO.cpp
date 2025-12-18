#include "IO.h"
#include "TsString.h"
#include "TsPromise.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <uv.h>

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
    delete work;
    free(req);
}

void* ts_fs_readFile_async(void* path) {
    TsString* pathStr = (TsString*)path;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    ReadFileWork* work = new ReadFileWork();
    work->promise = promise;
    work->path = pathStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, read_file_worker, read_file_after_worker);
    
    return promise;
}

struct FetchWork {
    ts::TsPromise* promise;
    std::string url;
    std::string result;
    bool success;
};

static void fetch_worker(uv_work_t* req) {
    FetchWork* work = (FetchWork*)req->data;
    // Simple mock fetch for now
    work->result = "Mock response for " + work->url;
    work->success = true;
}

static void fetch_after_worker(uv_work_t* req, int status) {
    FetchWork* work = (FetchWork*)req->data;
    if (work->success) {
        void* tsStr = ts_string_create(work->result.c_str());
        TsValue* result = ts_value_make_string(tsStr);
        ts::ts_promise_resolve_internal(work->promise, result);
    } else {
        void* tsStr = ts_string_create("Fetch failed");
        TsValue* reason = ts_value_make_string(tsStr);
        ts::ts_promise_reject_internal(work->promise, reason);
    }
    delete work;
    free(req);
}

void* ts_fetch(void* url) {
    TsString* urlStr = (TsString*)url;
    ts::TsPromise* promise = ts::ts_promise_create();
    
    FetchWork* work = new FetchWork();
    work->promise = promise;
    work->url = urlStr->ToUtf8();
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    
    uv_queue_work(uv_default_loop(), req, fetch_worker, fetch_after_worker);
    
    return promise;
}

int64_t ts_parseInt(void* str) {
    TsString* s = (TsString*)str;
    const char* cStr = s->ToUtf8();
    return std::strtoll(cStr, nullptr, 10);
}

}
