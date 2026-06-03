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

/// Duplicate a C string, returns malloc'd copy
char *liva_str_dup(const char *s);

/// Concatenate two C strings, returns malloc'd result
char *liva_str_concat(const char *a, const char *b);

/// Compare two C strings for equality (returns 1 if equal, 0 otherwise)
int32_t liva_str_equal(const char *a, const char *b);

/// Lexicographic compare: <0 if a<b, 0 if equal, >0 if a>b
int32_t liva_str_compare(const char *a, const char *b);

/// Get length of a C string (UTF-8 code point count)
int64_t liva_str_length(const char *a);

/// Get byte length of a C string
int64_t liva_str_byte_length(const char *a);

/// Convert i32 to string, returns malloc'd result
char *liva_i32_to_str(int32_t value);

/// Convert Unicode code point to 1-char string, returns malloc'd result
char *liva_char_to_str(int32_t codepoint);

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

/// Returns 1 if path is an existing directory, else 0
int8_t liva_path_is_dir(const char *path);

/// Returns file size in bytes, or -1 on error
int64_t liva_path_size(const char *path);

/// Returns file modification time as unix timestamp, or -1 on error
int64_t liva_path_modified_time(const char *path);

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

/// Seed the random generator
void liva_rand_seed(int64_t seed);

/// Random i64 composed from two 32-bit rand() calls
int64_t liva_rand_i64();

/// Generate a UUID v4 string (caller frees)
char *liva_rand_uuid();

/// Generate a UUID v7 string (RFC 9562) — time-ordered, 48-bit ms
/// timestamp prefix + 74 random bits. Caller frees.
char *liva_rand_uuid_v7();

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

/// Split string by regex pattern (caller frees each element and array)
char **liva_regex_split(const char *str, const char *pattern, int64_t *count);

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

// === Handle-based HTTP API (i64 handle exposed to Liva) ===

/// Perform full HTTP request (method, url, body, timeout_ms).
/// Returns opaque i64 handle (0 on failure); caller must free via liva_http_req_close.
int64_t liva_http_req(const char *method, const char *url,
                      const char *body, int64_t timeout_ms);

/// Returns HTTP status code (e.g. 200, 404) or 0 for invalid handle.
int32_t liva_http_req_status(int64_t handle);

/// Returns response body as fresh malloc'd string (may be empty, never nullptr for valid handle).
char *liva_http_req_body(int64_t handle);

/// Returns header value or nullptr if not found (case-insensitive name match).
char *liva_http_req_header(int64_t handle, const char *name);

/// Release the response buffer.
void liva_http_req_close(int64_t handle);

// === WebSocket (WinHTTP-backed on Windows; returns 0 on platforms without support) ===

/// Open a WebSocket. URL must be ws:// or wss://. Returns opaque handle (0 on failure).
int64_t liva_ws_connect(const char *url);

/// Send UTF-8 text frame. Returns 0 on success, nonzero on failure.
int32_t liva_ws_send_text(int64_t handle, const char *msg);

/// Receive next message (full reassembly across fragments).
/// Returns malloc'd string (caller frees) or nullptr on close/error.
char *liva_ws_recv_text(int64_t handle);

/// Close handshake + free underlying handles. Status is the close code (e.g. 1000).
int32_t liva_ws_close(int64_t handle, int32_t status, const char *reason);

/// Returns 1 if the socket is still open, 0 otherwise.
int32_t liva_ws_is_open(int64_t handle);

// === SQLite (Windows: dynamic-loaded winsqlite3.dll; POSIX: link libsqlite3 when found) ===
//
// Cross-platform support layer:
//   - Windows: runtime resolves winsqlite3.dll dynamically (no link dep).
//   - Linux/macOS: CMake's find_package(SQLite3) wires libsqlite3 in
//     and defines LIVA_HAS_POSIX_SQLITE; symbols bind directly.
//   - Neither: every entry point fails closed (returns 0/null/false).

/// Open or create a database. Path can be ":memory:" for an in-memory DB.
/// Returns opaque i64 handle (0 on failure).
int64_t liva_sqlite_open(const char *path);

/// Release the database connection.
void liva_sqlite_close(int64_t handle);

