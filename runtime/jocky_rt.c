// JOCKY v0.1 runtime implementation.
#define _GNU_SOURCE
#include "jocky_rt.h"

#include <gc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Allocation
// ---------------------------------------------------------------------------
void* jky_alloc(size_t n) {
    void* p = GC_MALLOC(n);
    if (!p) { fprintf(stderr, "[PANIC] out of memory\n"); exit(139); }
    return p;
}

static char* dup_n(const char* src, int64_t len) {
    char* p = (char*)jky_alloc((size_t)len + 1);
    if (src) memcpy(p, src, (size_t)len);
    p[len] = '\0';
    return p;
}

// ---------------------------------------------------------------------------
// Panics
// ---------------------------------------------------------------------------
static void iso_now(char* buf, size_t n) {
    time_t t = time(NULL);
    struct tm g;
    gmtime_r(&t, &g);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &g);
}

void jky_panic(const char* msg, int32_t line) {
    char ts[32];
    iso_now(ts, sizeof ts);
    fprintf(stderr, "[PANIC] [%s] FATAL: %s", ts, msg);
    if (line > 0) fprintf(stderr, " at line %d", line);
    fprintf(stderr, "\nProcess terminated with exit code 139.\n");
    exit(139);
}

void jky_nil_check(const void* ref, int32_t line) {
    if (!ref) jky_panic("Nil pointer dereference", line);
}

static void bounds_check(int64_t i, int64_t len, int32_t line) {
    if (i < 0 || i >= len) {
        char buf[128];
        snprintf(buf, sizeof buf,
                 "Index %lld out of range for length %lld",
                 (long long)i, (long long)len);
        jky_panic(buf, line);
    }
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------
jky_string* jky_str_new(const char* data, int64_t len) {
    jky_string* s = (jky_string*)jky_alloc(sizeof *s);
    s->len = len;
    s->data = dup_n(data, len);
    return s;
}
jky_string* jky_str_concat(jky_string* a, jky_string* b) {
    jky_nil_check(a, 0);
    jky_nil_check(b, 0);
    int64_t n = a->len + b->len;
    char* p = (char*)jky_alloc((size_t)n + 1);
    memcpy(p, a->data, (size_t)a->len);
    memcpy(p + a->len, b->data, (size_t)b->len);
    p[n] = '\0';
    jky_string* s = (jky_string*)jky_alloc(sizeof *s);
    s->len = n;
    s->data = p;
    return s;
}
int8_t jky_str_eq(jky_string* a, jky_string* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    return memcmp(a->data, b->data, (size_t)a->len) == 0;
}
int64_t jky_str_len(jky_string* s) { jky_nil_check(s, 0); return s->len; }
int8_t jky_str_contains(jky_string* s, jky_string* sub) {
    jky_nil_check(s, 0);
    jky_nil_check(sub, 0);
    if (sub->len == 0) return 1;
    if (sub->len > s->len) return 0;
    return memmem(s->data, (size_t)s->len, sub->data, (size_t)sub->len) != NULL;
}
jky_string* jky_str_slice(jky_string* s, int64_t start, int64_t end) {
    jky_nil_check(s, 0);
    if (start < 0) start = 0;
    if (end > s->len) end = s->len;
    if (end < start) end = start;
    return jky_str_new(s->data + start, end - start);
}
jky_string* jky_str_from_int(int64_t v) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%lld", (long long)v);
    return jky_str_new(buf, n);
}
jky_string* jky_str_from_float(double v) {
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%g", v);
    return jky_str_new(buf, n);
}
jky_string* jky_str_from_bool(int8_t v) {
    return v ? jky_str_new("true", 4) : jky_str_new("false", 5);
}
jky_string* jky_str_from_byte(uint8_t v) { return jky_str_from_int((int64_t)v); }

// ---------------------------------------------------------------------------
// Bytes
// ---------------------------------------------------------------------------
jky_bytes* jky_bytes_new(const uint8_t* data, int64_t len) {
    jky_bytes* b = (jky_bytes*)jky_alloc(sizeof *b);
    b->len = len;
    b->cap = len;
    b->data = (uint8_t*)jky_alloc((size_t)len ? (size_t)len : 1);
    if (data && len) memcpy(b->data, data, (size_t)len);
    return b;
}
int64_t jky_bytes_len(jky_bytes* b) { jky_nil_check(b, 0); return b->len; }
uint8_t jky_bytes_get(jky_bytes* b, int64_t i) {
    jky_nil_check(b, 0);
    bounds_check(i, b->len, 0);
    return b->data[i];
}
void jky_bytes_set(jky_bytes* b, int64_t i, uint8_t v) {
    jky_nil_check(b, 0);
    bounds_check(i, b->len, 0);
    b->data[i] = v;
}
jky_bytes* jky_bytes_slice(jky_bytes* b, int64_t s, int64_t e) {
    jky_nil_check(b, 0);
    if (s < 0) s = 0;
    if (e > b->len) e = b->len;
    if (e < s) e = s;
    return jky_bytes_new(b->data + s, e - s);
}
int8_t jky_bytes_eq(jky_bytes* a, jky_bytes* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    return memcmp(a->data, b->data, (size_t)a->len) == 0;
}

