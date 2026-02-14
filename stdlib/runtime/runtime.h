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

/// Get length of a C string (UTF-8 code point count)
int64_t liva_str_length(const char *a);

/// Get byte length of a C string
int64_t liva_str_byte_length(const char *a);

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

/// Seek within a file. whence: 0=SET, 1=CUR, 2=END. Returns 0 on success.
int32_t liva_file_seek(void *fp, int64_t offset, int32_t whence);

/// Get current position in file. Returns -1 on error.
int64_t liva_file_tell(void *fp);

/// Get size of file in bytes. Returns -1 on error.
int64_t liva_file_size(void *fp);

/// Read a line from stdin, returns malloc'd string
char *liva_read_line();

/// Convert i64 to string, returns malloc'd result
char *liva_i64_to_str(int64_t value);

// === Directory Operations ===

/// List directory contents, returns malloc'd array of malloc'd strings, sets count
char **liva_dir_list(const char *path, int64_t *count);

/// Create directory (and parents). Returns 1 on success, 0 on failure.
int8_t liva_dir_create(const char *path);

/// Remove directory recursively. Returns 1 on success, 0 on failure.
int8_t liva_dir_remove(const char *path);

/// Check if directory exists. Returns 1 if exists and is a directory, 0 otherwise.
int8_t liva_dir_exists(const char *path);

// === Path Operations ===

/// Join two path components, returns malloc'd result
char *liva_path_join(const char *a, const char *b);

/// Get parent directory of path, returns malloc'd result
char *liva_path_dirname(const char *path);

/// Get filename component of path, returns malloc'd result
char *liva_path_basename(const char *path);

/// Get file extension (including dot), returns malloc'd result
char *liva_path_extension(const char *path);

/// Check if a path exists (file or directory). Returns 1 if exists, 0 otherwise.
int8_t liva_path_exists(const char *path);

/// Check if path is a regular file. Returns 1 if file, 0 otherwise.
int8_t liva_file_is_file(const char *path);

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

// === Subprocess ===

/// Execute command synchronously, return exit code
int32_t liva_exec(const char *command);

/// Execute command and capture stdout, returns malloc'd string or NULL
char *liva_exec_output(const char *command);

/// Start async process, returns opaque handle as i64 (0 on failure)
int64_t liva_process_start(const char *command);

/// Wait for process to finish, returns exit code (-1 on error)
int32_t liva_process_wait(int64_t handle);

/// Kill a running process, returns 1 on success, 0 on failure
int8_t liva_process_kill(int64_t handle);

/// Read stdout from process, returns malloc'd string or NULL
char *liva_process_read(int64_t handle);

/// Close process handle and clean up resources
void liva_process_close(int64_t handle);

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

/// Replace all matches of pattern with replacement ($1,$2 supported), returns malloc'd
char *liva_regex_replace(const char *str, const char *pattern, const char *replacement);

/// Find first match and return capture groups (group 0 = full match)
/// Returns malloc'd array of malloc'd strings, sets count
char **liva_regex_find_groups(const char *str, const char *pattern, int64_t *count);

/// Compile a regex pattern. Returns handle (cast ptr) or 0 on error
int64_t liva_regex_compile(const char *pattern);

/// Test if string matches compiled regex
int8_t liva_regex_test(int64_t handle, const char *str);

/// Find first match using compiled regex (returns malloc'd or NULL)
char *liva_regex_exec(int64_t handle, const char *str);

/// Find first match groups using compiled regex
/// Returns malloc'd array of malloc'd strings, sets count
char **liva_regex_exec_groups(int64_t handle, const char *str, int64_t *count);

/// Replace using compiled regex ($1,$2 supported), returns malloc'd
char *liva_regex_replace_compiled(int64_t handle, const char *str, const char *replacement);

/// Free a compiled regex handle
void liva_regex_free(int64_t handle);

// === Networking ===

/// HTTP response with status code, body, and headers
typedef struct LivaHttpResponse {
    int32_t status_code;      // HTTP status code (200, 404, etc.)
    char *body;               // Response body (malloc'd, caller frees)
    char **header_names;      // Header names array (malloc'd)
    char **header_values;     // Header values array (malloc'd)
    int64_t header_count;     // Number of headers
} LivaHttpResponse;

/// HTTP GET request, returns malloc'd response body or NULL on failure
char *liva_http_get(const char *url);

/// HTTP POST request, returns malloc'd response body or NULL on failure
char *liva_http_post(const char *url, const char *body);

