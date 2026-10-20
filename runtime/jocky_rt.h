// JOCKY v0.1 runtime ABI.
// All reference types are heap pointers (nil == NULL). List elements and map
// keys/values are passed as raw 8-byte slots (int64_t); the compiler packs and
// unpacks them according to the statically known element type.
#ifndef JOCKY_RT_H
#define JOCKY_RT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int64_t len; const char* data; } jky_string; // data is NUL-terminated
typedef struct { int64_t len; int64_t cap; uint8_t* data; } jky_bytes;
typedef struct { int64_t len; int64_t cap; int64_t* data; } jky_list;
typedef struct jky_map jky_map;
typedef struct { int64_t code; jky_string* message; } jky_error;

// Result of fs.read — mirrors a JOCKY (bytes, Error) tuple as LLVM {ptr,ptr}.
typedef struct { jky_bytes* data; jky_error* err; } jky_read_result;

// --- allocation (GC) ---
void* jky_alloc(size_t n);

// --- strings ---
jky_string* jky_str_new(const char* data, int64_t len);
jky_string* jky_str_concat(jky_string* a, jky_string* b);
int8_t      jky_str_eq(jky_string* a, jky_string* b);
int64_t     jky_str_len(jky_string* s);
int8_t      jky_str_contains(jky_string* s, jky_string* sub);
jky_string* jky_str_slice(jky_string* s, int64_t start, int64_t end);
jky_string* jky_str_from_int(int64_t v);
jky_string* jky_str_from_float(double v);
jky_string* jky_str_from_bool(int8_t v);
jky_string* jky_str_from_byte(uint8_t v);

// --- bytes ---
jky_bytes*  jky_bytes_new(const uint8_t* data, int64_t len);
int64_t     jky_bytes_len(jky_bytes* b);
uint8_t     jky_bytes_get(jky_bytes* b, int64_t i);        // bounds-checked
void        jky_bytes_set(jky_bytes* b, int64_t i, uint8_t v);
jky_bytes*  jky_bytes_slice(jky_bytes* b, int64_t s, int64_t e);
int8_t      jky_bytes_eq(jky_bytes* a, jky_bytes* b);

// --- list (generic 8-byte slots) ---
jky_list*   jky_list_new(void);
void        jky_list_append(jky_list* l, int64_t slot);
int64_t     jky_list_get(jky_list* l, int64_t i);          // bounds-checked
void        jky_list_set(jky_list* l, int64_t i, int64_t slot);
int64_t     jky_list_len(jky_list* l);

// --- map (keykind: 0 = raw 8-byte key, 1 = jky_string* key by content) ---
jky_map*    jky_map_new(int32_t keykind);
void        jky_map_set(jky_map* m, int64_t key, int64_t val);
int64_t     jky_map_get(jky_map* m, int64_t key, int8_t* found);
int8_t      jky_map_has(jky_map* m, int64_t key);
int64_t     jky_map_len(jky_map* m);

// --- logging ---
void jky_log_info(jky_string* s);
void jky_log_warn(jky_string* s);
void jky_log_error(jky_string* s);

// --- filesystem ---
jky_read_result jky_fs_read(jky_string* path);

// --- errors / panics ---
jky_error* jky_err_new(int64_t code, jky_string* msg);
void jky_panic(const char* msg, int32_t line) __attribute__((noreturn));
void jky_nil_check(const void* ref, int32_t line);

// Provided by generated code: the user's `fn main`.
int64_t jky_main(void);

#ifdef __cplusplus
}
#endif
#endif // JOCKY_RT_H