// ---------------------------------------------------------------------------
// List (8-byte slots)
// ---------------------------------------------------------------------------
jky_list* jky_list_new(void) {
    jky_list* l = (jky_list*)jky_alloc(sizeof *l);
    l->len = 0;
    l->cap = 0;
    l->data = NULL;
    return l;
}
void jky_list_append(jky_list* l, int64_t slot) {
    jky_nil_check(l, 0);
    if (l->len == l->cap) {
        int64_t nc = l->cap ? l->cap * 2 : 4;
        int64_t* nd = (int64_t*)jky_alloc((size_t)nc * sizeof(int64_t));
        if (l->data) memcpy(nd, l->data, (size_t)l->len * sizeof(int64_t));
        l->data = nd;
        l->cap = nc;
    }
    l->data[l->len++] = slot;
}
int64_t jky_list_get(jky_list* l, int64_t i) {
    jky_nil_check(l, 0);
    bounds_check(i, l->len, 0);
    return l->data[i];
}
void jky_list_set(jky_list* l, int64_t i, int64_t slot) {
    jky_nil_check(l, 0);
    bounds_check(i, l->len, 0);
    l->data[i] = slot;
}
int64_t jky_list_len(jky_list* l) { jky_nil_check(l, 0); return l->len; }

// ---------------------------------------------------------------------------
// Map (open-addressing hash table)
// ---------------------------------------------------------------------------
typedef struct { int64_t key; int64_t val; uint8_t used; } jky_map_slot;
struct jky_map {
    int32_t keykind; // 0 = raw int-like, 1 = jky_string* by content
    int64_t count;
    int64_t cap;
    jky_map_slot* slots;
};

static uint64_t slot_hash(int32_t keykind, int64_t key) {
    if (keykind == 1) {
        jky_string* s = (jky_string*)(intptr_t)key;
        uint64_t h = 1469598103934665603ULL; // FNV-1a
        if (s) for (int64_t i = 0; i < s->len; i++) {
            h ^= (uint8_t)s->data[i];
            h *= 1099511628211ULL;
        }
        return h;
    }
    uint64_t x = (uint64_t)key;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
    return x;
}
static int key_eq(int32_t keykind, int64_t a, int64_t b) {
    if (keykind == 1)
        return jky_str_eq((jky_string*)(intptr_t)a, (jky_string*)(intptr_t)b);
    return a == b;
}

jky_map* jky_map_new(int32_t keykind) {
    jky_map* m = (jky_map*)jky_alloc(sizeof *m);
    m->keykind = keykind;
    m->count = 0;
    m->cap = 8;
    m->slots = (jky_map_slot*)jky_alloc((size_t)m->cap * sizeof(jky_map_slot));
    memset(m->slots, 0, (size_t)m->cap * sizeof(jky_map_slot));
    return m;
}
static void map_grow(jky_map* m) {
    int64_t oldCap = m->cap;
    jky_map_slot* old = m->slots;
    m->cap *= 2;
    m->slots = (jky_map_slot*)jky_alloc((size_t)m->cap * sizeof(jky_map_slot));
    memset(m->slots, 0, (size_t)m->cap * sizeof(jky_map_slot));
    m->count = 0;
    for (int64_t i = 0; i < oldCap; i++)
        if (old[i].used) jky_map_set(m, old[i].key, old[i].val);
}
void jky_map_set(jky_map* m, int64_t key, int64_t val) {
    jky_nil_check(m, 0);
    if ((m->count + 1) * 10 >= m->cap * 7) map_grow(m); // load factor 0.7
    uint64_t mask = (uint64_t)m->cap - 1;
    uint64_t i = slot_hash(m->keykind, key) & mask;
    while (m->slots[i].used) {
        if (key_eq(m->keykind, m->slots[i].key, key)) { m->slots[i].val = val; return; }
        i = (i + 1) & mask;
    }
    m->slots[i].used = 1;
    m->slots[i].key = key;
    m->slots[i].val = val;
    m->count++;
}
int64_t jky_map_get(jky_map* m, int64_t key, int8_t* found) {
    jky_nil_check(m, 0);
    uint64_t mask = (uint64_t)m->cap - 1;
    uint64_t i = slot_hash(m->keykind, key) & mask;
    while (m->slots[i].used) {
        if (key_eq(m->keykind, m->slots[i].key, key)) {
            if (found) *found = 1;
            return m->slots[i].val;
        }
        i = (i + 1) & mask;
    }
    if (found) *found = 0;
    return 0;
}
int8_t jky_map_has(jky_map* m, int64_t key) {
    int8_t f = 0;
    jky_map_get(m, key, &f);
    return f;
}
int64_t jky_map_len(jky_map* m) { jky_nil_check(m, 0); return m->count; }

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static void log_line(const char* level, jky_string* s) {
    char ts[32];
    iso_now(ts, sizeof ts);
    fprintf(stderr, "[%s] [%s] %s\n", level, ts, s && s->data ? s->data : "");
}
void jky_log_info(jky_string* s) { log_line("INFO", s); }
void jky_log_warn(jky_string* s) { log_line("WARN", s); }
void jky_log_error(jky_string* s) { log_line("ERROR", s); }

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------
jky_error* jky_err_new(int64_t code, jky_string* msg) {
    jky_error* e = (jky_error*)jky_alloc(sizeof *e);
    e->code = code;
    e->message = msg;
    return e;
}
jky_read_result jky_fs_read(jky_string* path) {
    jky_read_result r = {NULL, NULL};
    jky_nil_check(path, 0);
    FILE* f = fopen(path->data, "rb");
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof buf, "cannot open '%s'", path->data);
        r.err = jky_err_new(1, jky_str_new(buf, (int64_t)strlen(buf)));
        return r;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    jky_bytes* b = jky_bytes_new(NULL, sz);
    if (sz > 0 && fread(b->data, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        r.err = jky_err_new(2, jky_str_new("short read", 10));
        return r;
    }
    fclose(f);
    r.data = b;
    return r;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(void) {
    GC_INIT();
    return (int)jky_main();
}