/// HTTP PUT request, returns malloc'd response body or NULL on failure
char *liva_http_put(const char *url, const char *body);

/// HTTP PATCH request, returns malloc'd response body or NULL on failure
char *liva_http_patch(const char *url, const char *body);

/// HTTP DELETE request, returns malloc'd response body or NULL on failure
char *liva_http_delete(const char *url);

/// Full HTTP request with method, headers, timeout, and structured response.
/// method: "GET", "POST", "PUT", "PATCH", "DELETE"
/// headers: alternating name/value strings (name1, val1, name2, val2, ...)
/// header_count: number of header PAIRS (total strings = header_count * 2)
/// timeout_ms: request timeout in milliseconds (0 = no timeout)
/// Returns heap-allocated LivaHttpResponse* or NULL on failure.
LivaHttpResponse *liva_http_request(const char *method, const char *url,
                                     const char *body,
                                     const char **headers, int64_t header_count,
                                     int64_t timeout_ms);

/// Free an HTTP response returned by liva_http_request
void liva_http_response_free(LivaHttpResponse *resp);

/// Get status code from response (0 if NULL)
int32_t liva_http_response_status(const LivaHttpResponse *resp);

/// Get body from response (NULL if NULL)
char *liva_http_response_body(const LivaHttpResponse *resp);

/// Get header value by name from response (returns malloc'd copy or NULL)
char *liva_http_response_header(const LivaHttpResponse *resp, const char *name);

// === Async/Coroutine Runtime ===

typedef struct LivaTask {
    void *handle;            // Coroutine frame pointer
    struct LivaTask *parent; // Parent task waiting on us (or NULL)
    int8_t done;             // 1 = completed
    int8_t cancelled;        // 1 = cancelled (cooperative)
} LivaTask;

LivaTask *liva_task_create(void *coro_handle);
void liva_task_complete(LivaTask *task);
int8_t liva_task_is_done(LivaTask *task);
void *liva_task_get_handle(LivaTask *task);
void liva_task_set_parent(LivaTask *child, LivaTask *parent);
void liva_task_destroy(LivaTask *task);
void liva_task_cancel(LivaTask *task);
int8_t liva_task_is_cancelled(LivaTask *task);
void liva_coro_resume(void *handle);
void liva_coro_destroy(void *handle);
void liva_scheduler_run(LivaTask *root);

/// Async sleep: register timer and suspend coroutine
void liva_async_sleep(LivaTask *task, int64_t ms);

// === JSON ===

/// Get string value by key from JSON object, returns malloc'd string or NULL
char *liva_json_get(const char *json, const char *key);

/// Get integer value by key from JSON object (0 if not found)
int64_t liva_json_get_int(const char *json, const char *key);

/// Get float value by key from JSON object (0.0 if not found)
double liva_json_get_float(const char *json, const char *key);

/// Get bool value by key from JSON object (false if not found)
int8_t liva_json_get_bool(const char *json, const char *key);

/// Check if string is valid JSON
int8_t liva_json_is_valid(const char *json);

/// Get all keys from JSON object, returns malloc'd array of malloc'd strings
char **liva_json_keys(const char *json, int64_t *count);

// === Logging ===

/// Log messages at different levels
void liva_log_debug(const char *msg);
void liva_log_info(const char *msg);
void liva_log_warn(const char *msg);
void liva_log_error(const char *msg);

/// Set minimum log level: 0=debug, 1=info, 2=warn, 3=error
void liva_log_set_level(int32_t level);

// === Testing ===

/// Assert condition is true, panic if false
void liva_assert(int8_t condition);

/// Assert condition with custom message
void liva_assert_msg(int8_t condition, const char *msg);

/// Assert two i64 values are equal
void liva_assert_eq(int64_t a, int64_t b);

/// Assert two strings are equal
void liva_assert_eq_str(const char *a, const char *b);

/// Assert two f64 values are approximately equal (epsilon = 1e-9)
void liva_assert_eq_float(double a, double b);

// === DateTime ===

/// Current date as "YYYY-MM-DD", returns malloc'd string
char *liva_date_now();

/// Current time as "HH:MM:SS", returns malloc'd string
char *liva_time_now();

/// Current datetime as "YYYY-MM-DD HH:MM:SS", returns malloc'd string
char *liva_datetime_now();

/// Format timestamp (seconds since epoch) with strftime format, returns malloc'd
char *liva_date_format(double timestamp, const char *fmt);

