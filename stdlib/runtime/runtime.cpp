#include "runtime.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <regex>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#elif defined(LIVA_HAS_CURL)
#include <curl/curl.h>
#endif

extern "C" {

// === Memory Management ===

void *liva_alloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        liva_panic("out of memory");
    }
    return ptr;
}

void liva_free(void *ptr) {
    free(ptr);
}

void *liva_alloc_zeroed(size_t size) {
    void *ptr = calloc(1, size);
    if (!ptr) {
        liva_panic("out of memory");
    }
    return ptr;
}

// === String Operations ===

LivaString liva_string_new(const char *str) {
    size_t len = strlen(str);
    LivaString s;
    s.length = len;
    s.capacity = len + 1;
    s.data = (char *)liva_alloc(s.capacity);
    memcpy(s.data, str, len + 1);
    return s;
}

LivaString liva_string_from_parts(const char *data, size_t len) {
    LivaString s;
    s.length = len;
    s.capacity = len + 1;
    s.data = (char *)liva_alloc(s.capacity);
    memcpy(s.data, data, len);
    s.data[len] = '\0';
    return s;
}

void liva_string_free(LivaString *str) {
    if (str->data) {
        liva_free(str->data);
        str->data = nullptr;
        str->length = 0;
        str->capacity = 0;
    }
}

LivaString liva_string_concat(const LivaString *a, const LivaString *b) {
    LivaString result;
    result.length = a->length + b->length;
    result.capacity = result.length + 1;
    result.data = (char *)liva_alloc(result.capacity);
    memcpy(result.data, a->data, a->length);
    memcpy(result.data + a->length, b->data, b->length);
    result.data[result.length] = '\0';
    return result;
}

int32_t liva_string_compare(const LivaString *a, const LivaString *b) {
    return strcmp(a->data, b->data);
}

// === Simple char*-based string operations ===

char *liva_str_concat(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    char *result = (char *)malloc(la + lb + 1);
    memcpy(result, a, la);
    memcpy(result + la, b, lb + 1);
    return result;
}

int32_t liva_str_equal(const char *a, const char *b) {
    return strcmp(a, b) == 0 ? 1 : 0;
}

int64_t liva_str_length(const char *a) {
    return (int64_t)strlen(a);
}

char *liva_i32_to_str(int32_t value) {
    char *buf = (char *)malloc(16);
    snprintf(buf, 16, "%d", value);
    return buf;
}

char *liva_f64_to_str(double value) {
    char *buf = (char *)malloc(32);
    snprintf(buf, 32, "%g", value);
    return buf;
}

char *liva_bool_to_str(int8_t value) {
    const char *s = value ? "true" : "false";
    char *buf = (char *)malloc(6);
    strcpy(buf, s);
    return buf;
}

// === Type Conversion (String → Number) ===

int8_t liva_str_parse_i32(const char *str, int32_t *result) {
    if (!str || !*str) return 0;
    char *end = nullptr;
    long val = strtol(str, &end, 10);
    if (end == str || *end != '\0') return 0;
    if (val < INT32_MIN || val > INT32_MAX) return 0;
    *result = (int32_t)val;
    return 1;
}

int8_t liva_str_parse_i64(const char *str, int64_t *result) {
    if (!str || !*str) return 0;
    char *end = nullptr;
    long long val = strtoll(str, &end, 10);
    if (end == str || *end != '\0') return 0;
    *result = (int64_t)val;
    return 1;
}

int8_t liva_str_parse_f64(const char *str, double *result) {
    if (!str || !*str) return 0;
    char *end = nullptr;
    double val = strtod(str, &end);
    if (end == str || *end != '\0') return 0;
    *result = val;
    return 1;
}

// === String Methods ===

int8_t liva_str_contains(const char *str, const char *substr) {
    return strstr(str, substr) != nullptr ? 1 : 0;
}

int8_t liva_str_starts_with(const char *str, const char *prefix) {
    size_t pl = strlen(prefix);
    return strncmp(str, prefix, pl) == 0 ? 1 : 0;
}

