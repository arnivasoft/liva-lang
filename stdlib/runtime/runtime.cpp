#include "runtime.h"
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <direct.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#endif

#ifdef LIVA_HAS_CURL
#include <curl/curl.h>
#endif

extern "C" {

// Helper: duplicate a C string via malloc (used across many functions)
static char *strdup_safe(const char *s) {
    if (!s) return nullptr;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}

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
    if (!str) str = "";
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
    if (!data) { data = ""; len = 0; }
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
    if (!a || !a->data) {
        if (b && b->data) return liva_string_new(b->data);
        return liva_string_new("");
    }
    if (!b || !b->data) return liva_string_new(a->data);
    // Overflow check
    if (a->length > SIZE_MAX - b->length - 1) liva_panic("string concat overflow");
    result.length = a->length + b->length;
    result.capacity = result.length + 1;
    result.data = (char *)liva_alloc(result.capacity);
    memcpy(result.data, a->data, a->length);
    memcpy(result.data + a->length, b->data, b->length);
    result.data[result.length] = '\0';
    return result;
}

int32_t liva_string_compare(const LivaString *a, const LivaString *b) {
    const char *sa = (a && a->data) ? a->data : "";
    const char *sb = (b && b->data) ? b->data : "";
    return strcmp(sa, sb);
}

// === UTF-8 Internal Helpers ===

// Returns the byte length of a UTF-8 code point starting with byte c (1-4, 0 on error)
static int utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;        // 0xxxxxxx
    if ((c & 0xE0) == 0xC0) return 2; // 110xxxxx
    if ((c & 0xF0) == 0xE0) return 3; // 1110xxxx
    if ((c & 0xF8) == 0xF0) return 4; // 11110xxx
    return 1; // invalid lead byte — treat as 1 byte to avoid infinite loops
}

// Count the number of Unicode code points in a UTF-8 string
static int64_t utf8_count(const char *str) {
    int64_t count = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        p += utf8_char_len(*p);
        count++;
    }
    return count;
}

// Convert a code point index to a byte offset. Returns byte offset or -1 if out of range.
static int64_t utf8_offset(const char *str, int64_t cp_index) {
    const unsigned char *p = (const unsigned char *)str;
    int64_t i = 0;
    while (*p && i < cp_index) {
        p += utf8_char_len(*p);
        i++;
    }
    if (i < cp_index) return -1; // out of range
    return (int64_t)(p - (const unsigned char *)str);
}

// Get the byte length of the code point at the given byte offset
static int utf8_cp_bytes(const char *str, int64_t byte_offset) {
    return utf8_char_len((unsigned char)str[byte_offset]);
}

// Convert a byte offset to a code point index
static int64_t utf8_byte_to_cp(const char *str, int64_t byte_offset) {
    const unsigned char *p = (const unsigned char *)str;
    int64_t cp = 0;
    int64_t off = 0;
    while (off < byte_offset && *p) {
        int len = utf8_char_len(*p);
        p += len;
        off += len;
        cp++;
    }
    return cp;
}

// === Simple char*-based string operations ===

char *liva_str_concat(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    // Overflow check
    if (la > SIZE_MAX - lb - 1) liva_panic("string concat overflow");
    char *result = (char *)malloc(la + lb + 1);
    if (!result) liva_panic("out of memory");
    memcpy(result, a, la);
    memcpy(result + la, b, lb + 1);
    return result;
}

int32_t liva_str_equal(const char *a, const char *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0 ? 1 : 0;
}

int64_t liva_str_length(const char *a) {
    if (!a) return 0;
    return utf8_count(a);
}

int64_t liva_str_byte_length(const char *a) {
    if (!a) return 0;
    return (int64_t)strlen(a);
}

char *liva_i32_to_str(int32_t value) {
    char *buf = (char *)malloc(16);
    if (!buf) liva_panic("out of memory");
    snprintf(buf, 16, "%d", value);
    return buf;
}

char *liva_f64_to_str(double value) {
    // %g can produce up to ~24 chars for extreme values; 64 is safe
    char *buf = (char *)malloc(64);
    if (!buf) liva_panic("out of memory");
    snprintf(buf, 64, "%g", value);
    return buf;
}

char *liva_bool_to_str(int8_t value) {
    const char *s = value ? "true" : "false";
    char *buf = (char *)malloc(6);
    if (!buf) liva_panic("out of memory");
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
    if (!str || !substr) return 0;
    return strstr(str, substr) != nullptr ? 1 : 0;
}

int8_t liva_str_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    size_t pl = strlen(prefix);
    return strncmp(str, prefix, pl) == 0 ? 1 : 0;
}

int8_t liva_str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t sl = strlen(str), xl = strlen(suffix);
    if (xl > sl) return 0;
    return strcmp(str + sl - xl, suffix) == 0 ? 1 : 0;
}

int64_t liva_str_index_of(const char *str, const char *substr) {
    if (!str || !substr) return -1;
    const char *p = strstr(str, substr);
    if (!p) return -1;
    // Convert byte offset to code point index
    return utf8_byte_to_cp(str, (int64_t)(p - str));
}

char *liva_str_substring(const char *str, int64_t start, int64_t length) {
    if (!str) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    // Code point-based substring
    int64_t cp_count = utf8_count(str);
    if (start < 0) start = 0;
    if (start > cp_count) start = cp_count;
    if (length < 0) length = 0;
    if (start + length > cp_count) length = cp_count - start;

    // Find byte offset for start code point
    int64_t byte_start = utf8_offset(str, start);
    if (byte_start < 0) byte_start = (int64_t)strlen(str);

    // Find byte offset for end code point
    int64_t byte_end = utf8_offset(str, start + length);
    if (byte_end < 0) byte_end = (int64_t)strlen(str);

    int64_t byte_len = byte_end - byte_start;
    char *result = (char *)malloc((size_t)byte_len + 1);
    memcpy(result, str + byte_start, (size_t)byte_len);
    result[byte_len] = '\0';
    return result;
}

