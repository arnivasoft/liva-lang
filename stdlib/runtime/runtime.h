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

// === Type Conversion (String → Number) ===

/// Parse string to i32, returns 1 on success, 0 on failure
int8_t liva_str_parse_i32(const char *str, int32_t *result);

/// Parse string to i64, returns 1 on success, 0 on failure
int8_t liva_str_parse_i64(const char *str, int64_t *result);

/// Parse string to f64, returns 1 on success, 0 on failure
int8_t liva_str_parse_f64(const char *str, double *result);

// === String Methods ===

/// Check if string contains substring
int8_t liva_str_contains(const char *str, const char *substr);

/// Check if string starts with prefix
int8_t liva_str_starts_with(const char *str, const char *prefix);

/// Check if string ends with suffix
int8_t liva_str_ends_with(const char *str, const char *suffix);

/// Find index of substring (-1 if not found)
int64_t liva_str_index_of(const char *str, const char *substr);

/// Extract substring (start index, length), returns malloc'd result
char *liva_str_substring(const char *str, int64_t start, int64_t length);

/// Trim whitespace from both ends, returns malloc'd result
char *liva_str_trim(const char *str);

/// Convert to upper case, returns malloc'd result
char *liva_str_to_upper(const char *str);

/// Convert to lower case, returns malloc'd result
char *liva_str_to_lower(const char *str);

/// Replace all occurrences of old with new, returns malloc'd result
char *liva_str_replace(const char *str, const char *old_sub, const char *new_sub);

/// Split string by delimiter, returns malloc'd array of malloc'd strings
/// count is set to the number of parts
char **liva_str_split(const char *str, const char *delim, int64_t *count);

/// Free a string array returned by liva_str_split or liva_regex_find_all
void liva_str_array_free(char **arr, int64_t count);

// === File I/O ===

/// Open a file, returns FILE* or NULL on failure
void *liva_file_open(const char *path, const char *mode);

/// Close a file handle
void liva_file_close(void *fp);

/// Read a line from file, returns malloc'd string or NULL on EOF
char *liva_file_read_line(void *fp);

/// Read entire file contents, returns malloc'd string
char *liva_file_read_all(void *fp);

/// Write a string to file
void liva_file_write(void *fp, const char *str);

/// Write a string + newline to file
void liva_file_write_line(void *fp, const char *str);

/// Read a line from stdin, returns malloc'd string
char *liva_read_line();

/// Convert i64 to string, returns malloc'd result
char *liva_i64_to_str(int64_t value);

// === I/O ===

/// Print functions
void liva_print_i32(int32_t value);
void liva_print_i64(int64_t value);
void liva_print_f64(double value);
void liva_print_bool(int8_t value);
void liva_print_str(const char *str);
void liva_println_str(const char *str);

// === Dynamic Array ===

/// Allocate heap array for given element size and capacity
void *liva_array_new(int64_t elem_size, int64_t capacity);

/// Free heap array data
void liva_array_free(void *data);

/// Push element to dynamic array (may realloc)
void liva_array_push(void **data_ptr, int64_t *len_ptr, int64_t *cap_ptr,
                      const void *elem, int64_t elem_size);

/// Pop last element from dynamic array
void liva_array_pop(int64_t *len_ptr);

/// Check if array contains element (returns 1 if found, 0 otherwise)
/// key_kind: 0=memcmp (numeric), 1=strcmp (string)
int8_t liva_array_contains(void *data, int64_t len, const void *elem,
                            int64_t elem_size, int8_t key_kind);

/// Find index of element in array (-1 if not found)
int64_t liva_array_index_of(void *data, int64_t len, const void *elem,
                             int64_t elem_size, int8_t key_kind);

/// Reverse array in-place
void liva_array_reverse(void *data, int64_t len, int64_t elem_size);

// === Hash Map ===

/// Allocate hash map entry buffer (zero-initialized)
void *liva_map_new(int64_t capacity, int64_t entry_stride);

/// Free hash map entry buffer
void liva_map_free(void *entries);