/// Extract year from timestamp
int32_t liva_date_year(double timestamp);

/// Extract month (1-12) from timestamp
int32_t liva_date_month(double timestamp);

/// Extract day (1-31) from timestamp
int32_t liva_date_day(double timestamp);

/// Extract day of week (0=Sunday, 6=Saturday) from timestamp
int32_t liva_date_weekday(double timestamp);

// === Encoding / Compression ===

/// Base64 encode, returns malloc'd string
char *liva_base64_encode(const char *data);

/// Base64 decode, returns malloc'd string or NULL on invalid input
char *liva_base64_decode(const char *data);

/// Hex encode (bytes to hex string), returns malloc'd string
char *liva_hex_encode(const char *data);

/// Hex decode (hex string to bytes), returns malloc'd string or NULL
char *liva_hex_decode(const char *data);

/// CRC-32 checksum
int64_t liva_crc32(const char *data);

// === Synchronization: Mutex ===

/// Create a mutex, returns handle (0 on error)
int64_t liva_mutex_create();

/// Lock a mutex (blocks until acquired)
void liva_mutex_lock(int64_t handle);

/// Unlock a mutex
void liva_mutex_unlock(int64_t handle);

/// Try to lock without blocking, returns 1 if acquired
int8_t liva_mutex_try_lock(int64_t handle);

/// Free a mutex
void liva_mutex_free(int64_t handle);

// === Synchronization: Atomic i64 ===

/// Create an atomic i64 with initial value, returns handle
int64_t liva_atomic_create(int64_t initial);

/// Load value atomically
int64_t liva_atomic_load(int64_t handle);

/// Store value atomically
void liva_atomic_store(int64_t handle, int64_t value);

/// Atomically add and return previous value
int64_t liva_atomic_add(int64_t handle, int64_t value);

/// Atomically subtract and return previous value
int64_t liva_atomic_sub(int64_t handle, int64_t value);

/// Compare-and-swap: if current == expected, set to desired. Returns 1 on success
int8_t liva_atomic_cas(int64_t handle, int64_t expected, int64_t desired);

/// Free an atomic handle
void liva_atomic_free(int64_t handle);

// === String Utility Functions (std::string) ===

/// Repeat string n times, returns malloc'd result
char *liva_str_repeat(const char *s, int64_t n);

/// Pad string to width with fill character on the left, returns malloc'd result
char *liva_str_pad_left(const char *s, int64_t width, const char *fill);

/// Pad string to width with fill character on the right, returns malloc'd result
char *liva_str_pad_right(const char *s, int64_t width, const char *fill);

/// Join array of strings with separator, returns malloc'd result
char *liva_str_join(const char **strings, int64_t count, const char *sep);

/// Trim leading whitespace, returns malloc'd result
char *liva_str_trim_left(const char *s);

/// Trim trailing whitespace, returns malloc'd result
char *liva_str_trim_right(const char *s);

/// Reverse string (UTF-8 aware), returns malloc'd result
char *liva_str_reverse(const char *s);

/// Split string into individual UTF-8 characters, returns malloc'd array
char **liva_str_chars(const char *s, int64_t *count);

/// Split string into lines by '\n', returns malloc'd array
char **liva_str_lines(const char *s, int64_t *count);

// === Collection Utility Functions (std::collections) ===

/// Reverse a copy of array, returns malloc'd data
void liva_array_reversed(const void *data, int64_t len, int64_t elem_size,
                          void **out_data, int64_t *out_len, int64_t *out_cap);

/// Sort a copy of array using comparison callback, returns malloc'd data
void liva_array_sorted(const void *data, int64_t len, int64_t elem_size,
                        int (*cmp)(const void *, const void *),
                        void **out_data, int64_t *out_len, int64_t *out_cap);

/// Check if any element matches predicate
int8_t liva_array_any(const void *data, int64_t len, int64_t elem_size,
                       int8_t (*pred)(const void *));

/// Check if all elements match predicate
int8_t liva_array_all(const void *data, int64_t len, int64_t elem_size,
                       int8_t (*pred)(const void *));

/// Count elements matching predicate
int64_t liva_array_count(const void *data, int64_t len, int64_t elem_size,
                          int8_t (*pred)(const void *));

// === Panic ===

/// Called when an unrecoverable error occurs
[[noreturn]] void liva_panic(const char *message);

/// Called on use-after-move (debug builds)
[[noreturn]] void liva_use_after_move(const char *var_name);

} // extern "C"