char *liva_str_trim(const char *str) {
    if (!str) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
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
    if (!str) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
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
    if (!str) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
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
    if (!str) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    if (!old_sub) old_sub = "";
    if (!new_sub) new_sub = "";
    size_t sl = strlen(str), ol = strlen(old_sub), nl = strlen(new_sub);
    if (ol == 0) {
        char *result = (char *)malloc(sl + 1);
        if (!result) liva_panic("out of memory");
        memcpy(result, str, sl + 1);
        return result;
    }
    // Count occurrences
    size_t count = 0;
    const char *p = str;
    while ((p = strstr(p, old_sub)) != nullptr) { count++; p += ol; }
    // Safe size calculation using signed arithmetic to avoid underflow
    int64_t diff = (int64_t)nl - (int64_t)ol;
    int64_t new_len = (int64_t)sl + (int64_t)count * diff;
    if (new_len < 0) new_len = 0;
    char *result = (char *)malloc((size_t)new_len + 1);
    if (!result) liva_panic("out of memory");
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
    if (!str || !delim || !count) {
        if (count) *count = 0;
        return nullptr;
    }
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
        // Split into individual code points (UTF-8 aware)
        n = utf8_count(str);
        parts = (char **)realloc(parts, (size_t)n * sizeof(char *));
        const unsigned char *cp = (const unsigned char *)str;
        for (int64_t i = 0; i < n; i++) {
            int cplen = utf8_char_len(*cp);
            parts[i] = (char *)malloc((size_t)cplen + 1);
            memcpy(parts[i], cp, (size_t)cplen);
            parts[i][cplen] = '\0';
            cp += cplen;
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
    size_t capacity = 256;
    size_t length = 0;
    char *buf = (char *)malloc(capacity);
    if (!buf) return nullptr;

    while (true) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *newBuf = (char *)realloc(buf, capacity);
            if (!newBuf) { free(buf); return nullptr; }
            buf = newBuf;
        }
        int ch = fgetc((FILE *)fp);
        if (ch == EOF) {
            if (length == 0) { free(buf); return nullptr; }
            break;
        }
        if (ch == '\n') break;
        buf[length++] = (char)ch;
    }
    // Strip trailing \r (Windows line endings)
    if (length > 0 && buf[length - 1] == '\r')
        length--;
    buf[length] = '\0';

    // Shrink to fit
    char *result = (char *)realloc(buf, length + 1);
    return result ? result : buf;
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

int32_t liva_file_seek(void *fp, int64_t offset, int32_t whence) {
    if (!fp) return -1;
    int w = SEEK_SET;
    if (whence == 1) w = SEEK_CUR;
    else if (whence == 2) w = SEEK_END;
    return (int32_t)fseek((FILE *)fp, (long)offset, w);
}

int64_t liva_file_tell(void *fp) {
    if (!fp) return -1;
    return (int64_t)ftell((FILE *)fp);
}

int64_t liva_file_size(void *fp) {
    if (!fp) return -1;
    FILE *f = (FILE *)fp;
    long cur = ftell(f);
    if (cur < 0) return -1;
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long size = ftell(f);
    fseek(f, cur, SEEK_SET); // restore position
    return (int64_t)size;
}

char *liva_read_line() {
    size_t capacity = 256;
    size_t length = 0;
    char *buf = (char *)malloc(capacity);
    if (!buf) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    while (true) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *newBuf = (char *)realloc(buf, capacity);
            if (!newBuf) { buf[length] = '\0'; return buf; }
            buf = newBuf;
        }
        int ch = fgetc(stdin);
        if (ch == EOF || ch == '\n') break;
        buf[length++] = (char)ch;
    }
    // Strip trailing \r (Windows line endings)
    if (length > 0 && buf[length - 1] == '\r')
        length--;
    buf[length] = '\0';

    // Shrink to fit
    char *result = (char *)realloc(buf, length + 1);
    return result ? result : buf;
}

char *liva_i64_to_str(int64_t value) {
    char *buf = (char *)malloc(24);
    if (!buf) liva_panic("out of memory");
    snprintf(buf, 24, "%lld", (long long)value);
    return buf;
}

// === Directory Operations ===

char **liva_dir_list(const char *path, int64_t *count) {
    if (!path || !count) {
        if (count) *count = 0;
        return nullptr;
    }
    *count = 0;
    std::vector<std::string> entries;

#ifdef _WIN32
    std::string pattern = std::string(path) + "\\*";
    WIN32_FIND_DATAA fdata;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fdata);
    if (hFind == INVALID_HANDLE_VALUE) return nullptr;
    do {
        if (strcmp(fdata.cFileName, ".") != 0 && strcmp(fdata.cFileName, "..") != 0) {
            entries.push_back(fdata.cFileName);
        }
    } while (FindNextFileA(hFind, &fdata));
    FindClose(hFind);
#else
    DIR *dir = opendir(path);
    if (!dir) return nullptr;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            entries.push_back(entry->d_name);
        }
    }
    closedir(dir);
#endif

    *count = (int64_t)entries.size();
    if (entries.empty()) return nullptr;

    char **result = (char **)malloc(entries.size() * sizeof(char *));
    if (!result) { *count = 0; return nullptr; }
    for (size_t i = 0; i < entries.size(); i++) {
        result[i] = (char *)malloc(entries[i].size() + 1);
        if (result[i]) memcpy(result[i], entries[i].c_str(), entries[i].size() + 1);
    }
    return result;
}

int8_t liva_dir_create(const char *path) {
    if (!path) return 0;
    // Create directory and parents
    std::string p(path);
    for (size_t i = 1; i < p.size(); i++) {
        if (p[i] == '/' || p[i] == '\\') {
            std::string sub = p.substr(0, i);
#ifdef _WIN32
            _mkdir(sub.c_str());
#else
            mkdir(sub.c_str(), 0755);
#endif
        }
    }
#ifdef _WIN32
    return (_mkdir(p.c_str()) == 0 || errno == EEXIST) ? 1 : 0;
#else
    return (mkdir(p.c_str(), 0755) == 0 || errno == EEXIST) ? 1 : 0;
#endif
}

int8_t liva_dir_remove(const char *path) {
    if (!path) return 0;
    // Recursive removal
#ifdef _WIN32
    std::string pattern = std::string(path) + "\\*";
    WIN32_FIND_DATAA fdata;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fdata);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fdata.cFileName, ".") == 0 || strcmp(fdata.cFileName, "..") == 0)
                continue;
            std::string child = std::string(path) + "\\" + fdata.cFileName;
            if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                liva_dir_remove(child.c_str());
            } else {
                DeleteFileA(child.c_str());
            }
        } while (FindNextFileA(hFind, &fdata));
        FindClose(hFind);
    }
    return RemoveDirectoryA(path) ? 1 : 0;
#else
    DIR *dir = opendir(path);
    if (!dir) return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        std::string child = std::string(path) + "/" + entry->d_name;
        struct stat st;
        if (stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            liva_dir_remove(child.c_str());
        } else {
            remove(child.c_str());
        }
    }
    closedir(dir);
    return (rmdir(path) == 0) ? 1 : 0;
#endif
}

int8_t liva_dir_exists(const char *path) {
    if (!path) return 0;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
#endif
}

// === Path Operations ===