/// Insert key-value pair into map (may resize)
void liva_map_insert(void **entries, int64_t *size, int64_t *cap,
                     const void *key, const void *value,
                     int64_t key_size, int64_t val_size, int8_t key_kind);

/// Get value pointer for key (returns NULL if not found)
void *liva_map_get(void *entries, int64_t cap,
                   const void *key, int64_t key_size, int64_t val_size,
                   int8_t key_kind);

/// Remove key from map (returns 1 if found, 0 otherwise)
int8_t liva_map_remove(void *entries, int64_t *size, int64_t cap,
                       const void *key, int64_t key_size, int64_t val_size,
                       int8_t key_kind);

/// Check if key exists in map
int8_t liva_map_contains(void *entries, int64_t cap,
                         const void *key, int64_t key_size, int64_t val_size,
                         int8_t key_kind);

// === Hash Set ===

/// Allocate hash set entry buffer (zero-initialized)
void *liva_set_new(int64_t capacity, int64_t entry_stride);

/// Free hash set entry buffer
void liva_set_free(void *entries);

/// Insert element into set (may resize)
void liva_set_insert(void **entries, int64_t *size, int64_t *cap,
                     const void *elem, int64_t elem_size, int8_t key_kind);

/// Check if element exists in set
int8_t liva_set_contains(void *entries, int64_t cap,
                         const void *elem, int64_t elem_size, int8_t key_kind);

/// Remove element from set (returns 1 if found, 0 otherwise)
int8_t liva_set_remove(void *entries, int64_t *size, int64_t cap,
                       const void *elem, int64_t elem_size, int8_t key_kind);

// === Random ===

/// Random integer in [min, max] range
int32_t liva_rand_int(int32_t min, int32_t max);

/// Random float in [0.0, 1.0)
double liva_rand_float();

// === Process/Env ===

/// Initialize command line arguments (called from main)
void liva_init_args(int argc, char **argv);

/// Get environment variable (returns malloc'd copy or NULL)
char *liva_env_get(const char *name);

/// Exit process with code
[[noreturn]] void liva_exit(int32_t code);

/// Get command line arguments, sets count
char **liva_args(int64_t *count);

/// Free command line arguments returned by liva_args
void liva_args_free(char **args, int64_t count);

// === Date/Time ===

/// Current time as seconds since epoch (f64)
double liva_clock();

/// Current time in milliseconds
int64_t liva_clock_ms();

/// Sleep for given milliseconds
void liva_sleep(int64_t ms);

// === Regex ===

/// Check if entire string matches pattern
int8_t liva_regex_match(const char *str, const char *pattern);

/// Find first match of pattern in string (returns malloc'd or NULL)
char *liva_regex_find(const char *str, const char *pattern);

/// Find all matches, sets count, returns malloc'd array of malloc'd strings
char **liva_regex_find_all(const char *str, const char *pattern, int64_t *count);

/// Replace all matches of pattern with replacement, returns malloc'd
char *liva_regex_replace(const char *str, const char *pattern, const char *replacement);

// === Networking ===

/// HTTP GET request, returns malloc'd response body or NULL on failure
char *liva_http_get(const char *url);

/// HTTP POST request, returns malloc'd response body or NULL on failure
char *liva_http_post(const char *url, const char *body);

// === Async/Coroutine Runtime ===

typedef struct LivaTask {
    void *handle;            // Coroutine frame pointer
    struct LivaTask *parent; // Parent task waiting on us (or NULL)
    int8_t done;             // 1 = completed
} LivaTask;

LivaTask *liva_task_create(void *coro_handle);
void liva_task_complete(LivaTask *task);
int8_t liva_task_is_done(LivaTask *task);
void *liva_task_get_handle(LivaTask *task);
void liva_task_set_parent(LivaTask *child, LivaTask *parent);
void liva_task_destroy(LivaTask *task);
void liva_coro_resume(void *handle);
void liva_coro_destroy(void *handle);
void liva_scheduler_run(LivaTask *root);

// === Panic ===

/// Called when an unrecoverable error occurs
[[noreturn]] void liva_panic(const char *message);

/// Called on use-after-move (debug builds)
[[noreturn]] void liva_use_after_move(const char *var_name);

} // extern "C"
