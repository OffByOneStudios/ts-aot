#pragma once
#include <cstddef>
#include <cstdint>
#include "TsObject.h"

class TsString;

extern "C" {

// --- Memory Management ---
void* ts_alloc(size_t size);
void ts_gc_init();
void ts_panic(const char* msg);

// --- Event Loop ---
void ts_loop_init();
void ts_loop_run();
void ts_queue_microtask(void (*callback)(void*), void* data);
void ts_run_microtasks();

// --- Console ---
void ts_console_log(TsString* str);
void ts_console_log_int(int64_t val);
void ts_console_log_double(double val);
void ts_console_log_bool(bool val);
void ts_console_log_value(TsValue* val);
bool ts_value_is_nullish(TsValue* v);

// --- Conversions ---
int32_t ts_double_to_int32(double d);
uint32_t ts_double_to_uint32(double d);
void* ts_number_to_string(double val, int64_t radix);
void* ts_number_to_fixed(double val, int64_t digits);
void* ts_int_to_string(int64_t val, int64_t radix);
void* ts_double_to_string(double val, int64_t radix);
void* ts_double_to_fixed(double val, int64_t digits);

// --- String ---
void* ts_string_create(const char* str);
void* ts_string_concat(void* a, void* b);
void* ts_string_from_int(int64_t val);
void* ts_string_from_double(double val);
void* ts_string_from_bool(bool val);
void* ts_string_from_value(TsValue* val);
int64_t ts_string_length(void* str);

// --- Array ---
int64_t ts_array_length(void* arr);

// --- Value Length ---
int64_t ts_value_length(TsValue* val);

// --- Exceptions ---
void* ts_push_exception_handler();
void ts_pop_exception_handler();
void ts_throw(TsValue* exception);
void ts_set_exception(TsValue* exception);
TsValue* ts_get_exception();

// --- Entry Point ---
int ts_main(int argc, char** argv, TsValue* (*user_main)(void*));
void* ts_get_process_argv();
void* ts_get_process_env();
void ts_process_exit(int64_t code);
void* ts_process_cwd();

// --- BigInt ---
void* ts_bigint_create_int(int64_t val);
void* ts_bigint_create_str(const char* str, int32_t radix);
void* ts_bigint_to_string(void* bi, int32_t radix);
void* ts_bigint_from_value(TsValue* val);

// --- Symbol ---
void* ts_symbol_create(void* desc);
void* ts_symbol_for(void* key);
void* ts_symbol_key_for(void* sym);

// --- Value Creation ---
TsValue* ts_value_make_undefined();
TsValue* ts_value_make_int(int64_t i);
TsValue* ts_value_make_double(double d);
TsValue* ts_value_make_bool(bool b);
TsValue* ts_value_make_string(void* s);
TsValue* ts_value_make_object(void* o);
TsValue* ts_value_make_bigint(void* b);
TsValue* ts_value_make_symbol(void* s);
void* ts_value_get_object(TsValue* v);
TsValue* ts_value_make_promise(void* p);
TsValue* ts_value_make_function(void* funcPtr, void* context);
void* ts_function_get_ptr(TsValue* val);
void* ts_error_create(void* message);

int64_t ts_value_get_int(TsValue* v);
double ts_value_get_double(TsValue* v);
void* ts_value_get_string(TsValue* v);
bool ts_value_to_bool(TsValue* v);
bool ts_value_is_nullish(TsValue* v);

// --- Promises ---
TsValue* ts_promise_all(TsValue* iterable);

// --- File System (fs) ---
void* ts_fs_readFileSync(void* path);
void ts_fs_writeFileSync(void* path, void* content);
bool ts_fs_existsSync(void* path);
void ts_fs_mkdirSync(void* path, void* options);
void ts_fs_rmdirSync(void* path, void* options);
void ts_fs_rmSync(void* path, void* options);
void ts_fs_unlinkSync(void* path);
void ts_fs_renameSync(void* oldPath, void* newPath);
void ts_fs_copyFileSync(void* src, void* dest, int32_t flags);
void ts_fs_truncateSync(void* path, int64_t len);
void ts_fs_appendFileSync(void* path, void* content);
void* ts_fs_mkdtempSync(void* prefix);
void* ts_fs_statSync(void* path);
void* ts_fs_lstatSync(void* path);
void* ts_fs_readlinkSync(void* path);
void* ts_fs_realpathSync(void* path);
void* ts_fs_readdirSync(void* path);
void ts_fs_accessSync(void* path, int32_t mode);
void ts_fs_chmodSync(void* path, int32_t mode);
void ts_fs_chownSync(void* path, int32_t uid, int32_t gid);
void ts_fs_utimesSync(void* path, double atime, double mtime);
void* ts_fs_statfsSync(void* path);
void ts_fs_linkSync(void* existingPath, void* newPath);
void ts_fs_symlinkSync(void* target, void* path, void* type);
void* ts_fs_get_constants();
void* ts_fs_get_promises();
int64_t ts_fs_openSync(void* path, void* flags);
void ts_fs_closeSync(int64_t fd);
void* ts_fs_readdir_async(void* path);
void* ts_fs_readFile_async(void* path);
void* ts_fs_writeFile_async(void* path, void* content);
void* ts_fs_mkdir_async(void* path, void* options);
void* ts_fs_rmdir_async(void* path, void* options);
void* ts_fs_rm_async(void* path, void* options);
void* ts_fs_stat_async(void* path);
void* ts_fs_lstat_async(void* path);
void* ts_fs_rename_async(void* oldPath, void* newPath);
void* ts_fs_copyFile_async(void* src, void* dest, double flags);
void* ts_fs_truncate_async(void* path, double len);
void* ts_fs_appendFile_async(void* path, void* content);
void* ts_fs_mkdtemp_async(void* prefix);
void* ts_fs_access_async(void* path, double mode);
void* ts_fs_chmod_async(void* path, double mode);
void* ts_fs_chown_async(void* path, double uid, double gid);
void* ts_fs_utimes_async(void* path, double atime, double mtime);
void* ts_fs_statfs_async(void* path);
void* ts_fs_link_async(void* existingPath, void* newPath);
void* ts_fs_symlink_async(void* target, void* path, void* type);
void* ts_fs_readlink_async(void* path);
void* ts_fs_realpath_async(void* path);
void* ts_fs_createReadStream(void* path);

// --- Path ---
void* ts_path_join(void* path1, void* path2);
void* ts_path_resolve(void* path);
void* ts_path_dirname(void* path);
void* ts_path_basename(void* path);
void* ts_path_extname(void* path);
TsValue* ts_promise_race(TsValue* iterable);
TsValue* ts_promise_allSettled(TsValue* iterable);
TsValue* ts_promise_any(TsValue* iterable);
TsValue* ts_promise_then(TsValue* promise, TsValue* onFulfilled, TsValue* onRejected);
TsValue* ts_call_0(TsValue* func);

}