/// Execute SQL with no result rows (CREATE/INSERT/UPDATE/DELETE/PRAGMA).
/// Returns 0 on success, nonzero on error.
int32_t liva_sqlite_exec(int64_t handle, const char *sql);

/// Run a query and return the first column of the first row as a fresh
/// malloc'd string. Returns nullptr if the query produced no rows or failed.
char *liva_sqlite_query_first(int64_t handle, const char *sql);

/// Run a query and return the first column of the first row as i64.
/// *ok is set to 1 if a row was produced, 0 otherwise.
int64_t liva_sqlite_query_int(int64_t handle, const char *sql, int32_t *ok);

/// Run a query and return the first column of every row, joined by '\n'.
/// Returns a fresh malloc'd string (empty if no rows).
char *liva_sqlite_query_all_first_col(int64_t handle, const char *sql);

/// Last INSERT'd rowid on this connection (0 if none).
int64_t liva_sqlite_last_insert_rowid(int64_t handle);

/// Number of rows changed by the most recent exec on this connection.
int32_t liva_sqlite_changes(int64_t handle);

/// Last error message on this connection (caller frees).
char *liva_sqlite_errmsg(int64_t handle);

// --- Prepared statements ---

/// Compile SQL into a statement (1-indexed bind params: ?, ?1, :name).
/// Returns opaque i64 handle (0 on failure).
int64_t liva_sqlite_prepare(int64_t db, const char *sql);

/// Bind a value to parameter `idx` (1-based). Returns 0 on success.
int32_t liva_sqlite_bind_text(int64_t stmt, int32_t idx, const char *val);
int32_t liva_sqlite_bind_int(int64_t stmt, int32_t idx, int64_t val);
int32_t liva_sqlite_bind_double(int64_t stmt, int32_t idx, double val);
int32_t liva_sqlite_bind_null(int64_t stmt, int32_t idx);

/// Advance the statement: 1 = row available, 2 = done, 0 = error.
int32_t liva_sqlite_step(int64_t stmt);

/// Rewind so the same statement can be re-stepped (re-binds keep their values
/// unless explicitly rebound). Returns 0 on success.
int32_t liva_sqlite_reset(int64_t stmt);

/// Number of result columns in the current row.
int32_t liva_sqlite_column_count(int64_t stmt);

/// Read a column from the current row. col is 0-based. Caller frees text.
char *liva_sqlite_column_text(int64_t stmt, int32_t col);
int64_t liva_sqlite_column_int(int64_t stmt, int32_t col);
double  liva_sqlite_column_double(int64_t stmt, int32_t col);
/// Name of result column `col` (0-based). Caller frees.
char *liva_sqlite_column_name(int64_t stmt, int32_t col);

/// SQLite type code of column `col` in the current row: 1=INTEGER, 2=FLOAT,
/// 3=TEXT, 4=BLOB, 5=NULL. 0 if unavailable.
int32_t liva_sqlite_column_type(int64_t stmt, int32_t col);

/// Bind text `val` to the named parameter `name` (e.g. ":id"). Returns 0 on
/// success, -1 if the name is unknown or the bind fails.
int32_t liva_sqlite_bind_by_name(int64_t stmt, const char *name, const char *val);

/// Bind `len` raw bytes from `data` to parameter `idx` (1-based, BLOB).
/// Returns 0 on success.
int32_t liva_sqlite_bind_blob(int64_t stmt, int32_t idx, const void *data, int64_t len);

/// Read column `col` (0-based) as a blob: returns a fresh malloc'd byte buffer
/// and writes the length to *out_len. nullptr/0 if empty. Caller frees.
void *liva_sqlite_column_blob(int64_t stmt, int32_t col, int64_t *out_len);

/// Release the statement.
void liva_sqlite_finalize(int64_t stmt);

// === PostgreSQL (dynamic-loaded libpq; fail-closed when absent) ===
//
// libpq resolved at runtime (PATH, then standard install dirs newest-first,
// then $LIVA_LIBPQ_PATH). Missing libpq/symbol => every entry fails closed.

/// Open a connection from a conninfo string ("host=... dbname=... user=...").
/// Returns opaque i64 handle (0 on failure).
int64_t liva_pg_connect(const char *conninfo);