char *liva_path_join(const char *a, const char *b) {
    if (!a || !*a) { return b ? strdup_safe(b) : strdup_safe(""); }
    if (!b || !*b) { return strdup_safe(a); }
    size_t la = strlen(a);
    // Check if 'a' already ends with separator
    bool hasSep = (a[la - 1] == '/' || a[la - 1] == '\\');
    size_t lb = strlen(b);
    size_t total = la + lb + (hasSep ? 0 : 1) + 1;
    char *result = (char *)malloc(total);
    if (!result) liva_panic("out of memory");
    memcpy(result, a, la);
    if (!hasSep) result[la] = '/';
    memcpy(result + la + (hasSep ? 0 : 1), b, lb + 1);
    return result;
}

char *liva_path_dirname(const char *path) {
    if (!path || !*path) return strdup_safe(".");
    size_t len = strlen(path);
    // Find last separator
    int64_t last = -1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') last = (int64_t)i;
    }
    if (last < 0) return strdup_safe(".");
    if (last == 0) return strdup_safe("/");
    char *result = (char *)malloc((size_t)last + 1);
    if (!result) liva_panic("out of memory");
    memcpy(result, path, (size_t)last);
    result[last] = '\0';
    return result;
}

char *liva_path_basename(const char *path) {
    if (!path || !*path) return strdup_safe("");
    size_t len = strlen(path);
    // Find last separator
    int64_t last = -1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') last = (int64_t)i;
    }
    const char *name = (last >= 0) ? path + last + 1 : path;
    return strdup_safe(name);
}

char *liva_path_extension(const char *path) {
    if (!path || !*path) return strdup_safe("");
    // Get basename first to avoid directory dots
    size_t len = strlen(path);
    int64_t lastSep = -1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') lastSep = (int64_t)i;
    }
    const char *name = (lastSep >= 0) ? path + lastSep + 1 : path;
    // Find last dot in basename
    const char *dot = nullptr;
    for (const char *p = name; *p; p++) {
        if (*p == '.') dot = p;
    }
    if (!dot || dot == name) return strdup_safe("");
    return strdup_safe(dot);
}

int8_t liva_path_exists(const char *path) {
    if (!path) return 0;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
#else
    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
#endif
}

int8_t liva_file_is_file(const char *path) {
    if (!path) return 0;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
#endif
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
    if (str) printf("%s", str);
}

void liva_println_str(const char *str) {
    if (str) printf("%s\n", str);
    else printf("\n");
}

// === Dynamic Array ===

void *liva_array_new(int64_t elem_size, int64_t capacity) {
    if (elem_size <= 0 || capacity <= 0) {
        // Return minimal valid allocation
        void *ptr = calloc(1, 1);
        if (!ptr) liva_panic("out of memory");
        return ptr;
    }
    // Overflow check
    if (capacity > (int64_t)(SIZE_MAX / (size_t)elem_size))
        liva_panic("array allocation overflow");
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
        // Overflow check
        if (new_cap > (int64_t)(SIZE_MAX / (size_t)elem_size))
            liva_panic("array push overflow");
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

static std::once_flag rand_once_flag;
static void ensure_rand_seeded() {
    std::call_once(rand_once_flag, []() {
        srand((unsigned)time(nullptr));
    });
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

// === Subprocess ===

// Internal process handle structure
struct LivaProcess {
#ifdef _WIN32
    HANDLE hProcess;
    HANDLE hThread;
    HANDLE hStdoutRead;
#else
    pid_t pid;
    int stdout_fd;  // read end of stdout pipe (-1 if none)
#endif
};

int32_t liva_exec(const char *command) {
    if (!command) return -1;
    return (int32_t)system(command);
}

char *liva_exec_output(const char *command) {
    if (!command) return nullptr;
#ifdef _WIN32
    FILE *pipe = _popen(command, "r");
#else
    FILE *pipe = popen(command, "r");
#endif
    if (!pipe) return nullptr;

    size_t capacity = 1024;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return nullptr;
    }

    int c;
    while ((c = fgetc(pipe)) != EOF) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *newBuf = (char *)realloc(buffer, capacity);
            if (!newBuf) { free(buffer); break; }
            buffer = newBuf;
        }
        buffer[length++] = (char)c;
    }
    buffer[length] = '\0';

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return buffer;
}

int64_t liva_process_start(const char *command) {
    if (!command) return 0;
    auto *proc = new(std::nothrow) LivaProcess();
    if (!proc) return 0;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        delete proc;
        return 0;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStdoutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcess needs mutable command string
    size_t cmdLen = strlen(command);
    char *cmdBuf = (char *)malloc(cmdLen + 1);
    if (!cmdBuf) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        delete proc;
        return 0;
    }
    memcpy(cmdBuf, command, cmdLen + 1);

    BOOL ok = CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                              0, nullptr, nullptr, &si, &pi);
    free(cmdBuf);
    CloseHandle(hStdoutWrite);  // close write end in parent

    if (!ok) {
        CloseHandle(hStdoutRead);
        delete proc;
        return 0;
    }

    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->hStdoutRead = hStdoutRead;
#else
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        delete proc;
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        delete proc;
        return 0;
    }
    if (pid == 0) {
        // Child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command, (char *)nullptr);
        _exit(127);
    }
    // Parent
    close(pipefd[1]);
    proc->pid = pid;
    proc->stdout_fd = pipefd[0];
#endif

    return (int64_t)(uintptr_t)proc;
}

