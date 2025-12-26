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

extern void* TsMap_VTable[2];
extern "C" TsValue* ts_map_get_property(void* obj, void* propName);

#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif

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
    printf("DEBUG: bool_return_helper val=%d\n", val);
    return ts_value_make_bool(val);
}

class TsDirent : public TsMap {
public:
    static TsDirent* Create(const char* name, int type) {
        printf("DEBUG: TsDirent::Create name='%s' type=%d\n", name, type);
        TsDirent* d = (TsDirent*)GC_malloc(sizeof(TsDirent));
        new (d) TsDirent();

        if (!TsMap_VTable[1]) {
            TsMap_VTable[1] = (void*)ts_map_get_property;
        }
        d->vtable = TsMap_VTable;

        d->Set(TsString::Create("name"), *ts_value_make_string(TsString::Create(name)));
        
        bool isFile = (type == UV_DIRENT_FILE);
        bool isDirectory = (type == UV_DIRENT_DIR);
        bool isSymbolicLink = (type == UV_DIRENT_LINK);
        bool isBlockDevice = (type == UV_DIRENT_BLOCK);
        bool isCharacterDevice = (type == UV_DIRENT_CHAR);
        bool isFIFO = (type == UV_DIRENT_FIFO);
        bool isSocket = (type == UV_DIRENT_SOCKET);

        d->Set(TsString::Create("isFile"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isFile));
        d->Set(TsString::Create("isDirectory"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isDirectory));
        d->Set(TsString::Create("isSymbolicLink"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isSymbolicLink));
        d->Set(TsString::Create("isBlockDevice"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isBlockDevice));
        d->Set(TsString::Create("isCharacterDevice"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isCharacterDevice));
        d->Set(TsString::Create("isFIFO"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isFIFO));
        d->Set(TsString::Create("isSocket"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isSocket));

        return d;
    }
};

class TsDir : public TsMap {
public:
    uv_dir_t* dir;
    std::string path;

    static TsDir* Create(uv_dir_t* dir, const char* path) {
        TsDir* d = (TsDir*)GC_malloc(sizeof(TsDir));
        new (d) TsDir();
        d->dir = dir;
        d->path = path;
        
        if (!TsMap_VTable[1]) {
            TsMap_VTable[1] = (void*)ts_map_get_property;
        }
        d->vtable = TsMap_VTable;

        d->Set(TsString::Create("path"), *ts_value_make_string(TsString::Create(path)));
        return d;
    }
};

static void add_stats_methods(TsMap* stats, const uv_stat_t* st) {
    bool isFile = (st->st_mode & S_IFMT) == S_IFREG;
    bool isDirectory = (st->st_mode & S_IFMT) == S_IFDIR;
    bool isSymbolicLink = (st->st_mode & S_IFMT) == S_IFLNK;

    stats->Set(TsString::Create("isFile"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isFile));
    stats->Set(TsString::Create("isDirectory"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isDirectory));
    stats->Set(TsString::Create("isSymbolicLink"), *ts_value_make_function((void*)bool_return_helper, (void*)(uintptr_t)isSymbolicLink));
    
    stats->Set(TsString::Create("size"), TsValue((int64_t)st->st_size));
    
    double mtime_ms = (double)st->st_mtim.tv_sec * 1000.0 + (double)st->st_mtim.tv_nsec / 1000000.0;
    stats->Set(TsString::Create("mtimeMs"), TsValue(mtime_ms));
    stats->Set(TsString::Create("mtime"), *ts_value_make_object(TsDate::Create(mtime_ms)));

    stats->Set(TsString::Create("atimeMs"), TsValue((double)st->st_atim.tv_sec * 1000.0 + (double)st->st_atim.tv_nsec / 1000000.0));
    stats->Set(TsString::Create("ctimeMs"), TsValue((double)st->st_ctim.tv_sec * 1000.0 + (double)st->st_ctim.tv_nsec / 1000000.0));
    stats->Set(TsString::Create("birthtimeMs"), TsValue((double)st->st_birthtim.tv_sec * 1000.0 + (double)st->st_birthtim.tv_nsec / 1000000.0));

    stats->Set(TsString::Create("uid"), TsValue((int64_t)st->st_uid));
    stats->Set(TsString::Create("gid"), TsValue((int64_t)st->st_gid));
    stats->Set(TsString::Create("ino"), TsValue((int64_t)st->st_ino));
    stats->Set(TsString::Create("mode"), TsValue((int64_t)st->st_mode));
    stats->Set(TsString::Create("nlink"), TsValue((int64_t)st->st_nlink));
    stats->Set(TsString::Create("dev"), TsValue((int64_t)st->st_dev));
    stats->Set(TsString::Create("rdev"), TsValue((int64_t)st->st_rdev));
    stats->Set(TsString::Create("blksize"), TsValue((int64_t)st->st_blksize));
    stats->Set(TsString::Create("blocks"), TsValue((int64_t)st->st_blocks));
}

