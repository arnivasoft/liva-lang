#include "runtime.h"
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