int8_t liva_str_ends_with(const char *str, const char *suffix) {
    size_t sl = strlen(str), xl = strlen(suffix);
    if (xl > sl) return 0;
    return strcmp(str + sl - xl, suffix) == 0 ? 1 : 0;
}

int64_t liva_str_index_of(const char *str, const char *substr) {
    const char *p = strstr(str, substr);
    return p ? (int64_t)(p - str) : -1;
}

char *liva_str_substring(const char *str, int64_t start, int64_t length) {
    int64_t sl = (int64_t)strlen(str);
    if (start < 0) start = 0;
    if (start > sl) start = sl;
    if (length < 0) length = 0;
    if (start + length > sl) length = sl - start;
    char *result = (char *)malloc((size_t)length + 1);
    memcpy(result, str + start, (size_t)length);
    result[length] = '\0';
    return result;
}

char *liva_str_trim(const char *str) {
    const char *start = str;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
        start++;
    const char *end = str + strlen(str);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        end--;
    size_t len = (size_t)(end - start);
    char *result = (char *)malloc(len + 1);
    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

char *liva_str_to_upper(const char *str) {
    size_t len = strlen(str);
    char *result = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        result[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    result[len] = '\0';
    return result;
}

char *liva_str_to_lower(const char *str) {
    size_t len = strlen(str);
    char *result = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        result[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    result[len] = '\0';
    return result;
}

char *liva_str_replace(const char *str, const char *old_sub, const char *new_sub) {
    size_t sl = strlen(str), ol = strlen(old_sub), nl = strlen(new_sub);
    if (ol == 0) {
        char *result = (char *)malloc(sl + 1);
        memcpy(result, str, sl + 1);
        return result;
    }
    // Count occurrences
    size_t count = 0;
    const char *p = str;
    while ((p = strstr(p, old_sub)) != nullptr) { count++; p += ol; }
    size_t new_len = sl + count * (nl - ol);
    char *result = (char *)malloc(new_len + 1);
    char *dst = result;
    p = str;
    while (*p) {
        const char *found = strstr(p, old_sub);
        if (!found) {
            size_t rem = strlen(p);
            memcpy(dst, p, rem);
            dst += rem;
            break;
        }
        size_t seg = (size_t)(found - p);
        memcpy(dst, p, seg);
        dst += seg;
        memcpy(dst, new_sub, nl);
        dst += nl;
        p = found + ol;
    }
    *dst = '\0';
    return result;
}

char **liva_str_split(const char *str, const char *delim, int64_t *count) {
    size_t dl = strlen(delim);
    // Count parts
    int64_t n = 1;
    if (dl > 0) {
        const char *p = str;
        while ((p = strstr(p, delim)) != nullptr) { n++; p += dl; }
    }
    char **parts = (char **)malloc((size_t)n * sizeof(char *));
    const char *p = str;
    int64_t idx = 0;
    if (dl == 0) {
        // Split into individual characters
        size_t sl = strlen(str);
        n = (int64_t)sl;
        parts = (char **)realloc(parts, (size_t)n * sizeof(char *));
        for (size_t i = 0; i < sl; i++) {
            parts[i] = (char *)malloc(2);
            parts[i][0] = str[i];
            parts[i][1] = '\0';
        }
    } else {
        while (idx < n) {
            const char *found = strstr(p, delim);
            if (!found) {
                parts[idx] = (char *)malloc(strlen(p) + 1);
                strcpy(parts[idx], p);
                idx++;
                break;
            }
            size_t seg = (size_t)(found - p);
            parts[idx] = (char *)malloc(seg + 1);
            memcpy(parts[idx], p, seg);
            parts[idx][seg] = '\0';
            idx++;
            p = found + dl;
        }
    }
    *count = n;
    return parts;
}

void liva_str_array_free(char **arr, int64_t count) {
    if (!arr) return;
    for (int64_t i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

// === File I/O ===

void *liva_file_open(const char *path, const char *mode) {
    return (void *)fopen(path, mode);
}

void liva_file_close(void *fp) {
    if (fp) fclose((FILE *)fp);
}

char *liva_file_read_line(void *fp) {
    if (!fp) return nullptr;
    char buf[4096];
    if (!fgets(buf, sizeof(buf), (FILE *)fp)) return nullptr;
    // Strip trailing newline
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }
    if (len > 0 && buf[len - 1] == '\r') {
        buf[len - 1] = '\0';
        len--;
    }
    char *result = (char *)malloc(len + 1);
    memcpy(result, buf, len + 1);
    return result;
}

char *liva_file_read_all(void *fp) {
    if (!fp) return nullptr;
    size_t capacity = 4096;
    size_t length = 0;
    char *buf = (char *)malloc(capacity);
    if (!buf) return nullptr;
    while (1) {
        size_t n = fread(buf + length, 1, capacity - length - 1, (FILE *)fp);
        length += n;
        if (n == 0) break;
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *newBuf = (char *)realloc(buf, capacity);
            if (!newBuf) { free(buf); return nullptr; }
            buf = newBuf;
        }
    }
    buf[length] = '\0';
    return buf;
}

void liva_file_write(void *fp, const char *str) {
    if (fp && str) fputs(str, (FILE *)fp);
}

void liva_file_write_line(void *fp, const char *str) {
    if (fp && str) {
        fputs(str, (FILE *)fp);
        fputc('\n', (FILE *)fp);
    }
}

char *liva_read_line() {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) {
        char *empty = (char *)malloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }
    if (len > 0 && buf[len - 1] == '\r') {
        buf[len - 1] = '\0';
        len--;
    }
    char *result = (char *)malloc(len + 1);
    memcpy(result, buf, len + 1);
    return result;
}

char *liva_i64_to_str(int64_t value) {
    char *buf = (char *)malloc(24);
    snprintf(buf, 24, "%lld", (long long)value);
    return buf;
}

// === I/O ===

void liva_print_i32(int32_t value) {
    printf("%d", value);
}

void liva_print_i64(int64_t value) {
    printf("%lld", (long long)value);
}

void liva_print_f64(double value) {
    printf("%g", value);
}

void liva_print_bool(int8_t value) {
    printf("%s", value ? "true" : "false");
}

void liva_print_str(const char *str) {
    printf("%s", str);
}

void liva_println_str(const char *str) {
    printf("%s\n", str);
}

// === Dynamic Array ===

void *liva_array_new(int64_t elem_size, int64_t capacity) {
    size_t bytes = (size_t)(elem_size * capacity);
    void *ptr = calloc(1, bytes);
    if (!ptr) liva_panic("out of memory");
    return ptr;
}

void liva_array_free(void *data) {
    free(data);
}

void liva_array_push(void **data_ptr, int64_t *len_ptr, int64_t *cap_ptr,
                      const void *elem, int64_t elem_size) {
    if (*len_ptr == *cap_ptr) {
        int64_t new_cap = *cap_ptr < 8 ? 8 : *cap_ptr * 2;
        void *new_data = realloc(*data_ptr, (size_t)(new_cap * elem_size));
        if (!new_data) liva_panic("out of memory");
        *data_ptr = new_data;
        *cap_ptr = new_cap;
    }
    memcpy((char *)(*data_ptr) + (*len_ptr) * elem_size, elem, (size_t)elem_size);
    (*len_ptr)++;
}

void liva_array_pop(int64_t *len_ptr) {
    if (*len_ptr > 0) (*len_ptr)--;
}

int8_t liva_array_contains(void *data, int64_t len, const void *elem,
                            int64_t elem_size, int8_t key_kind) {
    for (int64_t i = 0; i < len; i++) {
        const char *entry = (const char *)data + i * elem_size;
        if (key_kind == 1) {
            // string comparison
            const char *a = *(const char **)entry;
            const char *b = *(const char **)elem;
            if (strcmp(a, b) == 0) return 1;
        } else {
            if (memcmp(entry, elem, (size_t)elem_size) == 0) return 1;
        }
    }
    return 0;
}

int64_t liva_array_index_of(void *data, int64_t len, const void *elem,
                             int64_t elem_size, int8_t key_kind) {
    for (int64_t i = 0; i < len; i++) {
        const char *entry = (const char *)data + i * elem_size;
        if (key_kind == 1) {
            const char *a = *(const char **)entry;
            const char *b = *(const char **)elem;
            if (strcmp(a, b) == 0) return i;
        } else {
            if (memcmp(entry, elem, (size_t)elem_size) == 0) return i;
        }
    }
    return -1;
}

void liva_array_reverse(void *data, int64_t len, int64_t elem_size) {
    if (len <= 1) return;
    char *buf = (char *)malloc((size_t)elem_size);
    if (!buf) liva_panic("out of memory");
    char *arr = (char *)data;
    for (int64_t i = 0; i < len / 2; i++) {
        int64_t j = len - 1 - i;
        memcpy(buf, arr + i * elem_size, (size_t)elem_size);
        memcpy(arr + i * elem_size, arr + j * elem_size, (size_t)elem_size);
        memcpy(arr + j * elem_size, buf, (size_t)elem_size);
    }
    free(buf);
}

// === Hash Table Internals ===

// Entry layout: [1 byte state][8 bytes hash][key_size bytes key][val_size bytes value]
// state: 0=empty, 1=occupied, 2=tombstone
// entry_stride = 9 + key_size + val_size

static uint64_t fnv1a_bytes(const void *data, int64_t size) {
    uint64_t hash = 14695981039346656037ULL;
    const uint8_t *p = (const uint8_t *)data;
    for (int64_t i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t fnv1a_string(const char *str) {
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= (uint8_t)*str;
        hash *= 1099511628211ULL;
        str++;
    }
    return hash;
}

static uint64_t compute_hash(const void *key, int64_t key_size, int8_t key_kind) {
    if (key_kind == 1) {
        // String pointer: key points to a char*
        const char *str = *(const char **)key;
        return fnv1a_string(str);
    }
    return fnv1a_bytes(key, key_size);
}

static int keys_equal(const void *a, const void *b, int64_t key_size, int8_t key_kind) {
    if (key_kind == 1) {
        const char *sa = *(const char **)a;
        const char *sb = *(const char **)b;
        return strcmp(sa, sb) == 0;
    }
    return memcmp(a, b, (size_t)key_size) == 0;
}

static inline int64_t entry_stride(int64_t key_size, int64_t val_size) {
    return 9 + key_size + val_size;  // 1 state + 8 hash + key + val
}

static inline uint8_t *entry_at(void *entries, int64_t idx, int64_t stride) {
    return (uint8_t *)entries + idx * stride;
}

static inline uint8_t entry_state(uint8_t *e) { return e[0]; }
static inline void set_entry_state(uint8_t *e, uint8_t s) { e[0] = s; }
static inline uint64_t entry_hash(uint8_t *e) { uint64_t h; memcpy(&h, e + 1, 8); return h; }
static inline void set_entry_hash(uint8_t *e, uint64_t h) { memcpy(e + 1, &h, 8); }
static inline void *entry_key(uint8_t *e) { return e + 9; }
static inline void *entry_val(uint8_t *e, int64_t key_size) { return e + 9 + key_size; }

static int64_t find_slot(void *entries, int64_t cap, uint64_t hash,
                         const void *key, int64_t key_size, int64_t val_size,
                         int8_t key_kind) {
    int64_t stride = entry_stride(key_size, val_size);
    int64_t idx = (int64_t)(hash % (uint64_t)cap);
    int64_t first_tombstone = -1;

    for (int64_t i = 0; i < cap; i++) {
        uint8_t *e = entry_at(entries, idx, stride);
        uint8_t state = entry_state(e);
        if (state == 0) {
            // Empty — return tombstone slot if we passed one, else this slot
            return first_tombstone >= 0 ? first_tombstone : idx;
        }
        if (state == 2) {
            if (first_tombstone < 0) first_tombstone = idx;
        } else if (state == 1 && entry_hash(e) == hash &&
                   keys_equal(entry_key(e), key, key_size, key_kind)) {
            return idx;  // Found existing key
        }
        idx = (idx + 1) % cap;
    }
    return first_tombstone >= 0 ? first_tombstone : -1;
}

static void map_rehash(void **entries, int64_t *cap,
                       int64_t key_size, int64_t val_size, int8_t key_kind) {
    int64_t old_cap = *cap;
    int64_t stride = entry_stride(key_size, val_size);
    void *old_entries = *entries;

    int64_t new_cap = old_cap * 2;
    void *new_entries = calloc(1, (size_t)(new_cap * stride));
    if (!new_entries) liva_panic("out of memory");

    for (int64_t i = 0; i < old_cap; i++) {
        uint8_t *e = entry_at(old_entries, i, stride);
        if (entry_state(e) == 1) {
            uint64_t h = entry_hash(e);
            int64_t slot = find_slot(new_entries, new_cap, h,
                                     entry_key(e), key_size, val_size, key_kind);
            uint8_t *ne = entry_at(new_entries, slot, stride);
            memcpy(ne, e, (size_t)stride);
        }
    }

    free(old_entries);
    *entries = new_entries;
    *cap = new_cap;
}

// === Hash Map ===

void *liva_map_new(int64_t capacity, int64_t stride) {
    void *entries = calloc(1, (size_t)(capacity * stride));
    if (!entries) liva_panic("out of memory");
    return entries;
}

void liva_map_free(void *entries) {
    free(entries);
}

void liva_map_insert(void **entries, int64_t *size, int64_t *cap,
                     const void *key, const void *value,
                     int64_t key_size, int64_t val_size, int8_t key_kind) {
    // Check load factor > 0.75 → rehash
    if (*size * 4 >= *cap * 3) {
        map_rehash(entries, cap, key_size, val_size, key_kind);
    }

    uint64_t hash = compute_hash(key, key_size, key_kind);
    int64_t slot = find_slot(*entries, *cap, hash, key, key_size, val_size, key_kind);
    if (slot < 0) {
        map_rehash(entries, cap, key_size, val_size, key_kind);
        slot = find_slot(*entries, *cap, hash, key, key_size, val_size, key_kind);
    }

    int64_t stride = entry_stride(key_size, val_size);
    uint8_t *e = entry_at(*entries, slot, stride);
    int was_empty = (entry_state(e) != 1);
    set_entry_state(e, 1);
    set_entry_hash(e, hash);
    memcpy(entry_key(e), key, (size_t)key_size);
    if (val_size > 0)
        memcpy(entry_val(e, key_size), value, (size_t)val_size);
    if (was_empty) (*size)++;
}

void *liva_map_get(void *entries, int64_t cap,
                   const void *key, int64_t key_size, int64_t val_size,
                   int8_t key_kind) {
    uint64_t hash = compute_hash(key, key_size, key_kind);
    int64_t stride = entry_stride(key_size, val_size);
    int64_t idx = (int64_t)(hash % (uint64_t)cap);

    for (int64_t i = 0; i < cap; i++) {
        uint8_t *e = entry_at(entries, idx, stride);
        uint8_t state = entry_state(e);
        if (state == 0) return nullptr;
        if (state == 1 && entry_hash(e) == hash &&
            keys_equal(entry_key(e), key, key_size, key_kind)) {
            return entry_val(e, key_size);
        }
        idx = (idx + 1) % cap;
    }
    return nullptr;
}

int8_t liva_map_remove(void *entries, int64_t *size, int64_t cap,
                       const void *key, int64_t key_size, int64_t val_size,
                       int8_t key_kind) {
    uint64_t hash = compute_hash(key, key_size, key_kind);
    int64_t stride = entry_stride(key_size, val_size);
    int64_t idx = (int64_t)(hash % (uint64_t)cap);

    for (int64_t i = 0; i < cap; i++) {
        uint8_t *e = entry_at(entries, idx, stride);
        uint8_t state = entry_state(e);
        if (state == 0) return 0;
        if (state == 1 && entry_hash(e) == hash &&
            keys_equal(entry_key(e), key, key_size, key_kind)) {
            set_entry_state(e, 2);  // tombstone
            (*size)--;
            return 1;
        }
        idx = (idx + 1) % cap;
    }
    return 0;
}

int8_t liva_map_contains(void *entries, int64_t cap,
                         const void *key, int64_t key_size, int64_t val_size,
                         int8_t key_kind) {
    uint64_t hash = compute_hash(key, key_size, key_kind);
    int64_t stride = entry_stride(key_size, val_size);
    int64_t idx = (int64_t)(hash % (uint64_t)cap);

    for (int64_t i = 0; i < cap; i++) {
        uint8_t *e = entry_at(entries, idx, stride);
        uint8_t state = entry_state(e);
        if (state == 0) return 0;
        if (state == 1 && entry_hash(e) == hash &&
            keys_equal(entry_key(e), key, key_size, key_kind)) {
            return 1;
        }
        idx = (idx + 1) % cap;
    }
    return 0;
}

// === Hash Set ===

void *liva_set_new(int64_t capacity, int64_t stride) {
    return liva_map_new(capacity, stride);
}

void liva_set_free(void *entries) {
    liva_map_free(entries);
}

void liva_set_insert(void **entries, int64_t *size, int64_t *cap,
                     const void *elem, int64_t elem_size, int8_t key_kind) {
    liva_map_insert(entries, size, cap, elem, nullptr, elem_size, 0, key_kind);
}

int8_t liva_set_contains(void *entries, int64_t cap,
                         const void *elem, int64_t elem_size, int8_t key_kind) {
    return liva_map_contains(entries, cap, elem, elem_size, 0, key_kind);
}

int8_t liva_set_remove(void *entries, int64_t *size, int64_t cap,
                       const void *elem, int64_t elem_size, int8_t key_kind) {
    return liva_map_remove(entries, size, cap, elem, elem_size, 0, key_kind);
}

// === Random ===

static bool rand_seeded = false;
static void ensure_rand_seeded() {
    if (!rand_seeded) {
        srand((unsigned)time(nullptr));
        rand_seeded = true;
    }
}

int32_t liva_rand_int(int32_t min, int32_t max) {
    ensure_rand_seeded();
    if (min > max) { int32_t t = min; min = max; max = t; }
    if (min == max) return min;
    return (int32_t)(rand() % (max - min + 1)) + min;
}

double liva_rand_float() {
    ensure_rand_seeded();
    return (double)rand() / (double)RAND_MAX;
}

// === Process/Env ===

char *liva_env_get(const char *name) {
    if (!name) return nullptr;
    const char *val = getenv(name);
    if (!val) return nullptr;
    size_t len = strlen(val);
    char *copy = (char *)malloc(len + 1);
    memcpy(copy, val, len + 1);
    return copy;
}

void liva_exit(int32_t code) {
    exit(code);
}

// Global args storage (initialized from main via liva_init_args)
static int g_argc = 0;
static char **g_argv = nullptr;

void liva_init_args(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
}

char **liva_args(int64_t *count) {
    int argc = g_argc;
    char **argv = g_argv;
    *count = (int64_t)argc;
    char **result = (char **)malloc((size_t)argc * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]);
        result[i] = (char *)malloc(len + 1);
        memcpy(result[i], argv[i], len + 1);
    }
    return result;
}

void liva_args_free(char **args, int64_t count) {
    liva_str_array_free(args, count);
}

// === Date/Time ===

double liva_clock() {
    return (double)time(nullptr);
}

int64_t liva_clock_ms() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // FILETIME is 100-nanosecond intervals since 1601-01-01
    // Convert to ms since Unix epoch (1970-01-01)
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
#endif
}