extern "C" {

static TsString* unboxString(void* ptr) {
    if (!ptr) return nullptr;
    
    // Check if it's a raw TsString* FIRST
    TsString* str = (TsString*)ptr;
    if (str->magic == 0x53545247) { // TsString::MAGIC
        fprintf(stderr, "DEBUG: unboxString %p is raw TsString: '%s'\n", ptr, str->ToUtf8());
        return str;
    }

    TsValue* val = (TsValue*)ptr;
    // Check if it's a TsValue* containing a string
    if (val->type == ValueType::STRING_PTR) {
        TsString* s = (TsString*)val->ptr_val;
        fprintf(stderr, "DEBUG: unboxString %p is TsValue(STRING_PTR): '%s'\n", ptr, s->ToUtf8());
        return s;
    }
    
    fprintf(stderr, "DEBUG: unboxString %p is unknown type %d, trying conversion\n", ptr, (int)val->type);
    // Fallback: try to convert to string
    TsString* res = (TsString*)ts_string_from_value(val);
    fprintf(stderr, "DEBUG: unboxString %p converted to: '%s'\n", ptr, res->ToUtf8());
    return res;
}

void* ts_fs_readFileSync(void* path) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return TsString::Create("");
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
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_unlink(NULL, &req, pathCStr, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_mkdirSync(void* path, void* options) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    // Default mode 0777
    uv_fs_mkdir(NULL, &req, pathCStr, 0777, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_rmdirSync(void* path, void* options) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_rmdir(NULL, &req, pathCStr, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_rmSync(void* path, void* options) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    // For now, just use rmdir or unlink depending on what it is
    // In a real implementation, we'd handle recursive: true
    uv_fs_t req;
    uv_fs_stat(NULL, &req, pathCStr, NULL);
    if (req.result == 0) {
        uv_fs_t rm_req;
        if (S_ISDIR(req.statbuf.st_mode)) {
            uv_fs_rmdir(NULL, &rm_req, pathCStr, NULL);
        } else {
            uv_fs_unlink(NULL, &rm_req, pathCStr, NULL);
        }
        uv_fs_req_cleanup(&rm_req);
    }
    uv_fs_req_cleanup(&req);
}

void ts_fs_renameSync(void* oldPath, void* newPath) {
    TsString* oldStr = unboxString(oldPath);
    TsString* newStr = unboxString(newPath);
    if (!oldStr || !newStr) return;
    uv_fs_t req;
    uv_fs_rename(NULL, &req, oldStr->ToUtf8(), newStr->ToUtf8(), NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_copyFileSync(void* src, void* dest, int32_t flags) {
    TsString* srcStr = unboxString(src);
    TsString* destStr = unboxString(dest);
    if (!srcStr || !destStr) return;
    uv_fs_t req;
    uv_fs_copyfile(NULL, &req, srcStr->ToUtf8(), destStr->ToUtf8(), flags, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_truncateSync(void* path, int64_t len) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t open_req;
    int fd = uv_fs_open(NULL, &open_req, pathCStr, O_RDWR, 0, NULL);
    if (open_req.result >= 0) {
        uv_fs_t trunc_req;
        uv_fs_ftruncate(NULL, &trunc_req, (uv_file)open_req.result, len, NULL);
        uv_fs_req_cleanup(&trunc_req);
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, (uv_file)open_req.result, NULL);
        uv_fs_req_cleanup(&close_req);
    }
    uv_fs_req_cleanup(&open_req);
}

void ts_fs_appendFileSync(void* path, void* content) {
    TsString* pathStr = unboxString(path);
    TsString* contentStr = unboxString(content);
    if (!pathStr || !contentStr) return;
    const char* pathCStr = pathStr->ToUtf8();

    uv_fs_t open_req;
    int fd = uv_fs_open(NULL, &open_req, pathCStr, O_WRONLY | O_CREAT | O_APPEND, 0666, NULL);
    if (fd >= 0) {
        uv_fs_t write_req;
        uv_buf_t buf = uv_buf_init((char*)contentStr->ToUtf8(), contentStr->Length());
        uv_fs_write(NULL, &write_req, fd, &buf, 1, -1, NULL);
        uv_fs_req_cleanup(&write_req);
        
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
    }
    uv_fs_req_cleanup(&open_req);
}

void* ts_fs_mkdtempSync(void* prefix) {
    TsString* prefixStr = unboxString(prefix);
    if (!prefixStr) return TsString::Create("");
    std::string template_str = std::string(prefixStr->ToUtf8()) + "XXXXXX";
    uv_fs_t req;
    int r = uv_fs_mkdtemp(NULL, &req, template_str.c_str(), NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return TsString::Create("");
    }
    TsString* res = TsString::Create(req.path);
    uv_fs_req_cleanup(&req);
    return res;
}

void ts_fs_writeFileSync(void* path, void* content) {
    TsString* pathStr = unboxString(path);
    TsString* contentStr = unboxString(content);
    if (!pathStr || !contentStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    const char* contentCStr = contentStr->ToUtf8();

    std::ofstream t(pathCStr);
    if (!t.is_open()) {
        return;
    }

    t << contentCStr;
    t.close();
}

bool ts_fs_existsSync(void* path) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return false;
    const char* pathCStr = pathStr->ToUtf8();
    return fs::exists(pathCStr);
}

void* ts_fs_statSync(void* path) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return ts_value_make_undefined();
    const char* pathCStr = pathStr->ToUtf8();
    
    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, pathCStr, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return ts_value_make_undefined();
    }
    
    TsMap* stats = TsMap::Create();
    add_stats_methods(stats, &req.statbuf);
    
    uv_fs_req_cleanup(&req);
    return ts_value_make_object(stats);
}

void* ts_fs_lstatSync(void* path) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return ts_value_make_undefined();
    const char* pathCStr = pathStr->ToUtf8();
    
    uv_fs_t req;
    int r = uv_fs_lstat(NULL, &req, pathCStr, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return ts_value_make_undefined();
    }
    
    TsMap* stats = TsMap::Create();
    add_stats_methods(stats, &req.statbuf);
    
    uv_fs_req_cleanup(&req);
    return ts_value_make_object(stats);
}

void ts_fs_linkSync(void* existingPath, void* newPath) {
    TsString* existingStr = unboxString(existingPath);
    TsString* newStr = unboxString(newPath);
    if (!existingStr || !newStr) return;
    uv_fs_t req;
    uv_fs_link(NULL, &req, existingStr->ToUtf8(), newStr->ToUtf8(), NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_symlinkSync(void* target, void* path, void* type) {
    TsString* targetStr = unboxString(target);
    TsString* pathStr = unboxString(path);
    if (!targetStr || !pathStr) return;
    int flags = 0;
    if (type && !ts_value_is_nullish((TsValue*)type)) {
        TsString* typeStr = unboxString(type);
        if (typeStr) {
            const char* typeCStr = typeStr->ToUtf8();
            if (strcmp(typeCStr, "dir") == 0) flags |= UV_FS_SYMLINK_DIR;
            else if (strcmp(typeCStr, "junction") == 0) flags |= UV_FS_SYMLINK_JUNCTION;
        }
    }
    uv_fs_t req;
    int r = uv_fs_symlink(NULL, &req, targetStr->ToUtf8(), pathStr->ToUtf8(), flags, NULL);
    if (r < 0) {
        printf("DEBUG: symlinkSync failed: %d (%s)\n", r, uv_strerror(r));
    }
    uv_fs_req_cleanup(&req);
}

void* ts_fs_readlinkSync(void* path) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return nullptr;
    uv_fs_t req;
    int r = uv_fs_readlink(NULL, &req, pathStr->ToUtf8(), NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return nullptr;
    }
    TsString* res = TsString::Create(req.ptr ? (const char*)req.ptr : "");
    uv_fs_req_cleanup(&req);
    return res;
}

void* ts_fs_realpathSync(void* path) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return nullptr;
    uv_fs_t req;
    int r = uv_fs_realpath(NULL, &req, pathStr->ToUtf8(), NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return nullptr;
    }
    TsString* res = TsString::Create((const char*)req.ptr);
    uv_fs_req_cleanup(&req);
    return res;
}

void* ts_fs_readdirSync(void* path, void* options) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return ts_value_make_array(TsArray::Create());
    const char* pathCStr = pathStr->ToUtf8();
    
    bool withFileTypes = false;
    if (options) {
        TsValue* optVal = (TsValue*)options;
        if (optVal->type == ValueType::OBJECT_PTR) {
            TsMap* optMap = (TsMap*)optVal->ptr_val;
            TsValue wft = optMap->Get(TsString::Create("withFileTypes"));
            if (wft.type == ValueType::BOOLEAN) {
                withFileTypes = wft.b_val;
            }
        }
    }

    TsArray* result = TsArray::Create();
    try {
        for (const auto& entry : fs::directory_iterator(pathCStr)) {
            std::string name = entry.path().filename().string();
            if (withFileTypes) {
                uv_dirent_type_t type = UV_DIRENT_UNKNOWN;
                if (entry.is_regular_file()) type = UV_DIRENT_FILE;
                else if (entry.is_directory()) type = UV_DIRENT_DIR;
                else if (entry.is_symlink()) type = UV_DIRENT_LINK;
                else if (entry.is_block_file()) type = UV_DIRENT_BLOCK;
                else if (entry.is_character_file()) type = UV_DIRENT_CHAR;
                else if (entry.is_fifo()) type = UV_DIRENT_FIFO;
                else if (entry.is_socket()) type = UV_DIRENT_SOCKET;
                
                result->Push((int64_t)ts_value_make_object(TsDirent::Create(name.c_str(), type)));
            } else {
                result->Push((int64_t)ts_value_make_string(TsString::Create(name.c_str())));
            }
        }
    } catch (...) {}
    return ts_value_make_array(result);
}

static TsValue* dir_read_sync_wrapper(void* context);
static TsValue* dir_close_sync_wrapper(void* context);
static TsValue* dir_read_async_wrapper(void* context);
static TsValue* dir_close_async_wrapper(void* context);

static TsValue* dir_read_sync_wrapper(void* context) {
    TsDir* dir = (TsDir*)context;
    if (!dir || !dir->dir) return ts_value_make_null();
    
    uv_fs_t req;
    uv_dirent_t ent;
    dir->dir->dirents = &ent;
    dir->dir->nentries = 1;
    int r = uv_fs_readdir(NULL, &req, dir->dir, NULL);
    if (r <= 0) {
        uv_fs_req_cleanup(&req);
        return ts_value_make_null();
    }
    TsDirent* res = TsDirent::Create(ent.name, ent.type);
    uv_fs_req_cleanup(&req);
    return ts_value_make_object(res);
}

static TsValue* dir_close_sync_wrapper(void* context) {
    TsDir* dir = (TsDir*)context;
    if (!dir || !dir->dir) return ts_value_make_undefined();
    uv_fs_t req;
    uv_fs_closedir(NULL, &req, dir->dir, NULL);
    uv_fs_req_cleanup(&req);
    return ts_value_make_undefined();
}

void* ts_fs_opendirSync(void* path, void* options) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return ts_value_make_undefined();
    const char* pathCStr = pathStr->ToUtf8();
    
    uv_fs_t req;
    int r = uv_fs_opendir(NULL, &req, pathCStr, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return ts_value_make_undefined();
    }
    
    uv_dir_t* dir = (uv_dir_t*)req.ptr;
    TsDir* res = TsDir::Create(dir, pathCStr);
    res->Set(TsString::Create("readSync"), *ts_value_make_function((void*)dir_read_sync_wrapper, res));
    res->Set(TsString::Create("closeSync"), *ts_value_make_function((void*)dir_close_sync_wrapper, res));
    res->Set(TsString::Create("read"), *ts_value_make_function((void*)dir_read_async_wrapper, res));
    res->Set(TsString::Create("close"), *ts_value_make_function((void*)dir_close_async_wrapper, res));
    
    uv_fs_req_cleanup(&req);
    return ts_value_make_object(res);
}