/// Close the connection.
void liva_pg_close(int64_t handle);

/// Run a no-result command. Returns 0 on success, nonzero on error.
int32_t liva_pg_exec(int64_t handle, const char *sql);

/// Last error message on the connection (caller frees).
char *liva_pg_errmsg(int64_t handle);

/// Rewrite '?' placeholders to '$1','$2',... (PostgreSQL style), skipping '?'
/// inside single-quoted string literals ('' escape), line comments (-- ...),
/// block comments (/* ... */), and dollar-quoted strings ($$...$$, $tag$...$tag$).
/// A '$' followed by a digit (e.g. $1) is left as-is. Caller frees. libpq-independent.
char *liva_pg_normalize_params(const char *sql);

/// Run a query; returns opaque PGresult* i64 handle (0 on failure). Caller must
/// liva_pg_clear it. Only row-returning (TUPLES_OK) results are returned.
int64_t liva_pg_query(int64_t handle, const char *sql);

/// Release a result handle.
void liva_pg_clear(int64_t result);

/// Row count of a result.
int32_t liva_pg_ntuples(int64_t result);

/// Column count of a result.
int32_t liva_pg_nfields(int64_t result);

/// Cell value as text (caller frees). Empty string if NULL/unavailable.
char *liva_pg_getvalue(int64_t result, int32_t row, int32_t col);

/// 1 if the cell is NULL, else 0.
int32_t liva_pg_getisnull(int64_t result, int32_t row, int32_t col);

/// Column name (caller frees).
char *liva_pg_fname(int64_t result, int32_t col);

/// Parameterized query. `values` holds `nparams` text C-strings (NULL = SQL
/// NULL). Returns opaque PGresult* i64 (0 on failure). Caller frees via clear.
int64_t liva_pg_query_params(int64_t handle, const char *sql,
                             const char *const *values, int64_t nparams);

// === Async/Coroutine Runtime ===

typedef struct LivaTask {
    void *handle;            // Coroutine frame pointer
    struct LivaTask *parent; // Parent task waiting on us (or NULL)
    int8_t done;             // 1 = completed
    int8_t cancelled;        // 1 = cancelled (cooperative)
    struct LivaTask **children; // Child tasks for cancellation propagation
    int64_t child_count;        // Number of children
    int64_t child_capacity;     // Allocated capacity for children array
    int32_t worker_id;          // Which worker thread (-1 = unassigned)
} LivaTask;

LivaTask *liva_task_create(void *coro_handle);
void liva_task_complete(LivaTask *task);
int8_t liva_task_is_done(LivaTask *task);
void *liva_task_get_handle(LivaTask *task);
void liva_task_set_parent(LivaTask *child, LivaTask *parent);
void liva_task_destroy(LivaTask *task);
void liva_task_cancel(LivaTask *task);
int8_t liva_task_is_cancelled(LivaTask *task);

/// Select: wait for first completed task, returns index (0-based)
int64_t liva_task_select(LivaTask **tasks, int64_t count);

/// WithTimeout: run task with deadline, returns 1 if completed, 0 if timed out
int8_t liva_task_with_timeout(LivaTask *task, int64_t timeout_ms);

/// Initialize thread pool scheduler with N workers (0 = auto-detect)
void liva_scheduler_init(int32_t num_workers);

/// Shutdown thread pool scheduler
void liva_scheduler_shutdown();

/// Get number of worker threads
int32_t liva_scheduler_worker_count();

/// Async file read — offloads to thread pool, returns malloc'd content or NULL
char *liva_async_file_read(const char *path);

/// Async file write — offloads to thread pool, returns 1 on success, 0 on failure
int8_t liva_async_file_write(const char *path, const char *content);

void liva_coro_resume(void *handle);
void liva_coro_destroy(void *handle);
void liva_scheduler_run(LivaTask *root);

/// Async sleep: register timer and suspend coroutine
void liva_async_sleep(LivaTask *task, int64_t ms);

