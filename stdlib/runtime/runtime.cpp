#include "runtime.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