void ts_fs_accessSync(void* path, int32_t mode) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_access(NULL, &req, pathCStr, mode, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_chmodSync(void* path, int32_t mode) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_chmod(NULL, &req, pathCStr, mode, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_chownSync(void* path, int32_t uid, int32_t gid) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_chown(NULL, &req, pathCStr, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
}

void ts_fs_utimesSync(void* path, double atime, double mtime) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return;
    const char* pathCStr = pathStr->ToUtf8();
    uv_fs_t req;
    uv_fs_utime(NULL, &req, pathCStr, atime, mtime, NULL);
    uv_fs_req_cleanup(&req);
}

void* ts_fs_statfsSync(void* path) {
    TsString* pathStr = unboxString(path);
    if (!pathStr) return ts_value_make_undefined();
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
    std::string path2;
    int mode;
    int uid;
    int gid;
    double atime;
    double mtime;
    int64_t len;
    int result;
    bool success;
    bool withFileTypes = false;
    enum Type { 
        ACCESS, CHMOD, CHOWN, UTIMES, STATFS, LINK, SYMLINK, READLINK, REALPATH, STAT, LSTAT,
        RENAME, COPYFILE, TRUNCATE, MKDIR, RMDIR, RM, MKDTEMP, APPEND_FILE,
        OPENDIR, DIR_READ, DIR_CLOSE, READDIR
    } type;
    uv_statfs_t statfs_res;
    uv_stat_t stat_res;
    std::string string_res;
    uv_dir_t* dir_ptr;
    uv_dirent_t dirent_res;
    struct DirentRes {
        std::string name;
        uv_dirent_type_t type;
    };
    DirentRes dirent_read_res;
    std::vector<std::string> readdir_res;
    std::vector<DirentRes> readdir_dirent_res;
};

static void fs_async_worker(uv_work_t* req) {
    FSAsyncWork* work = (FSAsyncWork*)req->data;
    uv_fs_t fs_req;
    memset(&fs_req, 0, sizeof(fs_req));
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
        case FSAsyncWork::LINK:
            r = uv_fs_link(NULL, &fs_req, work->path.c_str(), work->path2.c_str(), NULL);
            break;
        case FSAsyncWork::SYMLINK:
            r = uv_fs_symlink(NULL, &fs_req, work->path.c_str(), work->path2.c_str(), work->mode, NULL);
            break;
        case FSAsyncWork::READLINK:
            r = uv_fs_readlink(NULL, &fs_req, work->path.c_str(), NULL);
            if (r >= 0) work->string_res = (const char*)fs_req.ptr;
            break;
        case FSAsyncWork::REALPATH:
            r = uv_fs_realpath(NULL, &fs_req, work->path.c_str(), NULL);
            if (r >= 0) work->string_res = (const char*)fs_req.ptr;
            break;
        case FSAsyncWork::STAT:
            r = uv_fs_stat(NULL, &fs_req, work->path.c_str(), NULL);
            if (r >= 0) work->stat_res = fs_req.statbuf;
            break;
        case FSAsyncWork::LSTAT:
            r = uv_fs_lstat(NULL, &fs_req, work->path.c_str(), NULL);
            if (r >= 0) work->stat_res = fs_req.statbuf;
            break;
        case FSAsyncWork::RENAME:
            r = uv_fs_rename(NULL, &fs_req, work->path.c_str(), work->path2.c_str(), NULL);
            break;
        case FSAsyncWork::COPYFILE:
            r = uv_fs_copyfile(NULL, &fs_req, work->path.c_str(), work->path2.c_str(), work->mode, NULL);
            break;
        case FSAsyncWork::TRUNCATE: {
            uv_fs_t open_req;
            r = uv_fs_open(NULL, &open_req, work->path.c_str(), O_RDWR, 0, NULL);
            if (r >= 0) {
                uv_file fd = (uv_file)r;
                r = uv_fs_ftruncate(NULL, &fs_req, fd, work->len, NULL);
                uv_fs_t close_req;
                uv_fs_close(NULL, &close_req, fd, NULL);
                uv_fs_req_cleanup(&close_req);
            }
            uv_fs_req_cleanup(&open_req);
            break;
        }
        case FSAsyncWork::MKDIR:
            r = uv_fs_mkdir(NULL, &fs_req, work->path.c_str(), 0777, NULL);
            break;
        case FSAsyncWork::RMDIR:
            r = uv_fs_rmdir(NULL, &fs_req, work->path.c_str(), NULL);
            break;
        case FSAsyncWork::RM:
            // Simple rm for now
            r = uv_fs_stat(NULL, &fs_req, work->path.c_str(), NULL);
            if (r == 0) {
                uv_fs_t rm_req;
                if (S_ISDIR(fs_req.statbuf.st_mode)) {
                    r = uv_fs_rmdir(NULL, &rm_req, work->path.c_str(), NULL);
                } else {
                    r = uv_fs_unlink(NULL, &rm_req, work->path.c_str(), NULL);
                }
                uv_fs_req_cleanup(&rm_req);
            }
            break;
        case FSAsyncWork::MKDTEMP:
            r = uv_fs_mkdtemp(NULL, &fs_req, work->path.c_str(), NULL);
            if (r >= 0) work->string_res = fs_req.path;
            break;
        case FSAsyncWork::APPEND_FILE: {
            uv_fs_t open_req;
            int fd = uv_fs_open(NULL, &open_req, work->path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666, NULL);
            if (fd >= 0) {
                uv_fs_t write_req;
                uv_buf_t buf = uv_buf_init((char*)work->string_res.c_str(), work->string_res.length());
                r = uv_fs_write(NULL, &write_req, fd, &buf, 1, -1, NULL);
                uv_fs_req_cleanup(&write_req);
                
                uv_fs_t close_req;
                uv_fs_close(NULL, &close_req, fd, NULL);
                uv_fs_req_cleanup(&close_req);
            } else {
                r = fd;
            }
            uv_fs_req_cleanup(&open_req);
            break;
        }
        case FSAsyncWork::OPENDIR:
            r = uv_fs_opendir(NULL, &fs_req, work->path.c_str(), NULL);
            if (r >= 0) work->dir_ptr = (uv_dir_t*)fs_req.ptr;
            break;
        case FSAsyncWork::DIR_READ: {
            uv_dirent_t ent;
            work->dir_ptr->dirents = &ent;
            work->dir_ptr->nentries = 1;
            r = uv_fs_readdir(NULL, &fs_req, work->dir_ptr, NULL);
            if (r > 0) {
                work->dirent_read_res.name = ent.name;
                work->dirent_read_res.type = ent.type;
            }
            break;
        }
        case FSAsyncWork::DIR_CLOSE:
            r = uv_fs_closedir(NULL, &fs_req, work->dir_ptr, NULL);
            break;
        case FSAsyncWork::READDIR: {
            if (work->withFileTypes) {
                uv_fs_t opendir_req;
                r = uv_fs_opendir(NULL, &opendir_req, work->path.c_str(), NULL);
                if (r >= 0) {
                    uv_dir_t* dir = (uv_dir_t*)opendir_req.ptr;
                    uv_dirent_t ents[32];
                    dir->dirents = ents;
                    dir->nentries = 32;
                    int n;
                    while ((n = uv_fs_readdir(NULL, &opendir_req, dir, NULL)) > 0) {
                        for (int i = 0; i < n; i++) {
                            work->readdir_dirent_res.push_back({ents[i].name, ents[i].type});
                        }
                    }
                    uv_fs_t closedir_req;
                    uv_fs_closedir(NULL, &closedir_req, dir, NULL);
                    uv_fs_req_cleanup(&opendir_req);
                    uv_fs_req_cleanup(&closedir_req);
                    r = 0;
                }
            } else {
                uv_fs_t scandir_req;
                r = uv_fs_scandir(NULL, &scandir_req, work->path.c_str(), 0, NULL);
                if (r >= 0) {
                    uv_dirent_t ent;
                    while (uv_fs_scandir_next(&scandir_req, &ent) != UV_EOF) {
                        work->readdir_res.push_back(ent.name);
                    }
                }
                uv_fs_req_cleanup(&scandir_req);
            }
            break;
        }
    }
    work->result = r;
    work->success = (r >= 0);
    uv_fs_req_cleanup(&fs_req);
}