int32_t liva_process_wait(int64_t handle) {
    if (!handle) return -1;
    auto *proc = (LivaProcess *)(uintptr_t)handle;

#ifdef _WIN32
    WaitForSingleObject(proc->hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(proc->hProcess, &exitCode);
    return (int32_t)exitCode;
#else
    int status = 0;
    waitpid(proc->pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

int8_t liva_process_kill(int64_t handle) {
    if (!handle) return 0;
    auto *proc = (LivaProcess *)(uintptr_t)handle;

#ifdef _WIN32
    return TerminateProcess(proc->hProcess, 1) ? 1 : 0;
#else
    return (kill(proc->pid, SIGTERM) == 0) ? 1 : 0;
#endif
}

char *liva_process_read(int64_t handle) {
    if (!handle) return nullptr;
    auto *proc = (LivaProcess *)(uintptr_t)handle;

    size_t capacity = 1024;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) return nullptr;

#ifdef _WIN32
    DWORD bytesRead;
    char chunk[4096];
    while (ReadFile(proc->hStdoutRead, chunk, sizeof(chunk), &bytesRead, nullptr) && bytesRead > 0) {
        if (length + bytesRead >= capacity) {
            while (length + bytesRead >= capacity) capacity *= 2;
            char *newBuf = (char *)realloc(buffer, capacity);
            if (!newBuf) { free(buffer); return nullptr; }
            buffer = newBuf;
        }
        memcpy(buffer + length, chunk, bytesRead);
        length += bytesRead;
    }
#else
    char chunk[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(proc->stdout_fd, chunk, sizeof(chunk))) > 0) {
        if (length + (size_t)bytesRead >= capacity) {
            while (length + (size_t)bytesRead >= capacity) capacity *= 2;
            char *newBuf = (char *)realloc(buffer, capacity);
            if (!newBuf) { free(buffer); return nullptr; }
            buffer = newBuf;
        }
        memcpy(buffer + length, chunk, (size_t)bytesRead);
        length += (size_t)bytesRead;
    }
#endif

    buffer[length] = '\0';
    return buffer;
}

void liva_process_close(int64_t handle) {
    if (!handle) return;
    auto *proc = (LivaProcess *)(uintptr_t)handle;

#ifdef _WIN32
    if (proc->hStdoutRead) CloseHandle(proc->hStdoutRead);
    if (proc->hProcess) CloseHandle(proc->hProcess);
    if (proc->hThread) CloseHandle(proc->hThread);
#else
    if (proc->stdout_fd >= 0) close(proc->stdout_fd);
#endif

    delete proc;
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

char **liva_regex_find_groups(const char *str, const char *pattern, int64_t *count) {
    *count = 0;
    if (!str || !pattern) return nullptr;
    try {
        std::regex re(pattern);
        std::smatch m;
        std::string s(str);
        if (std::regex_search(s, m, re)) {
            *count = (int64_t)m.size();
            char **result = (char **)malloc(m.size() * sizeof(char *));
            for (size_t i = 0; i < m.size(); i++) {
                std::string g = m[i].str();
                result[i] = (char *)malloc(g.size() + 1);
                memcpy(result[i], g.c_str(), g.size() + 1);
            }
            return result;
        }
    } catch (...) {}
    return nullptr;
}

int64_t liva_regex_compile(const char *pattern) {
    if (!pattern) return 0;
    try {
        auto *re = new std::regex(pattern);
        return (int64_t)(intptr_t)re;
    } catch (...) {
        return 0;
    }
}

int8_t liva_regex_test(int64_t handle, const char *str) {
    if (!handle || !str) return 0;
    auto *re = (std::regex *)(intptr_t)handle;
    try {
        return std::regex_search(std::string(str), *re) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

char *liva_regex_exec(int64_t handle, const char *str) {
    if (!handle || !str) return nullptr;
    auto *re = (std::regex *)(intptr_t)handle;
    try {
        std::smatch m;
        std::string s(str);
        if (std::regex_search(s, m, *re)) {
            std::string match = m[0].str();
            char *result = (char *)malloc(match.size() + 1);
            memcpy(result, match.c_str(), match.size() + 1);
            return result;
        }
    } catch (...) {}
    return nullptr;
}

char **liva_regex_exec_groups(int64_t handle, const char *str, int64_t *count) {
    *count = 0;
    if (!handle || !str) return nullptr;
    auto *re = (std::regex *)(intptr_t)handle;
    try {
        std::smatch m;
        std::string s(str);
        if (std::regex_search(s, m, *re)) {
            *count = (int64_t)m.size();
            char **result = (char **)malloc(m.size() * sizeof(char *));
            for (size_t i = 0; i < m.size(); i++) {
                std::string g = m[i].str();
                result[i] = (char *)malloc(g.size() + 1);
                memcpy(result[i], g.c_str(), g.size() + 1);
            }
            return result;
        }
    } catch (...) {}
    return nullptr;
}

char *liva_regex_replace_compiled(int64_t handle, const char *str, const char *replacement) {
    if (!handle || !str || !replacement) {
        size_t len = str ? strlen(str) : 0;
        char *copy = (char *)malloc(len + 1);
        if (str) memcpy(copy, str, len + 1);
        else copy[0] = '\0';
        return copy;
    }
    auto *re = (std::regex *)(intptr_t)handle;
    try {
        std::string result = std::regex_replace(std::string(str), *re,
                                                 std::string(replacement));
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

void liva_regex_free(int64_t handle) {
    if (!handle) return;
    auto *re = (std::regex *)(intptr_t)handle;
    delete re;
}

// === Networking ===

// Helper: allocate an empty LivaHttpResponse
static LivaHttpResponse *alloc_response() {
    LivaHttpResponse *resp = (LivaHttpResponse *)calloc(1, sizeof(LivaHttpResponse));
    return resp;
}

// Helper: parse raw headers string "Name: Value\r\n..." into response
static void parse_response_headers(LivaHttpResponse *resp, const std::string &raw) {
    std::vector<std::string> names, values;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        if (eol == std::string::npos) eol = raw.size();
        std::string line = raw.substr(pos, eol - pos);
        // Strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol + 1;
        // Skip status line and empty lines
        if (line.empty() || line.find(':') == std::string::npos) continue;
        size_t colon = line.find(':');
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        // Trim leading whitespace from value
        size_t vs = 0;
        while (vs < value.size() && value[vs] == ' ') vs++;
        if (vs > 0) value = value.substr(vs);
        names.push_back(name);
        values.push_back(value);
    }
    resp->header_count = (int64_t)names.size();
    if (resp->header_count > 0) {
        resp->header_names = (char **)malloc((size_t)resp->header_count * sizeof(char *));
        resp->header_values = (char **)malloc((size_t)resp->header_count * sizeof(char *));
        for (int64_t i = 0; i < resp->header_count; i++) {
            resp->header_names[i] = strdup_safe(names[(size_t)i].c_str());
            resp->header_values[i] = strdup_safe(values[(size_t)i].c_str());
        }
    }
}

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

// Simple body-only request (backward compat)
static char *winhttp_request_simple(const char *url, const char *method, const char *body) {
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

    // Read response body
    std::string response;
    DWORD bytesRead = 0;
    char readBuf[8192];
    while (WinHttpReadData(hRequest, readBuf, sizeof(readBuf), &bytesRead) && bytesRead > 0) {
        response.append(readBuf, bytesRead);
        bytesRead = 0;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    char *result = (char *)malloc(response.size() + 1);
    if (!result) return nullptr;
    memcpy(result, response.c_str(), response.size() + 1);
    return result;
}

// Full request with headers, timeout, status code
static LivaHttpResponse *winhttp_request_full(
    const char *method, const char *url, const char *body,
    const char **headers, int64_t header_count, int64_t timeout_ms) {

    std::wstring host, path;
    int port;
    bool https;
    if (!parse_url(url, host, path, port, https)) return nullptr;

    HINTERNET hSession = WinHttpOpen(L"Liva/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return nullptr;

    // Set timeout
    if (timeout_ms > 0) {
        int tms = (int)timeout_ms;
        WinHttpSetTimeouts(hSession, tms, tms, tms, tms);
    }

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

    // Add request headers
    if (headers && header_count > 0) {
        for (int64_t i = 0; i < header_count; i++) {
            const char *name = headers[i * 2];
            const char *value = headers[i * 2 + 1];
            if (!name || !value) continue;
            std::string hdr = std::string(name) + ": " + value;
            std::wstring whdr(hdr.begin(), hdr.end());
            WinHttpAddRequestHeaders(hRequest, whdr.c_str(), (DWORD)-1,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
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

    LivaHttpResponse *resp = alloc_response();
    if (!resp) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return nullptr;
    }

    // Get status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int32_t)statusCode;

    // Get response headers
    DWORD headerSize = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &headerSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && headerSize > 0) {
        std::vector<wchar_t> headerBuf(headerSize / sizeof(wchar_t));
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, headerBuf.data(), &headerSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            // Convert wchar_t to char for parsing
            std::string rawHeaders;
            for (wchar_t wc : headerBuf) {
                if (wc == 0) break;
                rawHeaders += (char)wc;
            }
            parse_response_headers(resp, rawHeaders);
        }
    }

    // Read response body
    std::string responseBody;
    DWORD bytesRead = 0;
    char readBuf[8192];
    while (WinHttpReadData(hRequest, readBuf, sizeof(readBuf), &bytesRead) && bytesRead > 0) {
        responseBody.append(readBuf, bytesRead);
        bytesRead = 0;
    }
    resp->body = strdup_safe(responseBody.c_str());

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return resp;
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

// libcurl header callback
static size_t curl_header_cb(char *buffer, size_t size, size_t nitems, void *userp) {
    size_t total = size * nitems;
    std::string *headerBuf = static_cast<std::string *>(userp);
    headerBuf->append(buffer, total);
    return total;
}

// Simple body-only request (backward compat)
static char *curl_request_simple(const char *url, const char *method, const char *body) {
    CURL *curl = curl_easy_init();
    if (!curl) return nullptr;
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (strcmp(method, "GET") != 0)
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return nullptr;
    char *result = (char *)malloc(response.size() + 1);
    if (!result) return nullptr;
    memcpy(result, response.c_str(), response.size() + 1);
    return result;
}

// Full request with headers, timeout, status code
static LivaHttpResponse *curl_request_full(
    const char *method, const char *url, const char *body,
    const char **headers, int64_t header_count, int64_t timeout_ms) {

    CURL *curl = curl_easy_init();
    if (!curl) return nullptr;

    std::string responseBody;
    std::string responseHeaders;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Set method
    if (strcmp(method, "GET") != 0)
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

    // Set body
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }

    // Set timeout
    if (timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    }

    // Set request headers
    struct curl_slist *slist = nullptr;
    if (headers && header_count > 0) {
        for (int64_t i = 0; i < header_count; i++) {
            const char *name = headers[i * 2];
            const char *value = headers[i * 2 + 1];
            if (!name || !value) continue;
            std::string hdr = std::string(name) + ": " + value;
            slist = curl_slist_append(slist, hdr.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    }

    CURLcode res = curl_easy_perform(curl);

    LivaHttpResponse *resp = nullptr;
    if (res == CURLE_OK) {
        resp = alloc_response();
        if (resp) {
            // Get status code
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            resp->status_code = (int32_t)status;
            resp->body = strdup_safe(responseBody.c_str());
            parse_response_headers(resp, responseHeaders);
        }
    }

    if (slist) curl_slist_free_all(slist);
    curl_easy_cleanup(curl);
    return resp;
}
#endif

// === Public HTTP API ===

char *liva_http_get(const char *url) {
    if (!url) return nullptr;
#ifdef _WIN32
    return winhttp_request_simple(url, "GET", nullptr);
#elif defined(LIVA_HAS_CURL)
    return curl_request_simple(url, "GET", nullptr);
#else
    (void)url;
    return nullptr;
#endif
}

char *liva_http_post(const char *url, const char *body) {
    if (!url) return nullptr;
#ifdef _WIN32
    return winhttp_request_simple(url, "POST", body);
#elif defined(LIVA_HAS_CURL)
    return curl_request_simple(url, "POST", body);
#else
    (void)url;
    (void)body;
    return nullptr;
#endif
}

char *liva_http_put(const char *url, const char *body) {
    if (!url) return nullptr;
#ifdef _WIN32
    return winhttp_request_simple(url, "PUT", body);
#elif defined(LIVA_HAS_CURL)
    return curl_request_simple(url, "PUT", body);
#else
    (void)url;
    (void)body;
    return nullptr;
#endif
}

char *liva_http_patch(const char *url, const char *body) {
    if (!url) return nullptr;
#ifdef _WIN32
    return winhttp_request_simple(url, "PATCH", body);
#elif defined(LIVA_HAS_CURL)
    return curl_request_simple(url, "PATCH", body);
#else
    (void)url;
    (void)body;
    return nullptr;
#endif
}

char *liva_http_delete(const char *url) {
    if (!url) return nullptr;
#ifdef _WIN32
    return winhttp_request_simple(url, "DELETE", nullptr);
#elif defined(LIVA_HAS_CURL)
    return curl_request_simple(url, "DELETE", nullptr);
#else
    (void)url;
    return nullptr;
#endif
}

LivaHttpResponse *liva_http_request(const char *method, const char *url,
                                     const char *body,
                                     const char **headers, int64_t header_count,
                                     int64_t timeout_ms) {
    if (!method || !url) return nullptr;
#ifdef _WIN32
    return winhttp_request_full(method, url, body, headers, header_count, timeout_ms);
#elif defined(LIVA_HAS_CURL)
    return curl_request_full(method, url, body, headers, header_count, timeout_ms);
#else
    (void)method;
    (void)url;
    (void)body;
    (void)headers;
    (void)header_count;
    (void)timeout_ms;
    return nullptr;
#endif
}

void liva_http_response_free(LivaHttpResponse *resp) {
    if (!resp) return;
    free(resp->body);
    for (int64_t i = 0; i < resp->header_count; i++) {
        free(resp->header_names[i]);
        free(resp->header_values[i]);
    }
    free(resp->header_names);
    free(resp->header_values);
    free(resp);
}

int32_t liva_http_response_status(const LivaHttpResponse *resp) {
    return resp ? resp->status_code : 0;
}

char *liva_http_response_body(const LivaHttpResponse *resp) {
    return resp ? strdup_safe(resp->body) : nullptr;
}

char *liva_http_response_header(const LivaHttpResponse *resp, const char *name) {
    if (!resp || !name) return nullptr;
    for (int64_t i = 0; i < resp->header_count; i++) {
        if (resp->header_names[i]) {
            // Case-insensitive header name comparison
            const char *a = resp->header_names[i];
            const char *b = name;
            bool match = true;
            while (*a && *b) {
                char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
                char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
                if (ca != cb) { match = false; break; }
                a++;
                b++;
            }
            if (match && *a == '\0' && *b == '\0')
                return strdup_safe(resp->header_values[i]);
        }
    }
    return nullptr;
}

// === Async/Coroutine Runtime ===

// --- Dynamic ready queue (resizable circular buffer) ---

static std::mutex async_mutex_;  // protects ready_queue + timer_heap

static LivaTask **ready_queue = nullptr;
static int64_t ready_cap = 0;
static int64_t ready_head = 0;
static int64_t ready_tail = 0;

static void ready_init() {
    ready_cap = 64;
    ready_queue = (LivaTask **)calloc((size_t)ready_cap, sizeof(LivaTask *));
    ready_head = 0;
    ready_tail = 0;
}

static void ready_cleanup() {
    free(ready_queue);
    ready_queue = nullptr;
    ready_cap = 0;
    ready_head = 0;
    ready_tail = 0;
}

static void ready_push(LivaTask *task) {
    int64_t count = ready_tail - ready_head;
    if (count >= ready_cap) {
        // Grow: allocate double, copy contiguous
        int64_t new_cap = ready_cap * 2;
        LivaTask **new_buf = (LivaTask **)calloc((size_t)new_cap, sizeof(LivaTask *));
        for (int64_t i = 0; i < count; i++) {
            new_buf[i] = ready_queue[(ready_head + i) % ready_cap];
        }
        free(ready_queue);
        ready_queue = new_buf;
        ready_head = 0;
        ready_tail = count;
        ready_cap = new_cap;
    }
    ready_queue[ready_tail % ready_cap] = task;
    ready_tail++;
}

static LivaTask *ready_pop() {
    if (ready_head == ready_tail) {
        // Reset indices to prevent unbounded growth
        ready_head = 0;
        ready_tail = 0;
        return nullptr;
    }
    LivaTask *task = ready_queue[ready_head % ready_cap];
    ready_head++;
    return task;
}

// --- Timer min-heap (deadline-based) ---

typedef struct TimerEntry {
    LivaTask *task;
    int64_t deadline_ms;  // absolute time from liva_clock_ms()
} TimerEntry;

static TimerEntry *timer_heap = nullptr;
static int64_t timer_count = 0;
static int64_t timer_cap = 0;

static void timer_init() {
    timer_cap = 32;
    timer_heap = (TimerEntry *)calloc((size_t)timer_cap, sizeof(TimerEntry));
    timer_count = 0;
}

static void timer_cleanup() {
    free(timer_heap);
    timer_heap = nullptr;
    timer_count = 0;
    timer_cap = 0;
}

static void timer_sift_up(int64_t idx) {
    while (idx > 0) {
        int64_t parent = (idx - 1) / 2;
        if (timer_heap[parent].deadline_ms <= timer_heap[idx].deadline_ms) break;
        TimerEntry tmp = timer_heap[parent];
        timer_heap[parent] = timer_heap[idx];
        timer_heap[idx] = tmp;
        idx = parent;
    }
}

static void timer_sift_down(int64_t idx) {
    while (true) {
        int64_t smallest = idx;
        int64_t left = 2 * idx + 1;
        int64_t right = 2 * idx + 2;
        if (left < timer_count && timer_heap[left].deadline_ms < timer_heap[smallest].deadline_ms)
            smallest = left;
        if (right < timer_count && timer_heap[right].deadline_ms < timer_heap[smallest].deadline_ms)
            smallest = right;
        if (smallest == idx) break;
        TimerEntry tmp = timer_heap[smallest];
        timer_heap[smallest] = timer_heap[idx];
        timer_heap[idx] = tmp;
        idx = smallest;
    }
}

static void timer_push(LivaTask *task, int64_t deadline_ms) {
    if (timer_count >= timer_cap) {
        int64_t new_cap = timer_cap * 2;
        timer_heap = (TimerEntry *)realloc(timer_heap, (size_t)new_cap * sizeof(TimerEntry));
        timer_cap = new_cap;
    }
    timer_heap[timer_count].task = task;
    timer_heap[timer_count].deadline_ms = deadline_ms;
    timer_sift_up(timer_count);
    timer_count++;
}

// Process expired timers: move them to ready queue.
// Returns ms until next timer fires, or -1 if no timers.
static int64_t timer_process_expired() {
    int64_t now = liva_clock_ms();
    while (timer_count > 0 && timer_heap[0].deadline_ms <= now) {
        LivaTask *task = timer_heap[0].task;
        // Remove top: move last to top and sift down
        timer_count--;
        if (timer_count > 0) {
            timer_heap[0] = timer_heap[timer_count];
            timer_sift_down(0);
        }
        ready_push(task);
    }
    if (timer_count > 0) {
        int64_t wait = timer_heap[0].deadline_ms - liva_clock_ms();
        return wait > 0 ? wait : 0;
    }
    return -1;
}

void liva_async_sleep(LivaTask *task, int64_t ms) {
    std::lock_guard<std::mutex> lock(async_mutex_);
    timer_push(task, liva_clock_ms() + ms);
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
        std::lock_guard<std::mutex> lock(async_mutex_);
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
    ready_init();
    timer_init();

    // Resume root coroutine
    if (!root->done) {
        liva_coro_resume(root->handle);
    }

    // Event loop: process timers + ready queue until root is done
    while (!root->done) {
        timer_process_expired();

        LivaTask *task = ready_pop();
        if (task) {
            if (!task->done) {
                liva_coro_resume(task->handle);
            }
            continue;
        }

        // No ready tasks — check if timers are pending
        int64_t wait = timer_process_expired();
        if (wait >= 0 && timer_count > 0) {
            // Idle-wait with max 10ms granularity
            int64_t sleep_ms = wait < 10 ? wait : 10;
            if (sleep_ms > 0) {
                liva_sleep(sleep_ms);
            }
            continue;
        }

        break;  // No ready tasks and no timers — done or deadlock
    }

    timer_cleanup();
    ready_cleanup();
}

// === JSON ===

// Helper: skip JSON whitespace
static const char *json_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// Helper: skip a JSON string (starting at opening quote), return ptr after closing quote
static const char *json_skip_string(const char *p) {
    if (*p != '"') return p;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') { p++; if (*p) p++; }
        else p++;
    }
    if (*p == '"') p++;
    return p;
}

// Helper: skip a JSON value, return ptr after value
static const char *json_skip_value(const char *p) {
    p = json_skip_ws(p);
    if (*p == '"') return json_skip_string(p);
    if (*p == '{' || *p == '[') {
        char open = *p, close = (*p == '{') ? '}' : ']';
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') { p = json_skip_string(p); continue; }
            if (*p == open) depth++;
            else if (*p == close) depth--;
            p++;
        }
        return p;
    }
    // number, bool, null
    while (*p && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    return p;
}

// Helper: extract unescaped string content from JSON string value at p (p points to opening quote)
// Returns malloc'd string. Handles basic escape sequences.
static char *json_extract_string(const char *p) {
    if (*p != '"') return nullptr;
    p++;
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return nullptr;
    while (*p && *p != '"') {
        char c;
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                default: c = *p; break;
            }
        } else {
            c = *p;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return nullptr; }
            buf = nb;
        }
        buf[len++] = c;
        p++;
    }
    buf[len] = '\0';
    return buf;
}

// Helper: find value for key in top-level JSON object.
// Returns pointer to start of value (after whitespace), or NULL.
static const char *json_find_key(const char *json, const char *key) {
    if (!json || !key) return nullptr;
    const char *p = json_skip_ws(json);
    if (*p != '{') return nullptr;
    p++;
    size_t keyLen = strlen(key);
    while (true) {
        p = json_skip_ws(p);
        if (*p == '}' || *p == '\0') return nullptr;
        if (*p == ',') { p++; p = json_skip_ws(p); }
        if (*p != '"') return nullptr;
        // Compare key
        const char *kStart = p + 1;
        const char *kEnd = kStart;
        while (*kEnd && *kEnd != '"') {
            if (*kEnd == '\\') { kEnd++; if (*kEnd) kEnd++; }
            else kEnd++;
        }
        size_t kLen = (size_t)(kEnd - kStart);
        p = kEnd;
        if (*p == '"') p++;
        p = json_skip_ws(p);
        if (*p != ':') return nullptr;
        p++;
        p = json_skip_ws(p);
        if (kLen == keyLen && memcmp(kStart, key, keyLen) == 0) return p;
        p = json_skip_value(p);
    }
}

char *liva_json_get(const char *json, const char *key) {
    const char *val = json_find_key(json, key);
    if (!val) return nullptr;
    if (*val == '"') return json_extract_string(val);
    // For non-string values, return the raw text
    const char *end = json_skip_value(val);
    size_t len = (size_t)(end - val);
    char *r = (char *)malloc(len + 1);
    if (r) { memcpy(r, val, len); r[len] = '\0'; }
    return r;
}

int64_t liva_json_get_int(const char *json, const char *key) {
    const char *val = json_find_key(json, key);
    if (!val) return 0;
    if (*val == '"') { val++; } // skip quote for string numbers
    char *endptr;
    long long r = strtoll(val, &endptr, 10);
    return (int64_t)r;
}

double liva_json_get_float(const char *json, const char *key) {
    const char *val = json_find_key(json, key);
    if (!val) return 0.0;
    if (*val == '"') { val++; }
    char *endptr;
    double r = strtod(val, &endptr);
    return r;
}

int8_t liva_json_get_bool(const char *json, const char *key) {
    const char *val = json_find_key(json, key);
    if (!val) return 0;
    if (*val == 't') return 1; // true
    if (*val == '"') {
        if (val[1] == 't' && val[2] == 'r') return 1;
    }
    return 0;
}

int8_t liva_json_is_valid(const char *json) {
    if (!json) return 0;
    const char *p = json_skip_ws(json);
    if (*p == '\0') return 0;
    const char *end = json_skip_value(p);
    end = json_skip_ws(end);
    return (*end == '\0') ? 1 : 0;
}

char **liva_json_keys(const char *json, int64_t *count) {
    if (count) *count = 0;
    if (!json || !count) return nullptr;
    const char *p = json_skip_ws(json);
    if (*p != '{') return nullptr;
    p++;
    std::vector<std::string> keys;
    while (true) {
        p = json_skip_ws(p);
        if (*p == '}' || *p == '\0') break;
        if (*p == ',') { p++; p = json_skip_ws(p); }
        if (*p != '"') break;
        char *k = json_extract_string(p);
        if (k) { keys.push_back(k); free(k); }
        p = json_skip_string(p);
        p = json_skip_ws(p);
        if (*p == ':') p++;
        p = json_skip_value(p);
    }
    *count = (int64_t)keys.size();
    if (keys.empty()) return nullptr;
    char **result = (char **)malloc(keys.size() * sizeof(char *));
    if (!result) { *count = 0; return nullptr; }
    for (size_t i = 0; i < keys.size(); i++)
        result[i] = strdup_safe(keys[i].c_str());
    return result;
}

// === Logging ===

static std::atomic<int> liva_log_level_{0}; // 0=debug, 1=info, 2=warn, 3=error
static std::mutex liva_log_mutex_;

static void liva_log_print(int level, const char *prefix, const char *msg) {
    if (level < liva_log_level_.load(std::memory_order_relaxed) || !msg) return;
    std::lock_guard<std::mutex> lock(liva_log_mutex_);
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char timebuf[20];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);
    fprintf(stderr, "[%s %s] %s\n", timebuf, prefix, msg);
}

void liva_log_debug(const char *msg) { liva_log_print(0, "DEBUG", msg); }
void liva_log_info(const char *msg)  { liva_log_print(1, "INFO",  msg); }
void liva_log_warn(const char *msg)  { liva_log_print(2, "WARN",  msg); }
void liva_log_error(const char *msg) { liva_log_print(3, "ERROR", msg); }

void liva_log_set_level(int32_t level) {
    liva_log_level_.store((level < 0) ? 0 : (level > 3) ? 3 : level,
                          std::memory_order_relaxed);
}

// === Testing ===

void liva_assert(int8_t condition) {
    if (!condition) {
        fprintf(stderr, "ASSERTION FAILED\n");
        abort();
    }
}

void liva_assert_msg(int8_t condition, const char *msg) {
    if (!condition) {
        fprintf(stderr, "ASSERTION FAILED: %s\n", msg ? msg : "(no message)");
        abort();
    }
}

void liva_assert_eq(int64_t a, int64_t b) {
    if (a != b) {
        fprintf(stderr, "ASSERTION FAILED: expected %lld == %lld\n",
                (long long)a, (long long)b);
        abort();
    }
}

void liva_assert_eq_str(const char *a, const char *b) {
    if (!a && !b) return;
    if (!a || !b || strcmp(a, b) != 0) {
        fprintf(stderr, "ASSERTION FAILED: expected \"%s\" == \"%s\"\n",
                a ? a : "(null)", b ? b : "(null)");
        abort();
    }
}

void liva_assert_eq_float(double a, double b) {
    double diff = a - b;
    if (diff < 0) diff = -diff;
    if (diff > 1e-9) {
        fprintf(stderr, "ASSERTION FAILED: expected %g == %g\n", a, b);
        abort();
    }
}

// === DateTime ===

static struct tm *liva_localtime(double timestamp) {
    time_t t = (time_t)timestamp;
    return localtime(&t);
}

char *liva_date_now() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char *buf = (char *)malloc(11);
    if (buf) strftime(buf, 11, "%Y-%m-%d", t);
    return buf;
}

char *liva_time_now() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char *buf = (char *)malloc(9);
    if (buf) strftime(buf, 9, "%H:%M:%S", t);
    return buf;
}

char *liva_datetime_now() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char *buf = (char *)malloc(20);
    if (buf) strftime(buf, 20, "%Y-%m-%d %H:%M:%S", t);
    return buf;
}

char *liva_date_format(double timestamp, const char *fmt) {
    if (!fmt) return nullptr;
    struct tm *t = liva_localtime(timestamp);
    if (!t) return nullptr;
    char *buf = (char *)malloc(256);
    if (buf) {
        size_t n = strftime(buf, 256, fmt, t);
        buf[n] = '\0';
    }
    return buf;
}

int32_t liva_date_year(double timestamp)    { struct tm *t = liva_localtime(timestamp); return t ? t->tm_year + 1900 : 0; }
int32_t liva_date_month(double timestamp)   { struct tm *t = liva_localtime(timestamp); return t ? t->tm_mon + 1 : 0; }
int32_t liva_date_day(double timestamp)     { struct tm *t = liva_localtime(timestamp); return t ? t->tm_mday : 0; }
int32_t liva_date_weekday(double timestamp) { struct tm *t = liva_localtime(timestamp); return t ? t->tm_wday : 0; }

// === Encoding / Compression ===

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *liva_base64_encode(const char *data) {
    if (!data) return nullptr;
    size_t len = strlen(data);
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    if (!out) return nullptr;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t a = (uint8_t)data[i];
        uint32_t b = (i + 1 < len) ? (uint8_t)data[i + 1] : 0;
        uint32_t c = (i + 2 < len) ? (uint8_t)data[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return out;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

char *liva_base64_decode(const char *data) {
    if (!data) return nullptr;
    size_t len = strlen(data);
    if (len % 4 != 0) return nullptr;
    size_t out_len = len / 4 * 3;
    if (len > 0 && data[len - 1] == '=') out_len--;
    if (len > 1 && data[len - 2] == '=') out_len--;
    char *out = (char *)malloc(out_len + 1);
    if (!out) return nullptr;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        int a = b64_decode_char(data[i]);
        int b = b64_decode_char(data[i + 1]);
        int c = (data[i + 2] == '=') ? 0 : b64_decode_char(data[i + 2]);
        int d = (data[i + 3] == '=') ? 0 : b64_decode_char(data[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) { free(out); return nullptr; }
        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        if (j < out_len) out[j++] = (char)((triple >> 16) & 0xFF);
        if (j < out_len) out[j++] = (char)((triple >> 8) & 0xFF);
        if (j < out_len) out[j++] = (char)(triple & 0xFF);
    }
    out[j] = '\0';
    return out;
}

char *liva_hex_encode(const char *data) {
    if (!data) return nullptr;
    size_t len = strlen(data);
    char *out = (char *)malloc(len * 2 + 1);
    if (!out) return nullptr;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[((uint8_t)data[i]) >> 4];
        out[i * 2 + 1] = hex[((uint8_t)data[i]) & 0xF];
    }
    out[len * 2] = '\0';
    return out;
}

static int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char *liva_hex_decode(const char *data) {
    if (!data) return nullptr;
    size_t len = strlen(data);
    if (len % 2 != 0) return nullptr;
    size_t out_len = len / 2;
    char *out = (char *)malloc(out_len + 1);
    if (!out) return nullptr;
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_char_val(data[i * 2]);
        int lo = hex_char_val(data[i * 2 + 1]);
        if (hi < 0 || lo < 0) { free(out); return nullptr; }
        out[i] = (char)((hi << 4) | lo);
    }
    out[out_len] = '\0';
    return out;
}

int64_t liva_crc32(const char *data) {
    if (!data) return 0;
    // Standard CRC-32 with reflected polynomial 0xEDB88320
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; data[i]; i++) {
        crc ^= (uint8_t)data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return (int64_t)(crc ^ 0xFFFFFFFF);
}

// === Synchronization: Mutex ===

int64_t liva_mutex_create() {
    auto *mtx = new (std::nothrow) std::mutex();
    if (!mtx) return 0;
    return (int64_t)(intptr_t)mtx;
}

void liva_mutex_lock(int64_t handle) {
    if (!handle) return;
    auto *mtx = (std::mutex *)(intptr_t)handle;
    mtx->lock();
}

void liva_mutex_unlock(int64_t handle) {
    if (!handle) return;
    auto *mtx = (std::mutex *)(intptr_t)handle;
    mtx->unlock();
}

int8_t liva_mutex_try_lock(int64_t handle) {
    if (!handle) return 0;
    auto *mtx = (std::mutex *)(intptr_t)handle;
    return mtx->try_lock() ? 1 : 0;
}

void liva_mutex_free(int64_t handle) {
    if (!handle) return;
    auto *mtx = (std::mutex *)(intptr_t)handle;
    delete mtx;
}

// === Synchronization: Atomic i64 ===

int64_t liva_atomic_create(int64_t initial) {
    auto *val = new (std::nothrow) std::atomic<int64_t>(initial);
    if (!val) return 0;
    return (int64_t)(intptr_t)val;
}

int64_t liva_atomic_load(int64_t handle) {
    if (!handle) return 0;
    auto *val = (std::atomic<int64_t> *)(intptr_t)handle;
    return val->load(std::memory_order_seq_cst);
}

void liva_atomic_store(int64_t handle, int64_t value) {
    if (!handle) return;
    auto *val = (std::atomic<int64_t> *)(intptr_t)handle;
    val->store(value, std::memory_order_seq_cst);
}

int64_t liva_atomic_add(int64_t handle, int64_t value) {
    if (!handle) return 0;
    auto *val = (std::atomic<int64_t> *)(intptr_t)handle;
    return val->fetch_add(value, std::memory_order_seq_cst);
}

int64_t liva_atomic_sub(int64_t handle, int64_t value) {
    if (!handle) return 0;
    auto *val = (std::atomic<int64_t> *)(intptr_t)handle;
    return val->fetch_sub(value, std::memory_order_seq_cst);
}

int8_t liva_atomic_cas(int64_t handle, int64_t expected, int64_t desired) {
    if (!handle) return 0;
    auto *val = (std::atomic<int64_t> *)(intptr_t)handle;
    return val->compare_exchange_strong(expected, desired,
                                         std::memory_order_seq_cst) ? 1 : 0;
}

void liva_atomic_free(int64_t handle) {
    if (!handle) return;
    auto *val = (std::atomic<int64_t> *)(intptr_t)handle;
    delete val;
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
