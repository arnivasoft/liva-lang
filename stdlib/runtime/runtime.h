#pragma once

#include <cstdint>
#include <cstddef>

// Liva Runtime Support Library
// This provides C-level functions that the Liva compiler can call

extern "C" {

// === Memory Management ===

/// Allocate heap memory
void *liva_alloc(size_t size);

/// Free heap memory
void liva_free(void *ptr);

/// Allocate and zero-initialize
void *liva_alloc_zeroed(size_t size);

// === String Operations ===

/// Create a new string from C string
struct LivaString {
    char *data;
    size_t length;
    size_t capacity;
};

LivaString liva_string_new(const char *str);
LivaString liva_string_from_parts(const char *data, size_t len);
void liva_string_free(LivaString *str);
LivaString liva_string_concat(const LivaString *a, const LivaString *b);
int32_t liva_string_compare(const LivaString *a, const LivaString *b);

// === Simple char*-based string operations (compatible with i8* IR) ===

/// Concatenate two C strings, returns malloc'd result
char *liva_str_concat(const char *a, const char *b);

/// Compare two C strings for equality (returns 1 if equal, 0 otherwise)
int32_t liva_str_equal(const char *a, const char *b);

/// Get length of a C string
int64_t liva_str_length(const char *a);

/// Convert i32 to string, returns malloc'd result
char *liva_i32_to_str(int32_t value);

/// Convert f64 to string, returns malloc'd result
char *liva_f64_to_str(double value);

/// Convert bool to string, returns malloc'd result
char *liva_bool_to_str(int8_t value);

// === I/O ===

/// Print functions
void liva_print_i32(int32_t value);
void liva_print_i64(int64_t value);
void liva_print_f64(double value);
void liva_print_bool(int8_t value);
void liva_print_str(const char *str);
void liva_println_str(const char *str);

// === Panic ===

/// Called when an unrecoverable error occurs
[[noreturn]] void liva_panic(const char *message);

/// Called on use-after-move (debug builds)
[[noreturn]] void liva_use_after_move(const char *var_name);

} // extern "C"