static void fs_async_after_worker(uv_work_t* req, int status);

static TsValue* dir_read_async_wrapper(void* context) {
    TsDir* dir = (TsDir*)context;
    if (!dir || !dir->dir) return ts_value_make_promise(ts::ts_promise_create());
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->dir_ptr = dir->dir;
    work->type = FSAsyncWork::DIR_READ;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

static TsValue* dir_close_async_wrapper(void* context) {
    TsDir* dir = (TsDir*)context;
    if (!dir || !dir->dir) return ts_value_make_promise(ts::ts_promise_create());
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->dir_ptr = dir->dir;
    work->type = FSAsyncWork::DIR_CLOSE;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
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
        } else if (work->type == FSAsyncWork::STAT || work->type == FSAsyncWork::LSTAT) {
            TsMap* res = TsMap::Create();
            add_stats_methods(res, &work->stat_res);
            ts::ts_promise_resolve_internal(work->promise, ts_value_make_object(res));
        } else if (work->type == FSAsyncWork::READLINK || work->type == FSAsyncWork::REALPATH || work->type == FSAsyncWork::MKDTEMP) {
            ts::ts_promise_resolve_internal(work->promise, ts_value_make_string(TsString::Create(work->string_res.c_str())));
        } else if (work->type == FSAsyncWork::OPENDIR) {
            TsDir* res = TsDir::Create(work->dir_ptr, work->path.c_str());
            res->Set(TsString::Create("readSync"), *ts_value_make_function((void*)dir_read_sync_wrapper, res));
            res->Set(TsString::Create("closeSync"), *ts_value_make_function((void*)dir_close_sync_wrapper, res));
            res->Set(TsString::Create("read"), *ts_value_make_function((void*)dir_read_async_wrapper, res));
            res->Set(TsString::Create("close"), *ts_value_make_function((void*)dir_close_async_wrapper, res));
            ts::ts_promise_resolve_internal(work->promise, ts_value_make_object(res));
        } else if (work->type == FSAsyncWork::DIR_READ) {
            if (work->result > 0) {
                TsDirent* res = TsDirent::Create(work->dirent_read_res.name.c_str(), work->dirent_read_res.type);
                ts::ts_promise_resolve_internal(work->promise, ts_value_make_object(res));
            } else {
                ts::ts_promise_resolve_internal(work->promise, ts_value_make_null());
            }
        } else if (work->type == FSAsyncWork::READDIR) {
            TsArray* res = TsArray::Create();
            if (work->withFileTypes) {
                for (const auto& entry : work->readdir_dirent_res) {
                    res->Push((int64_t)ts_value_make_object(TsDirent::Create(entry.name.c_str(), entry.type)));
                }
            } else {
                for (const auto& name : work->readdir_res) {
                    res->Push((int64_t)ts_value_make_string(TsString::Create(name.c_str())));
                }
            }
            ts::ts_promise_resolve_internal(work->promise, ts_value_make_array(res));
        } else {
            ts::ts_promise_resolve_internal(work->promise, ts_value_make_undefined());
        }
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "FS operation failed: %d (%s)", (int)work->result, uv_strerror((int)work->result));
        ts::ts_promise_reject_internal(work->promise, ts_value_make_string(ts_string_create(buf)));
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
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
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
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
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
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
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
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
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
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::STATFS;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_link_async(void* existingPath, void* newPath) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* existingStr = unboxString(existingPath);
    TsString* newStr = unboxString(newPath);
    work->path = existingStr ? existingStr->ToUtf8() : "";
    work->path2 = newStr ? newStr->ToUtf8() : "";
    work->type = FSAsyncWork::LINK;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_symlink_async(void* target, void* path, void* type) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* targetStr = unboxString(target);
    TsString* pathStr = unboxString(path);
    work->path = targetStr ? targetStr->ToUtf8() : "";
    work->path2 = pathStr ? pathStr->ToUtf8() : "";
    work->mode = 0;
    if (type) {
        TsString* typeStr = unboxString(type);
        if (typeStr) {
            const char* typeCStr = typeStr->ToUtf8();
            if (strcmp(typeCStr, "dir") == 0) work->mode |= UV_FS_SYMLINK_DIR;
            else if (strcmp(typeCStr, "junction") == 0) work->mode |= UV_FS_SYMLINK_JUNCTION;
        }
    }
    work->type = FSAsyncWork::SYMLINK;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_readlink_async(void* path) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::READLINK;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_realpath_async(void* path) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::REALPATH;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_stat_async(void* path) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::STAT;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_lstat_async(void* path) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::LSTAT;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_rename_async(void* oldPath, void* newPath) {
    fprintf(stderr, "DEBUG: ts_fs_rename_async oldPath=%p newPath=%p\n", oldPath, newPath);
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    
    TsString* oldStr = unboxString(oldPath);
    TsString* newStr = unboxString(newPath);

    work->path = oldStr ? oldStr->ToUtf8() : "";
    work->path2 = newStr ? newStr->ToUtf8() : "";
    work->type = FSAsyncWork::RENAME;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_copyFile_async(void* src, void* dest, double flags) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;

    TsString* srcStr = unboxString(src);
    TsString* destStr = unboxString(dest);

    work->path = srcStr ? srcStr->ToUtf8() : "";
    work->path2 = destStr ? destStr->ToUtf8() : "";
    work->mode = (int)flags;
    work->type = FSAsyncWork::COPYFILE;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_truncate_async(void* path, double len) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;

    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->len = (int64_t)len;
    work->type = FSAsyncWork::TRUNCATE;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_mkdir_async(void* path, void* options) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;

    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::MKDIR;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_rmdir_async(void* path, void* options) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;

    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::RMDIR;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_rm_async(void* path, void* options) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;

    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::RM;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_mkdtemp_async(void* prefix) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* prefixStr = unboxString(prefix);
    work->path = std::string(prefixStr ? prefixStr->ToUtf8() : "") + "XXXXXX";
    work->type = FSAsyncWork::MKDTEMP;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_opendir_async(void* path, void* options) {
    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    TsString* pathStr = unboxString(path);
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::OPENDIR;
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    return ts_value_make_promise(promise);
}

void* ts_fs_appendFile_async(void* path, void* content) {
    TsString* pathStr = unboxString(path);
    TsString* contentStr = unboxString(content);

    ts::TsPromise* promise = ts::ts_promise_create();
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->string_res = contentStr ? contentStr->ToUtf8() : ""; // Reuse string_res for content
    work->type = FSAsyncWork::APPEND_FILE;
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

static TsValue* link_promise_wrapper(void* context, TsValue* existingPath, TsValue* newPath) {
    if (!existingPath || existingPath->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!newPath || newPath->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_link_async(existingPath->ptr_val, newPath->ptr_val);
}

static TsValue* symlink_promise_wrapper(void* context, TsValue* target, TsValue* path, TsValue* type) {
    if (!target || target->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    void* t = type ? type->ptr_val : nullptr;
    return (TsValue*)ts_fs_symlink_async(target->ptr_val, path->ptr_val, t);
}

static TsValue* readlink_promise_wrapper(void* context, TsValue* path) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_readlink_async(path->ptr_val);
}

static TsValue* realpath_promise_wrapper(void* context, TsValue* path) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_realpath_async(path->ptr_val);
}

static TsValue* stat_promise_wrapper(void* context, TsValue* path) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_stat_async(path->ptr_val);
}

static TsValue* lstat_promise_wrapper(void* context, TsValue* path) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_lstat_async(path->ptr_val);
}