// === Channel Runtime ===
int64_t liva_channel_create(int64_t capacity);
void liva_channel_send(int64_t handle, int64_t value);
int64_t liva_channel_receive(int64_t handle, int8_t *ok);
void liva_channel_close(int64_t handle);
int64_t liva_channel_len(int64_t handle);
void liva_channel_free(int64_t handle);

/// Non-blocking send. Returns 1 on success, 0 if buffer full or channel closed.
int8_t liva_channel_try_send(int64_t handle, int64_t value);

/// Non-blocking receive. Sets *ok to 1 if a value was retrieved, 0 if buffer empty.
int64_t liva_channel_try_receive(int64_t handle, int8_t *ok);

// === TOML (minimal) ===
//
// Supports the same subset as the compiler's liva.toml parser:
// [section] headers, key = "string" / 123 / true / false / ["a", "b"].
// No nested tables, no inline tables, no datetimes, no floats.

/// Parse TOML text. Returns opaque handle (>0) or 0 on error.
int64_t liva_toml_parse(const char *text);

/// Get a string value. Caller owns the returned malloc'd string, or NULL if missing.
char *liva_toml_get_string(int64_t handle, const char *section, const char *key);

/// Get an integer value. Sets *ok = 1 on success, 0 if missing or wrong type.
int64_t liva_toml_get_int(int64_t handle, const char *section,
                          const char *key, int8_t *ok);

/// Get a boolean value. Sets *ok = 1 on success, 0 if missing or wrong type.
int8_t liva_toml_get_bool(int64_t handle, const char *section,
                          const char *key, int8_t *ok);

/// Returns 1 if the key exists in the section, 0 otherwise.
int8_t liva_toml_has_key(int64_t handle, const char *section, const char *key);

/// Free a TOML document handle.
void liva_toml_free(int64_t handle);

// === TaskGroup Runtime ===
int64_t liva_task_group_create();
void liva_task_group_spawn(int64_t group, LivaTask *task);
void liva_task_group_await_all(int64_t group);
void liva_task_group_cancel_all(int64_t group);
int64_t liva_task_group_count(int64_t group);
void liva_task_group_free(int64_t group);

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

/// Pretty-print JSON with given indent width (2..16). Caller frees.
char *liva_json_stringify_pretty(const char *json, int32_t indent);

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

// === Test Runner ===

/// Begin a test run (reset counters)
void liva_test_begin();

/// Run a single test: setjmp + call test_fn, print PASS/FAIL
void liva_test_run(const char *name, void (*test_fn)(void));

/// Closure-aware test runner. Passes env_ptr as first arg to fn_ptr, catches
/// longjmp from liva_test_fail. Returns 1 on pass, 0 on failure.
int8_t liva_test_run_closure(const char *name, void *fn_ptr, void *env_ptr);

/// End a test run: print summary, return 0 if all passed, 1 if any failed
int32_t liva_test_end();

/// Fail the current test (longjmp if in test context, abort otherwise)
void liva_test_fail(const char *msg);

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

/// URL percent-encode (RFC 3986 unreserved set), returns malloc'd string
char *liva_url_encode(const char *data);

/// URL percent-decode (handles %XX and '+' as space), returns malloc'd string
/// or NULL on invalid input
char *liva_url_decode(const char *data);

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

// === Synchronization: RWLock (shared/exclusive) ===

/// Create a reader-writer lock, returns handle (0 on error)
int64_t liva_rwlock_create();

/// Acquire shared (read) lock, blocks until acquired
void liva_rwlock_read_lock(int64_t handle);

/// Release shared (read) lock
void liva_rwlock_read_unlock(int64_t handle);

/// Acquire exclusive (write) lock, blocks until acquired
void liva_rwlock_write_lock(int64_t handle);

/// Release exclusive (write) lock
void liva_rwlock_write_unlock(int64_t handle);

/// Try to acquire shared lock without blocking, returns 1 if acquired
int8_t liva_rwlock_try_read_lock(int64_t handle);

/// Try to acquire exclusive lock without blocking, returns 1 if acquired
int8_t liva_rwlock_try_write_lock(int64_t handle);

/// Free an rwlock
void liva_rwlock_free(int64_t handle);

// === Synchronization: ConditionVariable ===

/// Create a condition variable, returns handle (0 on error)
int64_t liva_condvar_create();