void liva_sleep(int64_t ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, nullptr);
#endif
}

// === Regex ===

int8_t liva_regex_match(const char *str, const char *pattern) {
    if (!str || !pattern) return 0;
    try {
        std::regex re(pattern);
        return std::regex_match(std::string(str), re) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

char *liva_regex_find(const char *str, const char *pattern) {
    if (!str || !pattern) return nullptr;
    try {
        std::regex re(pattern);
        std::smatch m;
        std::string s(str);
        if (std::regex_search(s, m, re)) {
            std::string match = m[0].str();
            char *result = (char *)malloc(match.size() + 1);
            memcpy(result, match.c_str(), match.size() + 1);
            return result;
        }
    } catch (...) {}
    return nullptr;
}

char **liva_regex_find_all(const char *str, const char *pattern, int64_t *count) {
    *count = 0;
    if (!str || !pattern) return nullptr;
    try {
        std::regex re(pattern);
        std::string s(str);
        std::vector<std::string> matches;
        auto begin = std::sregex_iterator(s.begin(), s.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            matches.push_back((*it)[0].str());
        }
        *count = (int64_t)matches.size();
        char **result = (char **)malloc(matches.size() * sizeof(char *));
        for (size_t i = 0; i < matches.size(); i++) {
            result[i] = (char *)malloc(matches[i].size() + 1);
            memcpy(result[i], matches[i].c_str(), matches[i].size() + 1);
        }
        return result;
    } catch (...) {
        return nullptr;
    }
}

char *liva_regex_replace(const char *str, const char *pattern, const char *replacement) {
    if (!str || !pattern || !replacement) {
        size_t len = str ? strlen(str) : 0;
        char *copy = (char *)malloc(len + 1);
        if (str) memcpy(copy, str, len + 1);
        else copy[0] = '\0';
        return copy;
    }
    try {
        std::regex re(pattern);
        std::string result = std::regex_replace(std::string(str), re, std::string(replacement));
        char *out = (char *)malloc(result.size() + 1);
        memcpy(out, result.c_str(), result.size() + 1);
        return out;
    } catch (...) {
        size_t len = strlen(str);
        char *copy = (char *)malloc(len + 1);
        memcpy(copy, str, len + 1);
        return copy;
    }
}

// === Networking ===

#ifdef _WIN32
// Helper: parse URL into host, path, port, and https flag
static bool parse_url(const char *url, std::wstring &host, std::wstring &path,
                      int &port, bool &https) {
    std::string u(url);
    https = true;
    port = 443;
    size_t proto_end = u.find("://");
    if (proto_end != std::string::npos) {
        std::string proto = u.substr(0, proto_end);
        if (proto == "http") { https = false; port = 80; }
        u = u.substr(proto_end + 3);
    }
    size_t slash = u.find('/');
    std::string h = (slash != std::string::npos) ? u.substr(0, slash) : u;
    std::string p = (slash != std::string::npos) ? u.substr(slash) : "/";
    // Check for port in host
    size_t colon = h.find(':');
    if (colon != std::string::npos) {
        port = atoi(h.substr(colon + 1).c_str());
        h = h.substr(0, colon);
    }
    host.assign(h.begin(), h.end());
    path.assign(p.begin(), p.end());
    return !host.empty();
}

static char *winhttp_request(const char *url, const char *method, const char *body) {
    std::wstring host, path;
    int port;
    bool https;
    if (!parse_url(url, host, path, port, https)) return nullptr;

    HINTERNET hSession = WinHttpOpen(L"Liva/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return nullptr;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return nullptr; }

    std::wstring wmethod(method, method + strlen(method));
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), path.c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return nullptr;
    }

    DWORD bodyLen = body ? (DWORD)strlen(body) : 0;
    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
                                    bodyLen, bodyLen, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return nullptr;
    }

    // Read response
    std::string response;
    DWORD bytesRead = 0;
    char buf[4096];
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        response.append(buf, bytesRead);
        bytesRead = 0;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    char *result = (char *)malloc(response.size() + 1);
    memcpy(result, response.c_str(), response.size() + 1);
    return result;
}
#endif