static TsValue* rename_promise_wrapper(void* context, TsValue* oldPath, TsValue* newPath) {
    if (!oldPath || oldPath->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!newPath || newPath->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_rename_async(oldPath->ptr_val, newPath->ptr_val);
}

static TsValue* copyFile_promise_wrapper(void* context, TsValue* src, TsValue* dest, TsValue* flags) {
    if (!src || src->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    if (!dest || dest->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    double f = 0;
    if (flags && (flags->type == ValueType::NUMBER_INT || flags->type == ValueType::NUMBER_DBL)) {
        f = (flags->type == ValueType::NUMBER_INT) ? (double)flags->i_val : flags->d_val;
    }
    return (TsValue*)ts_fs_copyFile_async(src->ptr_val, dest->ptr_val, f);
}

static TsValue* truncate_promise_wrapper(void* context, TsValue* path, TsValue* len) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    double l = 0;
    if (len && (len->type == ValueType::NUMBER_INT || len->type == ValueType::NUMBER_DBL)) {
        l = (len->type == ValueType::NUMBER_INT) ? (double)len->i_val : len->d_val;
    }
    return (TsValue*)ts_fs_truncate_async(path->ptr_val, l);
}

static TsValue* appendFile_promise_wrapper(void* context, TsValue* path, TsValue* data) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_appendFile_async(path->ptr_val, data);
}

static TsValue* mkdir_promise_wrapper(void* context, TsValue* path, TsValue* options) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_mkdir_async(path->ptr_val, options);
}