/// Atomically release mtx and wait until notified, then reacquire mtx.
/// Caller must already hold the mutex.
void liva_condvar_wait(int64_t cvHandle, int64_t mtxHandle);

/// Wake up one thread waiting on this condition variable
void liva_condvar_notify_one(int64_t handle);

/// Wake up all threads waiting on this condition variable
void liva_condvar_notify_all(int64_t handle);

/// Free a condition variable
void liva_condvar_free(int64_t handle);

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

// === UTF-8 utilities ===

/// Count Unicode codepoints in a UTF-8 string (same as len)
int64_t liva_str_char_count(const char *s);

/// Returns the Unicode codepoint at character index, or -1 if out of range
int32_t liva_str_codepoint_at(const char *s, int64_t index);

/// Returns 1 if every byte is ASCII (< 0x80), 0 otherwise
int8_t liva_str_is_ascii(const char *s);

/// Codepoint classification (ASCII range only)
int8_t liva_char_is_alpha(int32_t cp);
int8_t liva_char_is_digit(int32_t cp);
int8_t liva_char_is_alnum(int32_t cp);
int8_t liva_char_is_space(int32_t cp);
int8_t liva_char_is_upper(int32_t cp);
int8_t liva_char_is_lower(int32_t cp);

/// Case conversion (ASCII range only)
int32_t liva_char_to_upper(int32_t cp);
int32_t liva_char_to_lower(int32_t cp);

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

// === Benchmarking ===

/// Start benchmark timer — returns opaque handle (nanosecond precision)
int64_t liva_bench_start();

/// Complete one iteration, returns elapsed nanoseconds since start
int64_t liva_bench_iter(int64_t handle);

/// Finish benchmark, returns average nanoseconds per iteration
int64_t liva_bench_done(int64_t handle);

/// Report benchmark results to stdout: "name: avg ns/iter (N iterations)"
void liva_bench_report(const char *name, int64_t handle);

/// Reset benchmark handle for reuse
void liva_bench_reset(int64_t handle);

// === Crypto ===

/// SHA-256 hash, returns malloc'd 64-char hex string
char *liva_sha256(const char *data);

/// MD5 hash, returns malloc'd 32-char hex string
char *liva_md5(const char *data);

/// HMAC-SHA256, returns malloc'd 64-char hex string
char *liva_hmac_sha256(const char *key, const char *data);

/// SHA-1 hash, returns malloc'd 40-char hex string
char *liva_sha1(const char *data);

/// SHA-512 hash, returns malloc'd 128-char hex string
char *liva_sha512(const char *data);

/// HMAC-SHA1, returns malloc'd 40-char hex string
char *liva_hmac_sha1(const char *key, const char *data);

/// HMAC-SHA512, returns malloc'd 128-char hex string
char *liva_hmac_sha512(const char *key, const char *data);

/// Base64URL (RFC 4648 §5) encode — '-' '_' alphabet, no padding (JWT)
char *liva_base64_url_encode(const char *data);

/// Base64URL decode — accepts URL-safe alphabet, padding optional
char *liva_base64_url_decode(const char *data);

/// JWT-friendly: HMAC-SHA256 raw bytes → base64url-encoded signature
char *liva_jwt_hs256_sig(const char *secret, const char *data);

/// JWT-friendly: HMAC-SHA512 raw bytes → base64url-encoded signature
char *liva_jwt_hs512_sig(const char *secret, const char *data);

/// Verify HS256 JWT — returns malloc'd payload JSON if signature matches,
/// nullptr otherwise. Signature comparison is constant-time.
char *liva_jwt_hs256_verify(const char *secret, const char *token);

/// Constant-time string equality — returns 1 if equal, 0 otherwise.
/// Comparison time depends only on the length of the longer string.
int8_t liva_const_time_eq(const char *a, const char *b);

/// Allocate a new buffer that is a shallow copy of `data` (count*elem_size
/// bytes). Used by struct literals that store a DynArray field value: the
/// new struct gets an independently-freeable buffer so the source array
/// can also still be freed safely.
void *liva_array_clone(const void *data, int64_t count, int64_t elem_size);

