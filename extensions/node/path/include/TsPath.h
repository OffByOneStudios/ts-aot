// TsPath.h - Path module for ts-aot
//
// Node.js-compatible path manipulation functions.
// This module is built as a separate library (ts_path) and linked
// only when the path module is imported.

#ifndef TS_PATH_H
#define TS_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

// Basic path operations
void* ts_path_join(void* path1, void* path2);
void* ts_path_join_variadic(void* paths_ptr);
void* ts_path_join_variadic_ex(void* paths_ptr, int platform);

void* ts_path_resolve(void* paths_ptr);
void* ts_path_resolve_ex(void* paths_ptr, int platform);

void* ts_path_normalize(void* path_ptr);
void* ts_path_normalize_ex(void* path_ptr, int platform);

int ts_path_is_absolute(void* path_ptr);
int ts_path_is_absolute_ex(void* path_ptr, int platform);

void* ts_path_basename(void* path_ptr, void* ext_ptr);
void* ts_path_basename_ex(void* path_ptr, void* ext_ptr, int platform);

void* ts_path_dirname(void* path_ptr);
void* ts_path_dirname_ex(void* path_ptr, int platform);

void* ts_path_extname(void* path_ptr);
void* ts_path_extname_ex(void* path_ptr, int platform);

void* ts_path_relative(void* from_ptr, void* to_ptr);
void* ts_path_relative_ex(void* from_ptr, void* to_ptr, int platform);

// Path separators and delimiters
void* ts_path_get_sep();
void* ts_path_get_sep_win32();
void* ts_path_get_sep_posix();

void* ts_path_get_delimiter();
void* ts_path_get_delimiter_win32();
void* ts_path_get_delimiter_posix();

// Parse and format
void* ts_path_parse(void* path_ptr);
void* ts_path_parse_ex(void* path_ptr, int platform);

void* ts_path_format(void* obj_ptr);
void* ts_path_format_ex(void* obj_ptr, int platform);

void* ts_path_to_namespaced_path(void* path_ptr);
void* ts_path_to_namespaced_path_ex(void* path_ptr, int platform);

#ifdef __cplusplus
}
#endif

#endif // TS_PATH_H