static TsValue* rmdir_promise_wrapper(void* context, TsValue* path, TsValue* options) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_rmdir_async(path->ptr_val, options);
}

static TsValue* rm_promise_wrapper(void* context, TsValue* path, TsValue* options) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_rm_async(path->ptr_val, options);
}

static TsValue* mkdtemp_promise_wrapper(void* context, TsValue* prefix) {
    if (!prefix || prefix->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_mkdtemp_async(prefix->ptr_val);
}

static TsValue* opendir_promise_wrapper(void* context, TsValue* path, TsValue* options) {
    if (!path || path->type != ValueType::STRING_PTR) return ts_value_make_undefined();
    return (TsValue*)ts_fs_opendir_async(path->ptr_val, options);
}

static TsValue* readdir_promise_wrapper(void* context, TsValue* path, TsValue* options) {
    return (TsValue*)ts_fs_readdir_async(path, options);
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
    promises->Set(TsString::Create("link"), *ts_value_make_function((void*)link_promise_wrapper, nullptr));
    promises->Set(TsString::Create("symlink"), *ts_value_make_function((void*)symlink_promise_wrapper, nullptr));
    promises->Set(TsString::Create("readlink"), *ts_value_make_function((void*)readlink_promise_wrapper, nullptr));
    promises->Set(TsString::Create("realpath"), *ts_value_make_function((void*)realpath_promise_wrapper, nullptr));
    promises->Set(TsString::Create("stat"), *ts_value_make_function((void*)stat_promise_wrapper, nullptr));
    promises->Set(TsString::Create("lstat"), *ts_value_make_function((void*)lstat_promise_wrapper, nullptr));
    promises->Set(TsString::Create("rename"), *ts_value_make_function((void*)rename_promise_wrapper, nullptr));
    promises->Set(TsString::Create("copyFile"), *ts_value_make_function((void*)copyFile_promise_wrapper, nullptr));
    promises->Set(TsString::Create("truncate"), *ts_value_make_function((void*)truncate_promise_wrapper, nullptr));
    promises->Set(TsString::Create("appendFile"), *ts_value_make_function((void*)appendFile_promise_wrapper, nullptr));
    promises->Set(TsString::Create("mkdir"), *ts_value_make_function((void*)mkdir_promise_wrapper, nullptr));
    promises->Set(TsString::Create("rmdir"), *ts_value_make_function((void*)rmdir_promise_wrapper, nullptr));
    promises->Set(TsString::Create("rm"), *ts_value_make_function((void*)rm_promise_wrapper, nullptr));
    promises->Set(TsString::Create("mkdtemp"), *ts_value_make_function((void*)mkdtemp_promise_wrapper, nullptr));
    promises->Set(TsString::Create("opendir"), *ts_value_make_function((void*)opendir_promise_wrapper, nullptr));
    promises->Set(TsString::Create("readdir"), *ts_value_make_function((void*)readdir_promise_wrapper, nullptr));
    return ts_value_make_object(promises);
}

int64_t ts_fs_openSync(void* path, void* flags) {
    TsString* pathStr = unboxString(path);
    TsString* flagsStr = unboxString(flags);
    const char* pathCStr = pathStr ? pathStr->ToUtf8() : "";
    const char* flagsCStr = flagsStr ? flagsStr->ToUtf8() : "r";

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

void* ts_fs_readdir_async(void* path, void* options) {
    TsString* pathStr = unboxString(path);
    ts::TsPromise* promise = ts::ts_promise_create();
    
    FSAsyncWork* work = (FSAsyncWork*)GC_malloc_uncollectable(sizeof(FSAsyncWork));
    new (work) FSAsyncWork();
    work->promise = promise;
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->type = FSAsyncWork::READDIR;
    
    if (options) {
        TsValue* optVal = (TsValue*)options;
        if (optVal->type == ValueType::OBJECT_PTR) {
            TsMap* optMap = (TsMap*)optVal->ptr_val;
            TsValue withFileTypes = optMap->Get(TsString::Create("withFileTypes"));
            if (withFileTypes.type == ValueType::BOOLEAN) {
                work->withFileTypes = withFileTypes.b_val;
            }
        }
    }
    
    uv_work_t* req = (uv_work_t*)malloc(sizeof(uv_work_t));
    req->data = work;
    uv_queue_work(uv_default_loop(), req, fs_async_worker, fs_async_after_worker);
    
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
    TsString* pathStr = unboxString(path);
    ts::TsPromise* promise = ts::ts_promise_create();
    
    ReadFileWork* work = (ReadFileWork*)GC_malloc_uncollectable(sizeof(ReadFileWork));
    new (work) ReadFileWork();
    work->promise = promise;
    work->path = pathStr ? pathStr->ToUtf8() : "";
    
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
    TsString* pathStr = unboxString(path);
    TsString* contentStr = unboxString(content);
    ts::TsPromise* promise = ts::ts_promise_create();
    
    WriteFileWork* work = (WriteFileWork*)GC_malloc_uncollectable(sizeof(WriteFileWork));
    new (work) WriteFileWork();
    work->promise = promise;
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->content = contentStr ? contentStr->ToUtf8() : "";
    
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
    TsString* pathStr = unboxString(path);
    TsString* flagsStr = unboxString(flags);
    ts::TsPromise* promise = ts::ts_promise_create();
    
    OpenWork* work = (OpenWork*)GC_malloc_uncollectable(sizeof(OpenWork));
    new (work) OpenWork();
    work->promise = promise;
    work->path = pathStr ? pathStr->ToUtf8() : "";
    work->flags = flagsStr ? flagsStr->ToUtf8() : "r";
    
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