/// Copy `s` into a fresh malloc'd byte buffer; `*out_len` receives the
/// strlen byte count. Used to materialize `[u8]` from a Liva string.
void *liva_str_to_bytes(const char *s, int64_t *out_len);

/// Allocate a fresh null-terminated string holding the `len` bytes from
/// `data`. Embedded NUL bytes survive in the buffer but Liva's strlen-based
/// length will truncate at the first one — caller's responsibility.
char *liva_bytes_to_str(const void *data, int64_t len);

/// Decode a hex string into a fresh byte buffer; `*out_len` receives the
/// byte count, *ok flags success. Unlike liva_hex_decode this lets the
/// caller see embedded NUL bytes via the explicit length.
void *liva_hex_decode_bytes(const char *data, int64_t *out_len, int8_t *ok);

/// Encode `len` raw bytes as a lowercase hex string (caller frees).
char *liva_hex_encode_bytes(const void *data, int64_t len);

/// Encode `len` raw bytes as base64url (no padding); caller frees.
char *liva_base64_url_encode_bytes(const void *data, int64_t len);

/// Decode a base64url string into raw bytes. *out_len = byte count,
/// *ok = 1 on success, 0 on failure (returns nullptr in that case).
void *liva_base64_url_decode_bytes(const char *data, int64_t *out_len, int8_t *ok);

/// gzip-encode `len` bytes. Uses RFC 1952 framing around an RFC 1951
/// stored-block deflate stream: header (10 B) + uncompressed blocks +
/// CRC32 + ISIZE trailer (8 B). Output is valid gzip, accepted by any
/// conformant decoder, but never reduces size — real compression is a
/// future addition. Caller frees.
void *liva_gzip_encode_bytes(const void *data, int64_t len, int64_t *out_len);

/// gzip-decode `len` bytes. Handles all three deflate block types
/// (stored, fixed Huffman, dynamic Huffman) and verifies the CRC32 +
/// ISIZE trailer. *out_len = decoded byte count, *ok = 1 on success.
/// Returns nullptr on failure.
void *liva_gzip_decode_bytes(const void *data, int64_t len,
                              int64_t *out_len, int8_t *ok);

/// Format a Unix timestamp as RFC 3339 / ISO 8601 UTC ("YYYY-MM-DDTHH:MM:SSZ")
char *liva_iso_format_utc(double timestamp);

/// Parse RFC 3339 / ISO 8601 string to Unix timestamp. Accepts:
///   YYYY-MM-DD
///   YYYY-MM-DDTHH:MM:SS
///   YYYY-MM-DDTHH:MM:SS.fff
///   YYYY-MM-DDTHH:MM:SS[Z|±HH:MM]
/// Sets *ok to 1 on success, 0 on failure.
double liva_iso_parse(const char *str, int8_t *ok);

// === Panic ===

/// Called when an unrecoverable error occurs
[[noreturn]] void liva_panic(const char *message);

/// Called on use-after-move (debug builds)
[[noreturn]] void liva_use_after_move(const char *var_name);

// === BTreeMap runtime (P1-8 alt-spec 3) ===
// Keys and values use int32_t so Liva integer literals (i32) match without widening.
int64_t  liva_btree_i64_new();
void     liva_btree_i64_free(int64_t handle);
void     liva_btree_i64_insert(int64_t handle, int32_t key, int32_t value);
int32_t  liva_btree_i64_get(int64_t handle, int32_t key);
int32_t  liva_btree_i64_contains(int64_t handle, int32_t key);
int32_t  liva_btree_i64_remove(int64_t handle, int32_t key);
int32_t  liva_btree_i64_size(int64_t handle);
int32_t  liva_btree_i64_is_empty(int64_t handle);

int64_t  liva_btree_str_new();
void     liva_btree_str_free(int64_t handle);
void     liva_btree_str_insert(int64_t handle, const char *key, int32_t value);
int32_t  liva_btree_str_get(int64_t handle, const char *key);
int32_t  liva_btree_str_contains(int64_t handle, const char *key);
int32_t  liva_btree_str_remove(int64_t handle, const char *key);
int32_t  liva_btree_str_size(int64_t handle);
int32_t  liva_btree_str_is_empty(int64_t handle);

} // extern "C"