#if defined(LIVA_HAS_CURL) && !defined(_WIN32)
// libcurl write callback
static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    std::string *buf = static_cast<std::string *>(userp);
    buf->append(static_cast<char *>(contents), total);
    return total;
}

static char *curl_request(const char *url, const char *method, const char *body) {
    CURL *curl = curl_easy_init();
    if (!curl) return nullptr;
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (body && strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return nullptr;
    char *result = (char *)malloc(response.size() + 1);
    memcpy(result, response.c_str(), response.size() + 1);
    return result;
}
#endif

char *liva_http_get(const char *url) {
#ifdef _WIN32
    return winhttp_request(url, "GET", nullptr);
#elif defined(LIVA_HAS_CURL)
    return curl_request(url, "GET", nullptr);
#else
    (void)url;
    return nullptr;
#endif
}

char *liva_http_post(const char *url, const char *body) {
#ifdef _WIN32
    return winhttp_request(url, "POST", body);
#elif defined(LIVA_HAS_CURL)
    return curl_request(url, "POST", body);
#else
    (void)url;
    (void)body;
    return nullptr;
#endif
}

// === Async/Coroutine Runtime ===

// Ready queue for cooperative scheduler (simple FIFO)
static LivaTask *ready_queue[1024];
static int ready_head = 0;
static int ready_tail = 0;

static void ready_push(LivaTask *task) {
    int count = ready_tail - ready_head;
    if (count >= 1024) {
        liva_panic("async task queue overflow (max 1024 concurrent tasks)");
    }
    ready_queue[ready_tail % 1024] = task;
    ready_tail++;
}

static LivaTask *ready_pop() {
    if (ready_head == ready_tail) return nullptr;
    LivaTask *task = ready_queue[ready_head % 1024];
    ready_head++;
    return task;
}

LivaTask *liva_task_create(void *coro_handle) {
    LivaTask *task = (LivaTask *)malloc(sizeof(LivaTask));
    task->handle = coro_handle;
    task->parent = nullptr;
    task->done = 0;
    return task;
}

void liva_task_complete(LivaTask *task) {
    task->done = 1;
    if (task->parent) {
        ready_push(task->parent);
    }
}

int8_t liva_task_is_done(LivaTask *task) {
    return task->done;
}

void *liva_task_get_handle(LivaTask *task) {
    return task->handle;
}

void liva_task_set_parent(LivaTask *child, LivaTask *parent) {
    child->parent = parent;
}

void liva_task_destroy(LivaTask *task) {
    free(task);
}

void liva_coro_resume(void *handle) {
    // Coroutine frame layout: [resume_fn_ptr, destroy_fn_ptr, ...]
    // resume_fn_ptr is at offset 0
    void (*resume_fn)(void *) = *((void (**)(void *))handle);
    resume_fn(handle);
}

void liva_coro_destroy(void *handle) {
    // destroy_fn_ptr is at offset 1 (sizeof(void*))
    void (**fn_ptrs)(void *) = (void (**)(void *))handle;
    void (*destroy_fn)(void *) = fn_ptrs[1];
    destroy_fn(handle);
}

void liva_scheduler_run(LivaTask *root) {
    // Reset queue
    ready_head = 0;
    ready_tail = 0;

    // Resume root coroutine
    if (!root->done) {
        liva_coro_resume(root->handle);
    }

    // Process ready queue until root is done
    while (!root->done) {
        LivaTask *task = ready_pop();
        if (!task) break;  // No more tasks — deadlock prevention
        if (!task->done) {
            liva_coro_resume(task->handle);
        }
    }
}

// === Panic ===

void liva_panic(const char *message) {
    fprintf(stderr, "PANIC: %s\n", message);
    abort();
}

void liva_use_after_move(const char *var_name) {
    fprintf(stderr, "PANIC: use of moved value '%s'\n", var_name);
    abort();
}

} // extern "C"
