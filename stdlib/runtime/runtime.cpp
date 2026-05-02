#include "runtime.h"
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <csetjmp>
#include <ctime>
#include <cmath>
#include <condition_variable>
#include <shared_mutex>
#include <deque>
#include <future>
#include <mutex>
#include <regex>
#include <thread>
#include <string>
#include <unordered_map>
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

char *liva_str_dup(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    char *result = (char *)malloc(len + 1);
    if (!result) liva_panic("out of memory");
    memcpy(result, s, len + 1);
    return result;
}

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

// Lexicographic compare: <0 if a<b, 0 if equal, >0 if a>b. Used for
// ordering operators (<, <=, >, >=) on string operands. nil sorts first.
int32_t liva_str_compare(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return strcmp(a, b);
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

char *liva_char_to_str(int32_t codepoint) {
    // Encode a Unicode code point as UTF-8
    char *buf = (char *)malloc(5); // max 4 bytes + null
    if (!buf) liva_panic("out of memory");
    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        buf[1] = '\0';
    } else if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        buf[2] = '\0';
    } else if (codepoint < 0x10000) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        buf[3] = '\0';
    } else {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        buf[4] = '\0';
    }
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
    errno = 0;
    long long val = strtoll(str, &end, 10);
    if (end == str || *end != '\0') return 0;
    if (errno == ERANGE) return 0;
    *result = (int64_t)val;
    return 1;
}

int8_t liva_str_parse_f64(const char *str, double *result) {
    if (!str || !*str) return 0;
    char *end = nullptr;
    errno = 0;
    double val = strtod(str, &end);
    if (end == str || *end != '\0') return 0;
    if (errno == ERANGE) return 0;
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

int8_t liva_path_is_dir(const char *path) {
    if (!path) return 0;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
#endif
}

int64_t liva_path_size(const char *path) {
    if (!path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

int64_t liva_path_modified_time(const char *path) {
    if (!path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_mtime;
}

// fileRead(path) -> Optional<string> (null on error)
char *liva_file_read(const char *path) {
    if (!path) return nullptr;
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return nullptr; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

// fileWrite(path, content) -> bool
int8_t liva_file_write_path(const char *path, const char *content) {
    if (!path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (content) fwrite(content, 1, strlen(content), f);
    fclose(f);
    return 1;
}

// fileAppend(path, content) -> bool
int8_t liva_file_append(const char *path, const char *content) {
    if (!path) return 0;
    FILE *f = fopen(path, "ab");
    if (!f) return 0;
    if (content) fwrite(content, 1, strlen(content), f);
    fclose(f);
    return 1;
}

// fileRemove(path) -> bool
int8_t liva_file_remove(const char *path) {
    if (!path) return 0;
    return (remove(path) == 0) ? 1 : 0;
}

// fileCopy(src, dst) -> bool
int8_t liva_file_copy(const char *src, const char *dst) {
    if (!src || !dst) return 0;
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return 0; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in); fclose(out); return 0;
        }
    }
    fclose(in);
    fclose(out);
    return 1;
}

// pathAbsolute(path) -> string
char *liva_path_absolute(const char *path) {
    if (!path || !*path) return strdup_safe(".");
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetFullPathNameA(path, MAX_PATH, buf, nullptr);
    if (len == 0 || len >= MAX_PATH) return strdup_safe(path);
    return strdup_safe(buf);
#else
    char *resolved = realpath(path, nullptr);
    if (resolved) return resolved;
    return strdup_safe(path);
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
    // Overflow check — work in unsigned size_t. The previous version
    // cast `SIZE_MAX / elem_size` back to int64_t, which wraps to -1
    // (and other negatives) for elem_size ≤ 2 since SIZE_MAX exceeds
    // INT64_MAX. That made every [u8]/[i8]/[bool]/[i16]/[u16] array
    // allocation panic with "array allocation overflow".
    if ((size_t)capacity > SIZE_MAX / (size_t)elem_size)
        liva_panic("array allocation overflow");
    size_t bytes = (size_t)elem_size * (size_t)capacity;
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
        // Compare in unsigned size_t — see liva_array_new for the
        // signed-cast trap that bites elem_size ≤ 2.
        if ((size_t)new_cap > SIZE_MAX / (size_t)elem_size)
            liva_panic("array push overflow");
        void *new_data = realloc(*data_ptr,
                                 (size_t)new_cap * (size_t)elem_size);
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

void liva_rand_seed(int64_t seed) {
    std::call_once(rand_once_flag, []() {}); // prevent later auto-seed overriding
    srand((unsigned)seed);
}

int64_t liva_rand_i64() {
    ensure_rand_seeded();
    // Compose two 32-bit rand() calls into one i64
    int64_t hi = (int64_t)rand();
    int64_t lo = (int64_t)rand();
    return (hi << 32) ^ lo;
}

char *liva_rand_uuid() {
    ensure_rand_seeded();
    // UUID v4: 36 chars + NUL = 37
    char *buf = (char *)malloc(37);
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            buf[i] = '-';
        } else if (i == 14) {
            buf[i] = '4'; // version
        } else if (i == 19) {
            int r = rand() & 0x3;
            buf[i] = hex[0x8 | r]; // variant
        } else {
            buf[i] = hex[rand() & 0xF];
        }
    }
    buf[36] = '\0';
    return buf;
}

// UUID v7 (RFC 9562): 48-bit unix_ts_ms || ver=7 || 12-bit rand_a ||
// var=0b10 || 62-bit rand_b. Time-ordered, monotonic when sorted as
// strings — useful as DB primary keys.
char *liva_rand_uuid_v7() {
    ensure_rand_seeded();
    uint8_t b[16];

    // Bytes 0-5: unix milliseconds, big-endian.
    uint64_t ms = (uint64_t)liva_clock_ms();
    b[0] = (uint8_t)(ms >> 40);
    b[1] = (uint8_t)(ms >> 32);
    b[2] = (uint8_t)(ms >> 24);
    b[3] = (uint8_t)(ms >> 16);
    b[4] = (uint8_t)(ms >> 8);
    b[5] = (uint8_t)(ms);

    // Bytes 6-15: random with version + variant fields overlaid.
    for (int i = 6; i < 16; ++i) b[i] = (uint8_t)(rand() & 0xFF);
    b[6] = (uint8_t)((b[6] & 0x0F) | 0x70); // version 7 in high nibble
    b[8] = (uint8_t)((b[8] & 0x3F) | 0x80); // variant 0b10 in top 2 bits

    char *buf = (char *)malloc(37);
    if (!buf) liva_panic("out of memory");
    static const char hex[] = "0123456789abcdef";
    int j = 0;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) buf[j++] = '-';
        buf[j++] = hex[(b[i] >> 4) & 0xF];
        buf[j++] = hex[b[i] & 0xF];
    }
    buf[36] = '\0';
    return buf;
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

char **liva_regex_split(const char *str, const char *pattern, int64_t *count) {
    *count = 0;
    if (!str || !pattern) return nullptr;
    try {
        std::regex re(pattern);
        std::string s(str);
        std::vector<std::string> parts;
        std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
        std::sregex_token_iterator end;
        while (it != end) {
            parts.push_back(it->str());
            ++it;
        }
        *count = (int64_t)parts.size();
        if (parts.empty()) return nullptr;
        char **result = (char **)malloc(parts.size() * sizeof(char *));
        for (size_t i = 0; i < parts.size(); i++) {
            result[i] = (char *)malloc(parts[i].size() + 1);
            memcpy(result[i], parts[i].c_str(), parts[i].size() + 1);
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

// === Handle-based public API (i64 handle — exposed to Liva userland) ===

int64_t liva_http_req(const char *method, const char *url,
                      const char *body, int64_t timeout_ms) {
    // Allow nullptr body for GET/DELETE; no extra headers from userland for now.
    auto *resp = liva_http_request(method, url, body,
                                   /*headers=*/nullptr, /*header_count=*/0,
                                   timeout_ms);
    return (int64_t)(uintptr_t)resp;
}

int32_t liva_http_req_status(int64_t handle) {
    auto *resp = (LivaHttpResponse *)(uintptr_t)handle;
    return liva_http_response_status(resp);
}

char *liva_http_req_body(int64_t handle) {
    auto *resp = (LivaHttpResponse *)(uintptr_t)handle;
    return liva_http_response_body(resp);
}

char *liva_http_req_header(int64_t handle, const char *name) {
    auto *resp = (LivaHttpResponse *)(uintptr_t)handle;
    return liva_http_response_header(resp, name);
}

void liva_http_req_close(int64_t handle) {
    auto *resp = (LivaHttpResponse *)(uintptr_t)handle;
    liva_http_response_free(resp);
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

// === WebSocket (WinHTTP-based on Windows; stub elsewhere) ===

struct LivaWebSocket {
#ifdef _WIN32
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hWebSocket;
#endif
    int open;
};

#ifdef _WIN32
// Parse ws://host[:port]/path or wss://host[:port]/path
static bool parse_ws_url(const char *url, std::wstring &host, std::wstring &path,
                          int &port, bool &secure) {
    std::string u(url);
    secure = false;
    port = 80;
    size_t proto_end = u.find("://");
    if (proto_end == std::string::npos) return false;
    std::string proto = u.substr(0, proto_end);
    if (proto == "wss") { secure = true; port = 443; }
    else if (proto != "ws") return false;
    u = u.substr(proto_end + 3);
    size_t slash = u.find('/');
    std::string h = (slash != std::string::npos) ? u.substr(0, slash) : u;
    std::string p = (slash != std::string::npos) ? u.substr(slash) : "/";
    size_t colon = h.find(':');
    if (colon != std::string::npos) {
        port = atoi(h.substr(colon + 1).c_str());
        h = h.substr(0, colon);
    }
    host.assign(h.begin(), h.end());
    path.assign(p.begin(), p.end());
    return !host.empty();
}
#endif

int64_t liva_ws_connect(const char *url) {
    if (!url) return 0;
#ifdef _WIN32
    std::wstring host, path;
    int port = 0;
    bool secure = false;
    if (!parse_ws_url(url, host, path, port, secure)) return 0;

    HINTERNET hSession = WinHttpOpen(L"Liva-WS/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return 0;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return 0; }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                           nullptr, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 101) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    HINTERNET hWs = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
    WinHttpCloseHandle(hRequest);
    if (!hWs) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    LivaWebSocket *ws = new LivaWebSocket();
    ws->hSession = hSession;
    ws->hConnect = hConnect;
    ws->hWebSocket = hWs;
    ws->open = 1;
    return (int64_t)(uintptr_t)ws;
#else
    (void)url;
    return 0;
#endif
}

int32_t liva_ws_send_text(int64_t handle, const char *msg) {
    if (!handle || !msg) return -1;
#ifdef _WIN32
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    if (!ws->open) return -1;
    DWORD len = (DWORD)strlen(msg);
    DWORD err = WinHttpWebSocketSend(ws->hWebSocket,
                                     WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                     (PVOID)msg, len);
    return err == NO_ERROR ? 0 : -1;
#else
    (void)handle;
    (void)msg;
    return -1;
#endif
}

char *liva_ws_recv_text(int64_t handle) {
    if (!handle) return nullptr;
#ifdef _WIN32
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    if (!ws->open) return nullptr;

    std::string accum;
    char buf[4096];
    for (;;) {
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType;
        DWORD err = WinHttpWebSocketReceive(ws->hWebSocket, buf, sizeof(buf),
                                            &bytesRead, &bufType);
        if (err != NO_ERROR) {
            ws->open = 0;
            return nullptr;
        }
        if (bufType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            ws->open = 0;
            return nullptr;
        }
        accum.append(buf, bytesRead);
        // Final fragment of either a UTF-8 or binary message ends the read.
        if (bufType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            bufType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            char *out = (char *)malloc(accum.size() + 1);
            if (!out) return nullptr;
            memcpy(out, accum.data(), accum.size());
            out[accum.size()] = '\0';
            return out;
        }
        // FRAGMENT_BUFFER_TYPE: continue accumulating.
    }
#else
    (void)handle;
    return nullptr;
#endif
}

int32_t liva_ws_close(int64_t handle, int32_t status, const char *reason) {
    if (!handle) return -1;
#ifdef _WIN32
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    if (ws->open) {
        const char *r = reason ? reason : "";
        WinHttpWebSocketClose(ws->hWebSocket, (USHORT)status, (PVOID)r,
                              (DWORD)strlen(r));
        ws->open = 0;
    }
    if (ws->hWebSocket) WinHttpCloseHandle(ws->hWebSocket);
    if (ws->hConnect) WinHttpCloseHandle(ws->hConnect);
    if (ws->hSession) WinHttpCloseHandle(ws->hSession);
    delete ws;
    return 0;
#else
    (void)handle;
    (void)status;
    (void)reason;
    return -1;
#endif
}

int32_t liva_ws_is_open(int64_t handle) {
    if (!handle) return 0;
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    return ws->open ? 1 : 0;
}

// === SQLite (Windows: dynamic-loaded winsqlite3.dll; POSIX: link libsqlite3) ===
//
// Windows 10+ ships winsqlite3.dll in System32 — we pull it in via
// LoadLibrary/GetProcAddress so the runtime has no link-time dependency
// there. On Linux/macOS, CMake's find_package(SQLite3) wires in
// libsqlite3 when present and defines LIVA_HAS_POSIX_SQLITE; entries
// then resolve to the directly-linked symbols.
//
// Either way the code below funnels everything through a SqliteApi
// dispatch table. If neither path is available, every entry point
// treats the call as "feature unavailable" (returns 0/nullptr/false).

#if defined(_WIN32) || defined(LIVA_HAS_POSIX_SQLITE)
#define LIVA_HAS_SQLITE 1
#endif

#ifdef LIVA_HAS_POSIX_SQLITE
#include <sqlite3.h>
#endif

#ifdef LIVA_HAS_SQLITE
namespace {

// SQLite result codes used here.
constexpr int LIVA_SQLITE_OK = 0;
constexpr int LIVA_SQLITE_ROW = 100;
constexpr int LIVA_SQLITE_DONE = 101;

struct SqliteApi {
#ifdef _WIN32
    HMODULE dll = nullptr;
#endif

    int  (*open)(const char *, void **) = nullptr;
    int  (*close)(void *) = nullptr;
    int  (*exec)(void *, const char *, int (*)(void *, int, char **, char **),
                 void *, char **) = nullptr;
    int  (*prepare_v2)(void *, const char *, int, void **, const char **) = nullptr;
    int  (*step)(void *) = nullptr;
    int  (*reset)(void *) = nullptr;
    int  (*finalize)(void *) = nullptr;
    int  (*column_count)(void *) = nullptr;
    const unsigned char *(*column_text)(void *, int) = nullptr;
    long long (*column_int64)(void *, int) = nullptr;
    double (*column_double)(void *, int) = nullptr;
    int  (*bind_text)(void *, int, const char *, int, void (*)(void *)) = nullptr;
    int  (*bind_int64)(void *, int, long long) = nullptr;
    int  (*bind_double)(void *, int, double) = nullptr;
    int  (*bind_null)(void *, int) = nullptr;
    long long (*last_insert_rowid)(void *) = nullptr;
    int  (*changes)(void *) = nullptr;
    const char *(*errmsg)(void *) = nullptr;
    void (*free)(void *) = nullptr;
};

static SqliteApi &sqlite_api() {
    static SqliteApi api;
    static std::once_flag flag;
    std::call_once(flag, [&]() {
#ifdef _WIN32
        api.dll = LoadLibraryA("winsqlite3.dll");
        if (!api.dll) return;
        auto resolve = [&](const char *name) -> FARPROC {
            return GetProcAddress(api.dll, name);
        };
        api.open = (decltype(api.open))resolve("sqlite3_open");
        api.close = (decltype(api.close))resolve("sqlite3_close");
        api.exec = (decltype(api.exec))resolve("sqlite3_exec");
        api.prepare_v2 = (decltype(api.prepare_v2))resolve("sqlite3_prepare_v2");
        api.step = (decltype(api.step))resolve("sqlite3_step");
        api.reset = (decltype(api.reset))resolve("sqlite3_reset");
        api.finalize = (decltype(api.finalize))resolve("sqlite3_finalize");
        api.column_count = (decltype(api.column_count))resolve("sqlite3_column_count");
        api.column_text = (decltype(api.column_text))resolve("sqlite3_column_text");
        api.column_int64 = (decltype(api.column_int64))resolve("sqlite3_column_int64");
        api.column_double = (decltype(api.column_double))resolve("sqlite3_column_double");
        api.bind_text = (decltype(api.bind_text))resolve("sqlite3_bind_text");
        api.bind_int64 = (decltype(api.bind_int64))resolve("sqlite3_bind_int64");
        api.bind_double = (decltype(api.bind_double))resolve("sqlite3_bind_double");
        api.bind_null = (decltype(api.bind_null))resolve("sqlite3_bind_null");
        api.last_insert_rowid = (decltype(api.last_insert_rowid))resolve("sqlite3_last_insert_rowid");
        api.changes = (decltype(api.changes))resolve("sqlite3_changes");
        api.errmsg = (decltype(api.errmsg))resolve("sqlite3_errmsg");
        api.free = (decltype(api.free))resolve("sqlite3_free");
#elif defined(LIVA_HAS_POSIX_SQLITE)
        // Direct binding to libsqlite3 — the SqliteApi pointer types
        // use opaque void* in place of sqlite3*/sqlite3_stmt*; the
        // ABI is identical, so reinterpret_cast through the fn type.
        api.open = reinterpret_cast<decltype(api.open)>(&sqlite3_open);
        api.close = reinterpret_cast<decltype(api.close)>(&sqlite3_close);
        api.exec = reinterpret_cast<decltype(api.exec)>(&sqlite3_exec);
        api.prepare_v2 = reinterpret_cast<decltype(api.prepare_v2)>(&sqlite3_prepare_v2);
        api.step = reinterpret_cast<decltype(api.step)>(&sqlite3_step);
        api.reset = reinterpret_cast<decltype(api.reset)>(&sqlite3_reset);
        api.finalize = reinterpret_cast<decltype(api.finalize)>(&sqlite3_finalize);
        api.column_count = reinterpret_cast<decltype(api.column_count)>(&sqlite3_column_count);
        api.column_text = reinterpret_cast<decltype(api.column_text)>(&sqlite3_column_text);
        api.column_int64 = reinterpret_cast<decltype(api.column_int64)>(&sqlite3_column_int64);
        api.column_double = reinterpret_cast<decltype(api.column_double)>(&sqlite3_column_double);
        api.bind_text = reinterpret_cast<decltype(api.bind_text)>(&sqlite3_bind_text);
        api.bind_int64 = reinterpret_cast<decltype(api.bind_int64)>(&sqlite3_bind_int64);
        api.bind_double = reinterpret_cast<decltype(api.bind_double)>(&sqlite3_bind_double);
        api.bind_null = reinterpret_cast<decltype(api.bind_null)>(&sqlite3_bind_null);
        api.last_insert_rowid = reinterpret_cast<decltype(api.last_insert_rowid)>(&sqlite3_last_insert_rowid);
        api.changes = reinterpret_cast<decltype(api.changes)>(&sqlite3_changes);
        api.errmsg = reinterpret_cast<decltype(api.errmsg)>(&sqlite3_errmsg);
        api.free = reinterpret_cast<decltype(api.free)>(&sqlite3_free);
#endif
    });
    return api;
}

} // namespace
#endif // LIVA_HAS_SQLITE

int64_t liva_sqlite_open(const char *path) {
    if (!path) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.open) return 0;
    void *db = nullptr;
    if (api.open(path, &db) != LIVA_SQLITE_OK) {
        if (db && api.close) api.close(db);
        return 0;
    }
    return (int64_t)(uintptr_t)db;
#else
    (void)path;
    return 0;
#endif
}

void liva_sqlite_close(int64_t handle) {
    if (!handle) return;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (api.close) api.close((void *)(uintptr_t)handle);
#else
    (void)handle;
#endif
}

int32_t liva_sqlite_exec(int64_t handle, const char *sql) {
    if (!handle || !sql) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.exec) return -1;
    char *errmsg = nullptr;
    int rc = api.exec((void *)(uintptr_t)handle, sql, nullptr, nullptr, &errmsg);
    if (errmsg && api.free) api.free(errmsg);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)handle;
    (void)sql;
    return -1;
#endif
}

// First column of the first row as a freshly malloc'd string. Returns nullptr
// if the query produced no rows or failed.
char *liva_sqlite_query_first(int64_t handle, const char *sql) {
    if (!handle || !sql) return nullptr;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.prepare_v2 || !api.step || !api.finalize || !api.column_text)
        return nullptr;
    void *stmt = nullptr;
    if (api.prepare_v2((void *)(uintptr_t)handle, sql, -1, &stmt, nullptr)
            != LIVA_SQLITE_OK || !stmt) {
        if (stmt) api.finalize(stmt);
        return nullptr;
    }
    char *out = nullptr;
    if (api.step(stmt) == LIVA_SQLITE_ROW) {
        const unsigned char *txt = api.column_text(stmt, 0);
        out = strdup_safe(txt ? (const char *)txt : "");
    }
    api.finalize(stmt);
    return out;
#else
    (void)handle;
    (void)sql;
    return nullptr;
#endif
}

// First column of the first row as i64. *ok set to 1 on success, 0 otherwise.
int64_t liva_sqlite_query_int(int64_t handle, const char *sql, int32_t *ok) {
    if (ok) *ok = 0;
    if (!handle || !sql) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.prepare_v2 || !api.step || !api.finalize || !api.column_int64)
        return 0;
    void *stmt = nullptr;
    if (api.prepare_v2((void *)(uintptr_t)handle, sql, -1, &stmt, nullptr)
            != LIVA_SQLITE_OK || !stmt) {
        if (stmt) api.finalize(stmt);
        return 0;
    }
    int64_t result = 0;
    if (api.step(stmt) == LIVA_SQLITE_ROW) {
        result = (int64_t)api.column_int64(stmt, 0);
        if (ok) *ok = 1;
    }
    api.finalize(stmt);
    return result;
#else
    (void)handle;
    (void)sql;
    return 0;
#endif
}

// All rows, first column each, joined with '\n'. Returns malloc'd string
// (empty if no rows). Useful for quick sanity checks; use prepare/step
// for richer iteration once that API lands.
char *liva_sqlite_query_all_first_col(int64_t handle, const char *sql) {
    if (!handle || !sql) return nullptr;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.prepare_v2 || !api.step || !api.finalize || !api.column_text)
        return nullptr;
    void *stmt = nullptr;
    if (api.prepare_v2((void *)(uintptr_t)handle, sql, -1, &stmt, nullptr)
            != LIVA_SQLITE_OK || !stmt) {
        if (stmt) api.finalize(stmt);
        return nullptr;
    }
    std::string accum;
    bool first = true;
    while (api.step(stmt) == LIVA_SQLITE_ROW) {
        if (!first) accum.push_back('\n');
        first = false;
        const unsigned char *txt = api.column_text(stmt, 0);
        if (txt) accum.append((const char *)txt);
    }
    api.finalize(stmt);
    char *out = (char *)malloc(accum.size() + 1);
    if (!out) return nullptr;
    memcpy(out, accum.data(), accum.size());
    out[accum.size()] = '\0';
    return out;
#else
    (void)handle;
    (void)sql;
    return nullptr;
#endif
}

int64_t liva_sqlite_last_insert_rowid(int64_t handle) {
    if (!handle) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.last_insert_rowid) return 0;
    return (int64_t)api.last_insert_rowid((void *)(uintptr_t)handle);
#else
    (void)handle;
    return 0;
#endif
}

int32_t liva_sqlite_changes(int64_t handle) {
    if (!handle) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.changes) return 0;
    return (int32_t)api.changes((void *)(uintptr_t)handle);
#else
    (void)handle;
    return 0;
#endif
}

char *liva_sqlite_errmsg(int64_t handle) {
    if (!handle) return nullptr;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.errmsg) return nullptr;
    const char *m = api.errmsg((void *)(uintptr_t)handle);
    return strdup_safe(m ? m : "");
#else
    (void)handle;
    return nullptr;
#endif
}

// === SQLite prepared statements ===
//
// Lifecycle: prepare → bind* (1-indexed) → step (loops while it returns 1) →
// optional reset for re-execution → finalize.
//
// Step return code: 1 = row, 2 = done, 0 = error. The 1/2 split lets the
// wrapper expose `step() -> bool` (true means a row is available) while
// still letting userland tell errors apart from end-of-rows via errmsg.

int64_t liva_sqlite_prepare(int64_t db, const char *sql) {
    if (!db || !sql) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.prepare_v2) return 0;
    void *stmt = nullptr;
    if (api.prepare_v2((void *)(uintptr_t)db, sql, -1, &stmt, nullptr)
            != LIVA_SQLITE_OK || !stmt) {
        if (stmt && api.finalize) api.finalize(stmt);
        return 0;
    }
    return (int64_t)(uintptr_t)stmt;
#else
    (void)db;
    (void)sql;
    return 0;
#endif
}

int32_t liva_sqlite_bind_text(int64_t stmt, int32_t idx, const char *val) {
    if (!stmt) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.bind_text) return -1;
    // SQLITE_TRANSIENT = (void(*)(void*))-1 — tells SQLite to copy the
    // string; safe regardless of the caller's lifetime semantics.
    auto *transient = (void (*)(void *))(intptr_t)-1;
    int rc = api.bind_text((void *)(uintptr_t)stmt, idx,
                            val ? val : "",
                            val ? (int)strlen(val) : 0, transient);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)stmt;
    (void)idx;
    (void)val;
    return -1;
#endif
}

int32_t liva_sqlite_bind_int(int64_t stmt, int32_t idx, int64_t val) {
    if (!stmt) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.bind_int64) return -1;
    int rc = api.bind_int64((void *)(uintptr_t)stmt, idx, (long long)val);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)stmt;
    (void)idx;
    (void)val;
    return -1;
#endif
}

int32_t liva_sqlite_bind_double(int64_t stmt, int32_t idx, double val) {
    if (!stmt) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.bind_double) return -1;
    int rc = api.bind_double((void *)(uintptr_t)stmt, idx, val);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)stmt;
    (void)idx;
    (void)val;
    return -1;
#endif
}

int32_t liva_sqlite_bind_null(int64_t stmt, int32_t idx) {
    if (!stmt) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.bind_null) return -1;
    int rc = api.bind_null((void *)(uintptr_t)stmt, idx);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)stmt;
    (void)idx;
    return -1;
#endif
}

// 1 = row, 2 = done, 0 = error
int32_t liva_sqlite_step(int64_t stmt) {
    if (!stmt) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.step) return 0;
    int rc = api.step((void *)(uintptr_t)stmt);
    if (rc == LIVA_SQLITE_ROW) return 1;
    if (rc == LIVA_SQLITE_DONE) return 2;
    return 0;
#else
    (void)stmt;
    return 0;
#endif
}

int32_t liva_sqlite_reset(int64_t stmt) {
    if (!stmt) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.reset) return -1;
    int rc = api.reset((void *)(uintptr_t)stmt);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)stmt;
    return -1;
#endif
}

int32_t liva_sqlite_column_count(int64_t stmt) {
    if (!stmt) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.column_count) return 0;
    return (int32_t)api.column_count((void *)(uintptr_t)stmt);
#else
    (void)stmt;
    return 0;
#endif
}

char *liva_sqlite_column_text(int64_t stmt, int32_t col) {
    if (!stmt) return nullptr;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.column_text) return nullptr;
    const unsigned char *t = api.column_text((void *)(uintptr_t)stmt, col);
    return strdup_safe(t ? (const char *)t : "");
#else
    (void)stmt;
    (void)col;
    return nullptr;
#endif
}

int64_t liva_sqlite_column_int(int64_t stmt, int32_t col) {
    if (!stmt) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.column_int64) return 0;
    return (int64_t)api.column_int64((void *)(uintptr_t)stmt, col);
#else
    (void)stmt;
    (void)col;
    return 0;
#endif
}

double liva_sqlite_column_double(int64_t stmt, int32_t col) {
    if (!stmt) return 0.0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.column_double) return 0.0;
    return api.column_double((void *)(uintptr_t)stmt, col);
#else
    (void)stmt;
    (void)col;
    return 0.0;
#endif
}

void liva_sqlite_finalize(int64_t stmt) {
    if (!stmt) return;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (api.finalize) api.finalize((void *)(uintptr_t)stmt);
#else
    (void)stmt;
#endif
}

// === Async/Coroutine Runtime ===

// --- Dynamic ready queue (resizable circular buffer) ---

static std::mutex async_mutex_;  // protects ready_queue + timer_heap
static std::condition_variable global_task_cv_;   // wakes awaitAll/select waiters
static std::mutex global_task_cv_mtx_;

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
    task->cancelled = 0;
    task->children = nullptr;
    task->child_count = 0;
    task->child_capacity = 0;
    task->worker_id = -1;
    return task;
}

void liva_task_complete(LivaTask *task) {
    task->done = 1;
    if (task->parent) {
        std::lock_guard<std::mutex> lock(async_mutex_);
        ready_push(task->parent);
    }
    global_task_cv_.notify_all();
}

int8_t liva_task_is_done(LivaTask *task) {
    return task->done;
}

void *liva_task_get_handle(LivaTask *task) {
    return task->handle;
}

void liva_task_set_parent(LivaTask *child, LivaTask *parent) {
    child->parent = parent;
    if (parent) {
        if (parent->child_count >= parent->child_capacity) {
            parent->child_capacity = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
            parent->children = (LivaTask **)realloc(parent->children,
                (size_t)parent->child_capacity * sizeof(LivaTask *));
        }
        parent->children[parent->child_count++] = child;
    }
}

void liva_task_destroy(LivaTask *task) {
    free(task->children);
    free(task);
}

void liva_task_cancel(LivaTask *task) {
    if (!task || task->cancelled) return;
    task->cancelled = 1;
    // Recursively cancel children
    for (int64_t i = 0; i < task->child_count; i++) {
        if (task->children[i]) liva_task_cancel(task->children[i]);
    }
}

int8_t liva_task_is_cancelled(LivaTask *task) {
    return task->cancelled;
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
            if (task->cancelled) {
                task->done = 1;
                if (task->parent) {
                    ready_push(task->parent);
                }
                continue;
            }
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

// === Thread Pool Scheduler ===

struct WorkerThread {
    std::thread thread;
    std::deque<LivaTask *> local_queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool running = false;
    int32_t id = -1;
};

static WorkerThread *workers_ = nullptr;
static int32_t worker_count_ = 0;
static bool scheduler_running_ = false;
static std::mutex scheduler_mtx_;
static std::condition_variable scheduler_cv_;

// Global overflow queue for work-stealing
static std::deque<LivaTask *> global_work_queue_;
static std::mutex global_work_mtx_;

static void worker_loop(WorkerThread *w) {
    while (w->running) {
        LivaTask *task = nullptr;

        // 1. Try local queue
        {
            std::lock_guard<std::mutex> lock(w->mtx);
            if (!w->local_queue.empty()) {
                task = w->local_queue.front();
                w->local_queue.pop_front();
            }
        }

        // 2. Try global overflow queue
        if (!task) {
            std::lock_guard<std::mutex> lock(global_work_mtx_);
            if (!global_work_queue_.empty()) {
                task = global_work_queue_.front();
                global_work_queue_.pop_front();
            }
        }

        // 3. Try stealing from other workers
        if (!task && worker_count_ > 1) {
            for (int32_t i = 0; i < worker_count_ && !task; i++) {
                if (i == w->id) continue;
                std::lock_guard<std::mutex> lock(workers_[i].mtx);
                if (!workers_[i].local_queue.empty()) {
                    task = workers_[i].local_queue.back();  // steal from back
                    workers_[i].local_queue.pop_back();
                }
            }
        }

        if (task) {
            if (task->cancelled) {
                task->done = 1;
                global_task_cv_.notify_all();
                continue;
            }
            task->worker_id = w->id;
            if (!task->done && task->handle) {
                liva_coro_resume(task->handle);
            }
        } else {
            // No work — wait briefly
            std::unique_lock<std::mutex> lock(w->mtx);
            w->cv.wait_for(lock, std::chrono::milliseconds(5),
                           [w]{ return !w->running || !w->local_queue.empty(); });
        }
    }
}

void liva_scheduler_init(int32_t num_workers) {
    if (scheduler_running_) return;

    if (num_workers <= 0) {
        unsigned hw = std::thread::hardware_concurrency();
        num_workers = (int32_t)(hw > 1 ? hw : 1);
    }
    worker_count_ = num_workers;
    workers_ = new WorkerThread[worker_count_];
    scheduler_running_ = true;

    for (int32_t i = 0; i < worker_count_; i++) {
        workers_[i].id = i;
        workers_[i].running = true;
        workers_[i].thread = std::thread(worker_loop, &workers_[i]);
    }
}

void liva_scheduler_shutdown() {
    if (!scheduler_running_) return;
    scheduler_running_ = false;

    for (int32_t i = 0; i < worker_count_; i++) {
        workers_[i].running = false;
        workers_[i].cv.notify_all();
    }
    for (int32_t i = 0; i < worker_count_; i++) {
        if (workers_[i].thread.joinable()) {
            workers_[i].thread.join();
        }
    }
    delete[] workers_;
    workers_ = nullptr;
    worker_count_ = 0;
}

int32_t liva_scheduler_worker_count() {
    return worker_count_;
}

// === Async I/O — Thread-Offloaded Blocking I/O ===

char *liva_async_file_read(const char *path) {
    if (!path) return nullptr;
    auto fut = std::async(std::launch::async, [](const char *p) -> char * {
        FILE *f = fopen(p, "rb");
        if (!f) return nullptr;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); return nullptr; }
        char *buf = (char *)malloc((size_t)sz + 1);
        if (!buf) { fclose(f); return nullptr; }
        size_t rd = fread(buf, 1, (size_t)sz, f);
        buf[rd] = '\0';
        fclose(f);
        return buf;
    }, path);
    return fut.get();
}

int8_t liva_async_file_write(const char *path, const char *content) {
    if (!path || !content) return 0;
    auto fut = std::async(std::launch::async, [](const char *p, const char *c) -> int8_t {
        FILE *f = fopen(p, "wb");
        if (!f) return 0;
        size_t len = strlen(c);
        size_t written = fwrite(c, 1, len, f);
        fclose(f);
        return (written == len) ? 1 : 0;
    }, path, content);
    return fut.get();
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

// --- JSON Serialization ---

static std::string json_escape_string(const char *s) {
    std::string out;
    out += '"';
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += *p;     break;
        }
    }
    out += '"';
    return out;
}

// Helper: set a raw JSON value for a key in a JSON object string
static char *json_set_raw(const char *json, const char *key, const std::string &rawValue) {
    if (!json || !key) return strdup_safe("{}");
    const char *p = json_skip_ws(json);
    if (*p != '{') return strdup_safe("{}");

    std::string escapedKey = json_escape_string(key);
    std::string result;

    // Collect existing key-value pairs, skipping the one we're replacing
    std::vector<std::pair<std::string, std::string>> pairs;
    p = json_skip_ws(p + 1); // skip '{'
    bool found = false;
    while (*p && *p != '}') {
        if (*p == ',') { p = json_skip_ws(p + 1); continue; }
        if (*p != '"') break;
        // Parse key
        char *existingKey = json_extract_string(p);
        const char *keyStart = p;
        p = json_skip_string(p);
        p = json_skip_ws(p);
        if (*p == ':') p++;
        p = json_skip_ws(p);
        // Capture value range
        const char *valStart = p;
        p = json_skip_value(p);
        std::string valStr(valStart, p);
        p = json_skip_ws(p);

        if (existingKey && strcmp(existingKey, key) == 0) {
            found = true;
            pairs.push_back({escapedKey, rawValue});
        } else {
            std::string kStr = existingKey ? json_escape_string(existingKey) : "\"\"";
            pairs.push_back({kStr, valStr});
        }
        if (existingKey) free(existingKey);
    }
    if (!found) {
        pairs.push_back({escapedKey, rawValue});
    }

    result = "{";
    for (size_t i = 0; i < pairs.size(); i++) {
        if (i > 0) result += ",";
        result += pairs[i].first + ":" + pairs[i].second;
    }
    result += "}";
    return strdup_safe(result.c_str());
}

// jsonCreate() -> string
char *liva_json_create() {
    return strdup_safe("{}");
}

// jsonSet(json, key, val) -> string
char *liva_json_set(const char *json, const char *key, const char *val) {
    std::string raw = val ? json_escape_string(val) : "null";
    return json_set_raw(json, key, raw);
}

// jsonSetInt(json, key, val) -> string
char *liva_json_set_int(const char *json, const char *key, int64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    return json_set_raw(json, key, buf);
}

// jsonSetFloat(json, key, val) -> string
char *liva_json_set_float(const char *json, const char *key, double val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", val);
    return json_set_raw(json, key, buf);
}

// jsonSetBool(json, key, val) -> string
char *liva_json_set_bool(const char *json, const char *key, int8_t val) {
    return json_set_raw(json, key, val ? "true" : "false");
}

// jsonRemove(json, key) -> string
char *liva_json_remove(const char *json, const char *key) {
    if (!json || !key) return strdup_safe("{}");
    const char *p = json_skip_ws(json);
    if (*p != '{') return strdup_safe("{}");

    std::vector<std::pair<std::string, std::string>> pairs;
    p = json_skip_ws(p + 1);
    while (*p && *p != '}') {
        if (*p == ',') { p = json_skip_ws(p + 1); continue; }
        if (*p != '"') break;
        char *existingKey = json_extract_string(p);
        p = json_skip_string(p);
        p = json_skip_ws(p);
        if (*p == ':') p++;
        p = json_skip_ws(p);
        const char *valStart = p;
        p = json_skip_value(p);
        std::string valStr(valStart, p);
        p = json_skip_ws(p);

        if (existingKey && strcmp(existingKey, key) != 0) {
            std::string kStr = json_escape_string(existingKey);
            pairs.push_back({kStr, valStr});
        }
        if (existingKey) free(existingKey);
    }

    std::string result = "{";
    for (size_t i = 0; i < pairs.size(); i++) {
        if (i > 0) result += ",";
        result += pairs[i].first + ":" + pairs[i].second;
    }
    result += "}";
    return strdup_safe(result.c_str());
}

// jsonGetArray(json, key) -> Optional<string> (null on missing/not-array)
char *liva_json_get_array(const char *json, const char *key) {
    if (!json || !key) return nullptr;
    const char *p = json_skip_ws(json);
    if (*p != '{') return nullptr;
    p = json_skip_ws(p + 1);
    while (*p && *p != '}') {
        if (*p == ',') { p = json_skip_ws(p + 1); continue; }
        if (*p != '"') break;
        char *k = json_extract_string(p);
        p = json_skip_string(p);
        p = json_skip_ws(p);
        if (*p == ':') p++;
        p = json_skip_ws(p);
        const char *valStart = p;
        p = json_skip_value(p);
        if (k && strcmp(k, key) == 0) {
            const char *vs = json_skip_ws(valStart);
            if (*vs == '[') {
                std::string val(valStart, p);
                free(k);
                return strdup_safe(val.c_str());
            }
            free(k);
            return nullptr;
        }
        if (k) free(k);
        p = json_skip_ws(p);
    }
    return nullptr;
}

// jsonGetObject(json, key) -> Optional<string> (null on missing/not-object)
char *liva_json_get_object(const char *json, const char *key) {
    if (!json || !key) return nullptr;
    const char *p = json_skip_ws(json);
    if (*p != '{') return nullptr;
    p = json_skip_ws(p + 1);
    while (*p && *p != '}') {
        if (*p == ',') { p = json_skip_ws(p + 1); continue; }
        if (*p != '"') break;
        char *k = json_extract_string(p);
        p = json_skip_string(p);
        p = json_skip_ws(p);
        if (*p == ':') p++;
        p = json_skip_ws(p);
        const char *valStart = p;
        p = json_skip_value(p);
        if (k && strcmp(k, key) == 0) {
            const char *vs = json_skip_ws(valStart);
            if (*vs == '{') {
                std::string val(valStart, p);
                free(k);
                return strdup_safe(val.c_str());
            }
            free(k);
            return nullptr;
        }
        if (k) free(k);
        p = json_skip_ws(p);
    }
    return nullptr;
}

// jsonCount(json) -> i32 (number of top-level keys)
int32_t liva_json_count(const char *json) {
    if (!json) return 0;
    const char *p = json_skip_ws(json);
    if (*p != '{') return 0;
    p = json_skip_ws(p + 1);
    int32_t count = 0;
    while (*p && *p != '}') {
        if (*p == ',') { p = json_skip_ws(p + 1); continue; }
        if (*p != '"') break;
        count++;
        p = json_skip_string(p);
        p = json_skip_ws(p);
        if (*p == ':') p++;
        p = json_skip_value(p);
        p = json_skip_ws(p);
    }
    return count;
}

// Pretty-print JSON with given indent width (returns new string, caller frees)
char *liva_json_stringify_pretty(const char *json, int32_t indent) {
    if (!json) {
        char *out = (char *)malloc(3);
        memcpy(out, "{}", 3);
        return out;
    }
    if (indent < 0) indent = 2;
    if (indent > 16) indent = 16;
    std::string result;
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    auto write_indent = [&](int d) {
        for (int i = 0; i < d * indent; ++i) result.push_back(' ');
    };
    for (const char *p = json; *p; ++p) {
        char c = *p;
        if (in_string) {
            result.push_back(c);
            if (escape) { escape = false; }
            else if (c == '\\') escape = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') {
            in_string = true;
            result.push_back(c);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '{' || c == '[') {
            // lookahead: if next non-space is closing bracket, emit compact
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') ++q;
            if ((*q == '}' && c == '{') || (*q == ']' && c == '[')) {
                result.push_back(c);
                result.push_back(*q);
                p = q;
                continue;
            }
            result.push_back(c);
            result.push_back('\n');
            ++depth;
            write_indent(depth);
        } else if (c == '}' || c == ']') {
            result.push_back('\n');
            --depth;
            write_indent(depth);
            result.push_back(c);
        } else if (c == ',') {
            result.push_back(c);
            result.push_back('\n');
            write_indent(depth);
        } else if (c == ':') {
            result.push_back(c);
            result.push_back(' ');
        } else {
            result.push_back(c);
        }
    }
    char *out = (char *)malloc(result.size() + 1);
    memcpy(out, result.c_str(), result.size() + 1);
    return out;
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

// === Test Runner ===

static thread_local int liva_test_total = 0;
static thread_local int liva_test_passed = 0;
static thread_local int liva_test_failed_count = 0;
static thread_local bool liva_test_active = false;
static thread_local jmp_buf liva_test_jmpbuf;

void liva_test_begin() {
    liva_test_total = 0;
    liva_test_passed = 0;
    liva_test_failed_count = 0;
}

void liva_test_run(const char *name, void (*test_fn)(void)) {
    liva_test_total++;
    liva_test_active = true;
    if (setjmp(liva_test_jmpbuf) == 0) {
        test_fn();
        liva_test_passed++;
        fprintf(stderr, "  PASS: %s\n", name);
    } else {
        liva_test_failed_count++;
        fprintf(stderr, "  FAIL: %s\n", name);
    }
    liva_test_active = false;
}

int32_t liva_test_end() {
    fprintf(stderr, "\n%d/%d tests passed", liva_test_passed, liva_test_total);
    if (liva_test_failed_count > 0) {
        fprintf(stderr, ", %d failed\n", liva_test_failed_count);
        return 1;
    }
    fprintf(stderr, "\n");
    return 0;
}

// Closure-based test runner: wraps a Liva closure call with setjmp,
// catches liva_test_fail longjmp, and returns 1 (passed) / 0 (failed).
int8_t liva_test_run_closure(const char *name, void *fn_ptr, void *env_ptr) {
    liva_test_total++;
    liva_test_active = true;
    int result = 1; // passed
    if (setjmp(liva_test_jmpbuf) == 0) {
        // Liva closure ABI: (env, args...) -> ret
        auto fn = (void (*)(void *))fn_ptr;
        fn(env_ptr);
        liva_test_passed++;
        if (name) fprintf(stderr, "  PASS: %s\n", name);
    } else {
        liva_test_failed_count++;
        result = 0;
        if (name) fprintf(stderr, "  FAIL: %s\n", name);
    }
    liva_test_active = false;
    return (int8_t)result;
}

void liva_test_fail(const char *msg) {
    if (liva_test_active) {
        if (msg) fprintf(stderr, "    %s\n", msg);
        longjmp(liva_test_jmpbuf, 1);
    } else {
        fprintf(stderr, "ASSERTION FAILED");
        if (msg) fprintf(stderr, ": %s", msg);
        fprintf(stderr, "\n");
        abort();
    }
}

// === Testing (assert functions) ===

void liva_assert(int8_t condition) {
    if (!condition) {
        liva_test_fail("ASSERTION FAILED");
    }
}

void liva_assert_msg(int8_t condition, const char *msg) {
    if (!condition) {
        static thread_local char buf[512];
        snprintf(buf, sizeof(buf), "ASSERTION FAILED: %s", msg ? msg : "(no message)");
        liva_test_fail(buf);
    }
}

void liva_assert_eq(int64_t a, int64_t b) {
    if (a != b) {
        static thread_local char buf[256];
        snprintf(buf, sizeof(buf), "ASSERTION FAILED: expected %lld == %lld",
                 (long long)a, (long long)b);
        liva_test_fail(buf);
    }
}

void liva_assert_eq_str(const char *a, const char *b) {
    if (!a && !b) return;
    if (!a || !b || strcmp(a, b) != 0) {
        static thread_local char buf[512];
        snprintf(buf, sizeof(buf), "ASSERTION FAILED: expected \"%s\" == \"%s\"",
                 a ? a : "(null)", b ? b : "(null)");
        liva_test_fail(buf);
    }
}

void liva_assert_eq_float(double a, double b) {
    double diff = a - b;
    if (diff < 0) diff = -diff;
    if (diff > 1e-9) {
        static thread_local char buf[256];
        snprintf(buf, sizeof(buf), "ASSERTION FAILED: expected %g == %g", a, b);
        liva_test_fail(buf);
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

// dateTimestamp() -> f64 (Unix timestamp)
double liva_date_timestamp() {
    return (double)time(nullptr);
}

// dateParse(str, fmt) -> f64 (timestamp, -1 on error)
double liva_date_parse(const char *str, const char *fmt) {
    if (!str || !fmt) return -1.0;
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_isdst = -1;
#ifdef _WIN32
    // Windows: no strptime, sscanf fallback for common formats
    if (strcmp(fmt, "%Y-%m-%d %H:%M:%S") == 0) {
        if (sscanf(str, "%d-%d-%d %d:%d:%d",
                   &t.tm_year, &t.tm_mon, &t.tm_mday,
                   &t.tm_hour, &t.tm_min, &t.tm_sec) == 6) {
            t.tm_year -= 1900;
            t.tm_mon -= 1;
            time_t result = mktime(&t);
            return (result == (time_t)-1) ? -1.0 : (double)result;
        }
    } else if (strcmp(fmt, "%Y-%m-%d") == 0) {
        if (sscanf(str, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) == 3) {
            t.tm_year -= 1900;
            t.tm_mon -= 1;
            time_t result = mktime(&t);
            return (result == (time_t)-1) ? -1.0 : (double)result;
        }
    } else if (strcmp(fmt, "%H:%M:%S") == 0) {
        if (sscanf(str, "%d:%d:%d", &t.tm_hour, &t.tm_min, &t.tm_sec) == 3) {
            t.tm_year = 70; t.tm_mon = 0; t.tm_mday = 1;
            time_t result = mktime(&t);
            return (result == (time_t)-1) ? -1.0 : (double)result;
        }
    }
    return -1.0;
#else
    char *end = strptime(str, fmt, &t);
    if (!end) return -1.0;
    time_t result = mktime(&t);
    return (result == (time_t)-1) ? -1.0 : (double)result;
#endif
}

// dateAdd(ts, secs) -> f64
double liva_date_add(double timestamp, double seconds) {
    return timestamp + seconds;
}

// dateDiff(ts1, ts2) -> f64
double liva_date_diff(double ts1, double ts2) {
    return ts1 - ts2;
}

// dateHour/dateMinute/dateSecond
int32_t liva_date_hour(double timestamp)   { struct tm *t = liva_localtime(timestamp); return t ? t->tm_hour : 0; }
int32_t liva_date_minute(double timestamp) { struct tm *t = liva_localtime(timestamp); return t ? t->tm_min : 0; }
int32_t liva_date_second(double timestamp) { struct tm *t = liva_localtime(timestamp); return t ? t->tm_sec : 0; }

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

// Base64URL (RFC 4648 §5): same alphabet but '-' instead of '+',
// '_' instead of '/', and no '=' padding. Used by JWT.
char *liva_base64_url_encode(const char *data) {
    char *std_enc = liva_base64_encode(data);
    if (!std_enc) return nullptr;
    size_t len = strlen(std_enc);
    // Strip trailing '=' padding
    while (len > 0 && std_enc[len - 1] == '=') {
        std_enc[--len] = '\0';
    }
    // Replace '+' -> '-', '/' -> '_'
    for (size_t i = 0; i < len; i++) {
        if (std_enc[i] == '+') std_enc[i] = '-';
        else if (std_enc[i] == '/') std_enc[i] = '_';
    }
    return std_enc;
}

char *liva_base64_url_decode(const char *data) {
    if (!data) return nullptr;
    size_t len = strlen(data);
    // Pad to multiple of 4 with '=' and translate URL-safe alphabet to standard
    size_t pad = (4 - (len % 4)) % 4;
    char *buf = (char *)malloc(len + pad + 1);
    if (!buf) return nullptr;
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '-')      buf[i] = '+';
        else if (c == '_') buf[i] = '/';
        else               buf[i] = c;
    }
    for (size_t i = 0; i < pad; i++) buf[len + i] = '=';
    buf[len + pad] = '\0';
    char *out = liva_base64_decode(buf);
    free(buf);
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

// === URL encoding (percent-encoding, RFC 3986) ===

char *liva_url_encode(const char *data) {
    if (!data) {
        char *out = (char *)malloc(1);
        if (out) out[0] = '\0';
        return out;
    }
    size_t len = strlen(data);
    // worst case: every byte → %XX (3x)
    char *out = (char *)malloc(len * 3 + 1);
    if (!out) return nullptr;
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        // Unreserved chars per RFC 3986: ALPHA / DIGIT / "-" / "." / "_" / "~"
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') ||
                         c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out[j++] = (char)c;
        } else {
            out[j++] = '%';
            out[j++] = hex[c >> 4];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = '\0';
    return out;
}

char *liva_url_decode(const char *data) {
    if (!data) return nullptr;
    size_t len = strlen(data);
    char *out = (char *)malloc(len + 1);
    if (!out) return nullptr;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '+') {
            out[j++] = ' ';
        } else if (c == '%' && i + 2 < len) {
            int hi = hex_char_val(data[i + 1]);
            int lo = hex_char_val(data[i + 2]);
            if (hi < 0 || lo < 0) { free(out); return nullptr; }
            out[j++] = (char)((hi << 4) | lo);
            i += 2;
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
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

// === Synchronization: RWLock ===

int64_t liva_rwlock_create() {
    auto *rw = new (std::nothrow) std::shared_mutex();
    if (!rw) return 0;
    return (int64_t)(intptr_t)rw;
}

void liva_rwlock_read_lock(int64_t handle) {
    if (!handle) return;
    auto *rw = (std::shared_mutex *)(intptr_t)handle;
    rw->lock_shared();
}

void liva_rwlock_read_unlock(int64_t handle) {
    if (!handle) return;
    auto *rw = (std::shared_mutex *)(intptr_t)handle;
    rw->unlock_shared();
}

void liva_rwlock_write_lock(int64_t handle) {
    if (!handle) return;
    auto *rw = (std::shared_mutex *)(intptr_t)handle;
    rw->lock();
}

void liva_rwlock_write_unlock(int64_t handle) {
    if (!handle) return;
    auto *rw = (std::shared_mutex *)(intptr_t)handle;
    rw->unlock();
}

int8_t liva_rwlock_try_read_lock(int64_t handle) {
    if (!handle) return 0;
    auto *rw = (std::shared_mutex *)(intptr_t)handle;
    return rw->try_lock_shared() ? 1 : 0;
}

int8_t liva_rwlock_try_write_lock(int64_t handle) {
    if (!handle) return 0;
    auto *rw = (std::shared_mutex *)(intptr_t)handle;
    return rw->try_lock() ? 1 : 0;
}

void liva_rwlock_free(int64_t handle) {
    if (!handle) return;
    auto *rw = (std::shared_mutex *)(intptr_t)handle;
    delete rw;
}

// === Synchronization: ConditionVariable ===
// We use std::condition_variable_any so it pairs with our std::mutex* handle
// without forcing the caller to hold a std::unique_lock.

int64_t liva_condvar_create() {
    auto *cv = new (std::nothrow) std::condition_variable_any();
    if (!cv) return 0;
    return (int64_t)(intptr_t)cv;
}

void liva_condvar_wait(int64_t cvHandle, int64_t mtxHandle) {
    if (!cvHandle || !mtxHandle) return;
    auto *cv = (std::condition_variable_any *)(intptr_t)cvHandle;
    auto *mtx = (std::mutex *)(intptr_t)mtxHandle;
    // Caller already holds *mtx. wait() releases it, blocks, then reacquires.
    cv->wait(*mtx);
}

void liva_condvar_notify_one(int64_t handle) {
    if (!handle) return;
    auto *cv = (std::condition_variable_any *)(intptr_t)handle;
    cv->notify_one();
}

void liva_condvar_notify_all(int64_t handle) {
    if (!handle) return;
    auto *cv = (std::condition_variable_any *)(intptr_t)handle;
    cv->notify_all();
}

void liva_condvar_free(int64_t handle) {
    if (!handle) return;
    auto *cv = (std::condition_variable_any *)(intptr_t)handle;
    delete cv;
}

// === TOML (minimal subset matching liva.toml semantics) ===

namespace {

struct LivaTomlValue {
    enum Kind { String, Integer, Boolean, StringArray };
    Kind kind = String;
    std::string s;
    int64_t i = 0;
    bool b = false;
    std::vector<std::string> arr;
};

struct LivaTomlDoc {
    // sections[section][key] = value
    std::unordered_map<std::string,
        std::unordered_map<std::string, LivaTomlValue>> sections;
};

static std::string toml_trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) --b;
    return s.substr(a, b - a);
}

// Parse a quoted string starting at pos (must point to ").
// Sets pos to one past the closing quote on success.
static bool toml_parse_str(const std::string &s, size_t &pos, std::string &out) {
    if (pos >= s.size() || s[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            char c = s[pos + 1];
            if (c == '"' || c == '\\') out.push_back(c);
            else if (c == 'n') out.push_back('\n');
            else if (c == 't') out.push_back('\t');
            else if (c == 'r') out.push_back('\r');
            else { out.push_back('\\'); out.push_back(c); }
            pos += 2;
        } else {
            out.push_back(s[pos]);
            ++pos;
        }
    }
    if (pos >= s.size()) return false; // unterminated
    ++pos; // consume closing "
    return true;
}

} // anonymous namespace

int64_t liva_toml_parse(const char *text) {
    if (!text) return 0;
    auto *doc = new (std::nothrow) LivaTomlDoc();
    if (!doc) return 0;

    std::string content(text);
    std::string currentSection;
    size_t lineStart = 0;
    while (lineStart <= content.size()) {
        size_t nl = content.find('\n', lineStart);
        std::string raw = (nl == std::string::npos)
            ? content.substr(lineStart)
            : content.substr(lineStart, nl - lineStart);
        lineStart = (nl == std::string::npos) ? content.size() + 1 : nl + 1;

        std::string line = toml_trim(raw);
        if (line.empty() || line[0] == '#') continue;

        // [section]
        if (line[0] == '[') {
            auto close = line.find(']');
            if (close == std::string::npos) { delete doc; return 0; }
            currentSection = toml_trim(line.substr(1, close - 1));
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) { delete doc; return 0; }
        std::string key = toml_trim(line.substr(0, eq));
        std::string val = toml_trim(line.substr(eq + 1));

        // strip inline comment when not inside string/array
        if (!val.empty() && val[0] != '"' && val[0] != '[') {
            auto h = val.find('#');
            if (h != std::string::npos) val = toml_trim(val.substr(0, h));
        }

        LivaTomlValue v;
        if (!val.empty() && val[0] == '"') {
            size_t pos = 0;
            std::string s;
            if (!toml_parse_str(val, pos, s)) { delete doc; return 0; }
            v.kind = LivaTomlValue::String;
            v.s = std::move(s);
        } else if (val == "true" || val == "false") {
            v.kind = LivaTomlValue::Boolean;
            v.b = (val == "true");
        } else if (!val.empty() && val[0] == '[') {
            auto closeB = val.find(']');
            if (closeB == std::string::npos) { delete doc; return 0; }
            v.kind = LivaTomlValue::StringArray;
            std::string inner = toml_trim(val.substr(1, closeB - 1));
            size_t p = 0;
            while (p < inner.size()) {
                while (p < inner.size() && (inner[p] == ' ' || inner[p] == '\t' || inner[p] == ','))
                    ++p;
                if (p >= inner.size()) break;
                if (inner[p] != '"') { delete doc; return 0; }
                std::string elem;
                if (!toml_parse_str(inner, p, elem)) { delete doc; return 0; }
                v.arr.push_back(std::move(elem));
            }
        } else {
            char *endptr = nullptr;
            long n = std::strtol(val.c_str(), &endptr, 10);
            if (endptr != val.c_str() && (*endptr == '\0' || *endptr == ' ' || *endptr == '\t')) {
                v.kind = LivaTomlValue::Integer;
                v.i = n;
            } else {
                v.kind = LivaTomlValue::String;
                v.s = val;
            }
        }
        doc->sections[currentSection][key] = std::move(v);
    }
    return (int64_t)(intptr_t)doc;
}

char *liva_toml_get_string(int64_t handle, const char *section, const char *key) {
    if (!handle || !section || !key) return nullptr;
    auto *doc = (LivaTomlDoc *)(intptr_t)handle;
    auto sIt = doc->sections.find(section);
    if (sIt == doc->sections.end()) return nullptr;
    auto kIt = sIt->second.find(key);
    if (kIt == sIt->second.end() || kIt->second.kind != LivaTomlValue::String)
        return nullptr;
    return strdup(kIt->second.s.c_str());
}

int64_t liva_toml_get_int(int64_t handle, const char *section,
                          const char *key, int8_t *ok) {
    if (ok) *ok = 0;
    if (!handle || !section || !key) return 0;
    auto *doc = (LivaTomlDoc *)(intptr_t)handle;
    auto sIt = doc->sections.find(section);
    if (sIt == doc->sections.end()) return 0;
    auto kIt = sIt->second.find(key);
    if (kIt == sIt->second.end() || kIt->second.kind != LivaTomlValue::Integer)
        return 0;
    if (ok) *ok = 1;
    return kIt->second.i;
}

int8_t liva_toml_get_bool(int64_t handle, const char *section,
                          const char *key, int8_t *ok) {
    if (ok) *ok = 0;
    if (!handle || !section || !key) return 0;
    auto *doc = (LivaTomlDoc *)(intptr_t)handle;
    auto sIt = doc->sections.find(section);
    if (sIt == doc->sections.end()) return 0;
    auto kIt = sIt->second.find(key);
    if (kIt == sIt->second.end() || kIt->second.kind != LivaTomlValue::Boolean)
        return 0;
    if (ok) *ok = 1;
    return kIt->second.b ? 1 : 0;
}

int8_t liva_toml_has_key(int64_t handle, const char *section, const char *key) {
    if (!handle || !section || !key) return 0;
    auto *doc = (LivaTomlDoc *)(intptr_t)handle;
    auto sIt = doc->sections.find(section);
    if (sIt == doc->sections.end()) return 0;
    return sIt->second.find(key) != sIt->second.end() ? 1 : 0;
}

void liva_toml_free(int64_t handle) {
    if (!handle) return;
    auto *doc = (LivaTomlDoc *)(intptr_t)handle;
    delete doc;
}

// === Channel Runtime ===

struct LivaChannel {
    int64_t *buffer;
    int64_t capacity;
    int64_t head;
    int64_t tail;
    int64_t count;
    int8_t closed;
    std::mutex mtx;
    std::condition_variable cv_not_full;
    std::condition_variable cv_not_empty;
};

int64_t liva_channel_create(int64_t capacity) {
    if (capacity <= 0) capacity = 1;
    auto *ch = new LivaChannel();
    ch->buffer = (int64_t *)calloc((size_t)capacity, sizeof(int64_t));
    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    return (int64_t)(intptr_t)ch;
}

void liva_channel_send(int64_t handle, int64_t value) {
    if (!handle) return;
    auto *ch = (LivaChannel *)(intptr_t)handle;
    std::unique_lock<std::mutex> lock(ch->mtx);
    ch->cv_not_full.wait(lock, [ch]{ return ch->count < ch->capacity || ch->closed; });
    if (ch->closed) return;
    ch->buffer[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;
    lock.unlock();
    ch->cv_not_empty.notify_one();
}

int64_t liva_channel_receive(int64_t handle, int8_t *ok) {
    if (!handle) { if (ok) *ok = 0; return 0; }
    auto *ch = (LivaChannel *)(intptr_t)handle;
    std::unique_lock<std::mutex> lock(ch->mtx);
    ch->cv_not_empty.wait(lock, [ch]{ return ch->count > 0 || ch->closed; });
    if (ch->count > 0) {
        int64_t value = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        if (ok) *ok = 1;
        lock.unlock();
        ch->cv_not_full.notify_one();
        return value;
    }
    if (ok) *ok = 0;
    return 0;
}

void liva_channel_close(int64_t handle) {
    if (!handle) return;
    auto *ch = (LivaChannel *)(intptr_t)handle;
    {
        std::lock_guard<std::mutex> lock(ch->mtx);
        ch->closed = 1;
    }
    ch->cv_not_full.notify_all();
    ch->cv_not_empty.notify_all();
}

int64_t liva_channel_len(int64_t handle) {
    if (!handle) return 0;
    auto *ch = (LivaChannel *)(intptr_t)handle;
    std::lock_guard<std::mutex> lock(ch->mtx);
    return ch->count;
}

void liva_channel_free(int64_t handle) {
    if (!handle) return;
    auto *ch = (LivaChannel *)(intptr_t)handle;
    free(ch->buffer);
    delete ch;
}

int8_t liva_channel_try_send(int64_t handle, int64_t value) {
    if (!handle) return 0;
    auto *ch = (LivaChannel *)(intptr_t)handle;
    std::unique_lock<std::mutex> lock(ch->mtx);
    if (ch->closed) return 0;
    if (ch->count >= ch->capacity) return 0;
    ch->buffer[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;
    lock.unlock();
    ch->cv_not_empty.notify_one();
    return 1;
}

int64_t liva_channel_try_receive(int64_t handle, int8_t *ok) {
    if (!handle) { if (ok) *ok = 0; return 0; }
    auto *ch = (LivaChannel *)(intptr_t)handle;
    std::unique_lock<std::mutex> lock(ch->mtx);
    if (ch->count == 0) { if (ok) *ok = 0; return 0; }
    int64_t value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;
    if (ok) *ok = 1;
    lock.unlock();
    ch->cv_not_full.notify_one();
    return value;
}

// === TaskGroup Runtime ===

struct LivaTaskGroup {
    LivaTask **tasks;
    int64_t count;
    int64_t capacity;
};

int64_t liva_task_group_create() {
    auto *g = new LivaTaskGroup();
    g->capacity = 8;
    g->tasks = (LivaTask **)calloc((size_t)g->capacity, sizeof(LivaTask *));
    g->count = 0;
    return (int64_t)(intptr_t)g;
}

void liva_task_group_spawn(int64_t group, LivaTask *task) {
    if (!group || !task) return;
    auto *g = (LivaTaskGroup *)(intptr_t)group;
    // Grow array if needed
    if (g->count >= g->capacity) {
        g->capacity *= 2;
        g->tasks = (LivaTask **)realloc(g->tasks, (size_t)g->capacity * sizeof(LivaTask *));
    }
    g->tasks[g->count++] = task;
    // Start the coroutine
    if (task->handle) {
        liva_coro_resume(task->handle);
    }
}

void liva_task_group_await_all(int64_t group) {
    if (!group) return;
    auto *g = (LivaTaskGroup *)(intptr_t)group;
    std::unique_lock<std::mutex> lock(global_task_cv_mtx_);
    while (true) {
        bool allDone = true;
        for (int64_t i = 0; i < g->count; i++) {
            if (g->tasks[i] && !g->tasks[i]->done) {
                allDone = false;
                break;
            }
        }
        if (allDone) break;
        global_task_cv_.wait_for(lock, std::chrono::milliseconds(10));
    }
}

void liva_task_group_cancel_all(int64_t group) {
    if (!group) return;
    auto *g = (LivaTaskGroup *)(intptr_t)group;
    for (int64_t i = 0; i < g->count; i++) {
        if (g->tasks[i]) {
            liva_task_cancel(g->tasks[i]);
        }
    }
}

int64_t liva_task_group_count(int64_t group) {
    if (!group) return 0;
    auto *g = (LivaTaskGroup *)(intptr_t)group;
    return g->count;
}

void liva_task_group_free(int64_t group) {
    if (!group) return;
    auto *g = (LivaTaskGroup *)(intptr_t)group;
    for (int64_t i = 0; i < g->count; i++) {
        if (g->tasks[i]) {
            if (g->tasks[i]->handle) {
                liva_coro_destroy(g->tasks[i]->handle);
                g->tasks[i]->handle = nullptr;
            }
            liva_task_destroy(g->tasks[i]);
        }
    }
    free(g->tasks);
    delete g;
}

// === Task Select & WithTimeout ===

int64_t liva_task_select(LivaTask **tasks, int64_t count) {
    if (!tasks || count <= 0) return -1;
    // Resume all non-started tasks
    for (int64_t i = 0; i < count; i++) {
        if (tasks[i] && !tasks[i]->done && tasks[i]->handle) {
            liva_coro_resume(tasks[i]->handle);
        }
    }
    // Wait until at least one completes
    std::unique_lock<std::mutex> lock(global_task_cv_mtx_);
    while (true) {
        for (int64_t i = 0; i < count; i++) {
            if (tasks[i] && tasks[i]->done) return i;
        }
        global_task_cv_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

int8_t liva_task_with_timeout(LivaTask *task, int64_t timeout_ms) {
    if (!task) return 0;
    if (!task->done && task->handle) {
        liva_coro_resume(task->handle);
    }
    int64_t deadline = liva_clock_ms() + timeout_ms;
    std::unique_lock<std::mutex> lock(global_task_cv_mtx_);
    while (!task->done) {
        int64_t remaining = deadline - liva_clock_ms();
        if (remaining <= 0) {
            liva_task_cancel(task);
            return 0;  // timeout
        }
        int64_t wait = remaining < 10 ? remaining : 10;
        global_task_cv_.wait_for(lock, std::chrono::milliseconds(wait));
    }
    return 1;  // completed
}

// === String Utility Functions (std::string) ===

char *liva_str_repeat(const char *s, int64_t n) {
    if (!s || n <= 0) return strdup_safe("");
    size_t slen = strlen(s);
    size_t total = slen * (size_t)n;
    char *result = (char *)malloc(total + 1);
    if (!result) return nullptr;
    for (int64_t i = 0; i < n; i++)
        memcpy(result + i * slen, s, slen);
    result[total] = '\0';
    return result;
}

char *liva_str_pad_left(const char *s, int64_t width, const char *fill) {
    if (!s) return strdup_safe("");
    size_t slen = strlen(s);
    if ((int64_t)slen >= width) return strdup_safe(s);
    char fc = (fill && fill[0]) ? fill[0] : ' ';
    size_t pad = (size_t)(width - (int64_t)slen);
    char *result = (char *)malloc((size_t)width + 1);
    if (!result) return nullptr;
    memset(result, fc, pad);
    memcpy(result + pad, s, slen);
    result[width] = '\0';
    return result;
}

char *liva_str_pad_right(const char *s, int64_t width, const char *fill) {
    if (!s) return strdup_safe("");
    size_t slen = strlen(s);
    if ((int64_t)slen >= width) return strdup_safe(s);
    char fc = (fill && fill[0]) ? fill[0] : ' ';
    size_t pad = (size_t)(width - (int64_t)slen);
    char *result = (char *)malloc((size_t)width + 1);
    if (!result) return nullptr;
    memcpy(result, s, slen);
    memset(result + slen, fc, pad);
    result[width] = '\0';
    return result;
}

char *liva_str_join(const char **strings, int64_t count, const char *sep) {
    if (!strings || count <= 0) return strdup_safe("");
    if (!sep) sep = "";
    size_t sepLen = strlen(sep);
    size_t total = 0;
    for (int64_t i = 0; i < count; i++) {
        total += strings[i] ? strlen(strings[i]) : 0;
        if (i > 0) total += sepLen;
    }
    char *result = (char *)malloc(total + 1);
    if (!result) return nullptr;
    char *p = result;
    for (int64_t i = 0; i < count; i++) {
        if (i > 0 && sepLen > 0) {
            memcpy(p, sep, sepLen);
            p += sepLen;
        }
        if (strings[i]) {
            size_t len = strlen(strings[i]);
            memcpy(p, strings[i], len);
            p += len;
        }
    }
    *p = '\0';
    return result;
}

char *liva_str_trim_left(const char *s) {
    if (!s) return strdup_safe("");
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
        s++;
    return strdup_safe(s);
}

char *liva_str_trim_right(const char *s) {
    if (!s) return strdup_safe("");
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\n' || s[len - 1] == '\r'))
        len--;
    char *result = (char *)malloc(len + 1);
    if (!result) return nullptr;
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}

char *liva_str_reverse(const char *s) {
    if (!s) return strdup_safe("");
    size_t len = strlen(s);
    // Collect UTF-8 character boundaries
    std::vector<std::pair<size_t, size_t>> chars; // (offset, length)
    size_t i = 0;
    while (i < len) {
        int clen = utf8_char_len((unsigned char)s[i]);
        if (i + clen > len) clen = (int)(len - i);
        chars.push_back({i, (size_t)clen});
        i += clen;
    }
    char *result = (char *)malloc(len + 1);
    if (!result) return nullptr;
    char *p = result;
    for (auto it = chars.rbegin(); it != chars.rend(); ++it) {
        memcpy(p, s + it->first, it->second);
        p += it->second;
    }
    *p = '\0';
    return result;
}

char **liva_str_chars(const char *s, int64_t *count) {
    if (!s || !count) {
        if (count) *count = 0;
        return nullptr;
    }
    size_t len = strlen(s);
    // Count UTF-8 characters
    std::vector<std::pair<size_t, size_t>> chars;
    size_t i = 0;
    while (i < len) {
        int clen = utf8_char_len((unsigned char)s[i]);
        if (i + clen > len) clen = (int)(len - i);
        chars.push_back({i, (size_t)clen});
        i += clen;
    }
    *count = (int64_t)chars.size();
    char **result = (char **)malloc(sizeof(char *) * chars.size());
    if (!result) { *count = 0; return nullptr; }
    for (size_t j = 0; j < chars.size(); j++) {
        result[j] = (char *)malloc(chars[j].second + 1);
        memcpy(result[j], s + chars[j].first, chars[j].second);
        result[j][chars[j].second] = '\0';
    }
    return result;
}

char **liva_str_lines(const char *s, int64_t *count) {
    if (!s || !count) {
        if (count) *count = 0;
        return nullptr;
    }
    std::vector<std::string> lines;
    const char *p = s;
    const char *start = s;
    while (*p) {
        if (*p == '\n') {
            lines.emplace_back(start, p);
            start = p + 1;
        }
        p++;
    }
    if (start <= p) // last line (may be empty)
        lines.emplace_back(start, p);
    *count = (int64_t)lines.size();
    char **result = (char **)malloc(sizeof(char *) * lines.size());
    if (!result) { *count = 0; return nullptr; }
    for (size_t i = 0; i < lines.size(); i++)
        result[i] = strdup_safe(lines[i].c_str());
    return result;
}

// === UTF-8 utilities (public API) ===

// UTF-8 codepoint count (same as liva_str_length)
int64_t liva_str_char_count(const char *s) {
    if (!s) return 0;
    return utf8_count(s);
}

// Decode a codepoint from a UTF-8 sequence starting at bytes[0..len-1]
static int32_t decode_utf8_cp(const unsigned char *bytes, int len) {
    if (len == 1) return bytes[0];
    if (len == 2) return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    if (len == 3) return ((bytes[0] & 0x0F) << 12)
                        | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
    if (len == 4) return ((bytes[0] & 0x07) << 18)
                        | ((bytes[1] & 0x3F) << 12)
                        | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
    return -1;
}

// Get Unicode codepoint at character index. Returns -1 on out-of-range.
int32_t liva_str_codepoint_at(const char *s, int64_t index) {
    if (!s || index < 0) return -1;
    int64_t offset = utf8_offset(s, index);
    if (offset < 0) return -1;
    const unsigned char *p = (const unsigned char *)s + offset;
    if (*p == '\0') return -1;
    int clen = utf8_char_len(*p);
    return decode_utf8_cp(p, clen);
}

// Returns 1 if all bytes are ASCII (< 0x80), 0 otherwise
int8_t liva_str_is_ascii(const char *s) {
    if (!s) return 1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p & 0x80) return 0;
    }
    return 1;
}

// Codepoint predicates (ASCII-only for correctness; Unicode letter/digit
// classes would require large tables — out of scope here).
int8_t liva_char_is_alpha(int32_t cp) {
    return ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) ? 1 : 0;
}

int8_t liva_char_is_digit(int32_t cp) {
    return (cp >= '0' && cp <= '9') ? 1 : 0;
}

int8_t liva_char_is_alnum(int32_t cp) {
    return (liva_char_is_alpha(cp) || liva_char_is_digit(cp)) ? 1 : 0;
}

int8_t liva_char_is_space(int32_t cp) {
    return (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
            cp == '\v' || cp == '\f') ? 1 : 0;
}

int8_t liva_char_is_upper(int32_t cp) {
    return (cp >= 'A' && cp <= 'Z') ? 1 : 0;
}

int8_t liva_char_is_lower(int32_t cp) {
    return (cp >= 'a' && cp <= 'z') ? 1 : 0;
}

int32_t liva_char_to_upper(int32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - ('a' - 'A');
    return cp;
}

int32_t liva_char_to_lower(int32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
    return cp;
}

// === Collection Utility Functions (std::collections) ===

void liva_array_reversed(const void *data, int64_t len, int64_t elem_size,
                          void **out_data, int64_t *out_len, int64_t *out_cap) {
    if (!data || len <= 0 || !out_data || !out_len || !out_cap) {
        if (out_data) *out_data = nullptr;
        if (out_len) *out_len = 0;
        if (out_cap) *out_cap = 0;
        return;
    }
    void *result = malloc((size_t)len * (size_t)elem_size);
    if (!result) { *out_data = nullptr; *out_len = 0; *out_cap = 0; return; }
    const char *src = (const char *)data;
    char *dst = (char *)result;
    for (int64_t i = 0; i < len; i++)
        memcpy(dst + i * elem_size, src + (len - 1 - i) * elem_size, (size_t)elem_size);
    *out_data = result;
    *out_len = len;
    *out_cap = len;
}

void liva_array_sorted(const void *data, int64_t len, int64_t elem_size,
                        int (*cmp)(const void *, const void *),
                        void **out_data, int64_t *out_len, int64_t *out_cap) {
    if (!data || len <= 0 || !out_data || !out_len || !out_cap) {
        if (out_data) *out_data = nullptr;
        if (out_len) *out_len = 0;
        if (out_cap) *out_cap = 0;
        return;
    }
    size_t total = (size_t)len * (size_t)elem_size;
    void *result = malloc(total);
    if (!result) { *out_data = nullptr; *out_len = 0; *out_cap = 0; return; }
    memcpy(result, data, total);
    if (cmp)
        qsort(result, (size_t)len, (size_t)elem_size, cmp);
    *out_data = result;
    *out_len = len;
    *out_cap = len;
}

int8_t liva_array_any(const void *data, int64_t len, int64_t elem_size,
                       int8_t (*pred)(const void *)) {
    if (!data || !pred || len <= 0) return 0;
    const char *p = (const char *)data;
    for (int64_t i = 0; i < len; i++) {
        if (pred(p + i * elem_size))
            return 1;
    }
    return 0;
}

int8_t liva_array_all(const void *data, int64_t len, int64_t elem_size,
                       int8_t (*pred)(const void *)) {
    if (!data || !pred || len <= 0) return 1;
    const char *p = (const char *)data;
    for (int64_t i = 0; i < len; i++) {
        if (!pred(p + i * elem_size))
            return 0;
    }
    return 1;
}

int64_t liva_array_count(const void *data, int64_t len, int64_t elem_size,
                          int8_t (*pred)(const void *)) {
    if (!data || !pred || len <= 0) return 0;
    const char *p = (const char *)data;
    int64_t n = 0;
    for (int64_t i = 0; i < len; i++) {
        if (pred(p + i * elem_size))
            n++;
    }
    return n;
}

// === Benchmarking ===

struct BenchState {
    int64_t startTick;   // platform tick at start
    int64_t totalNs;     // accumulated nanoseconds
    int64_t iterCount;   // iteration count
#ifdef _WIN32
    int64_t freq;        // QPC frequency (cached)
#endif
};

static BenchState benchSlots[16];
static int benchNextSlot = 0;

#ifdef _WIN32
static int64_t benchTickToNs(int64_t ticks, int64_t freq) {
    // Convert QPC ticks to nanoseconds: ticks * 1e9 / freq
    return (int64_t)((double)ticks * 1000000000.0 / (double)freq);
}
#endif

int64_t liva_bench_start() {
    int slot = benchNextSlot++;
    if (slot >= 16) {
        fprintf(stderr, "BENCH: too many concurrent benchmarks (max 16)\n");
        abort();
    }
    BenchState &s = benchSlots[slot];
    s.totalNs = 0;
    s.iterCount = 0;
#ifdef _WIN32
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    s.freq = f.QuadPart;
    s.startTick = t.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    s.startTick = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
    return (int64_t)slot;
}

int64_t liva_bench_iter(int64_t handle) {
    if (handle < 0 || handle >= 16) return 0;
    BenchState &s = benchSlots[handle];
    int64_t elapsedNs;
#ifdef _WIN32
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    elapsedNs = benchTickToNs(t.QuadPart - s.startTick, s.freq);
    s.startTick = t.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    elapsedNs = now - s.startTick;
    s.startTick = now;
#endif
    s.totalNs += elapsedNs;
    s.iterCount++;
    return elapsedNs;
}

int64_t liva_bench_done(int64_t handle) {
    if (handle < 0 || handle >= 16) return 0;
    BenchState &s = benchSlots[handle];
    if (s.iterCount == 0) return 0;
    return s.totalNs / s.iterCount;
}

void liva_bench_report(const char *name, int64_t handle) {
    if (handle < 0 || handle >= 16) return;
    BenchState &s = benchSlots[handle];
    int64_t avg = (s.iterCount > 0) ? (s.totalNs / s.iterCount) : 0;
    printf("%s: %lld ns/iter (%lld iterations)\n",
           name, (long long)avg, (long long)s.iterCount);
}

void liva_bench_reset(int64_t handle) {
    if (handle < 0 || handle >= 16) return;
    BenchState &s = benchSlots[handle];
    s.totalNs = 0;
    s.iterCount = 0;
#ifdef _WIN32
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    s.startTick = t.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    s.startTick = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

// === Crypto: SHA-256 (FIPS 180-4) ===

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t sha256_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16)
              | ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i-15],7) ^ sha256_rotr(w[i-15],18) ^ (w[i-15]>>3);
        uint32_t s1 = sha256_rotr(w[i-2],17) ^ sha256_rotr(w[i-2],19)  ^ (w[i-2]>>10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=state[0], b=state[1], c=state[2], d=state[3];
    uint32_t e=state[4], f=state[5], g=state[6], h=state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = sha256_rotr(e,6) ^ sha256_rotr(e,11) ^ sha256_rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = sha256_rotr(a,2) ^ sha256_rotr(a,13) ^ sha256_rotr(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint8_t block[64];
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        sha256_transform(state, data + i);
    size_t rem = len - i;
    memcpy(block, data + i, rem);
    block[rem++] = 0x80;
    if (rem > 56) {
        memset(block + rem, 0, 64 - rem);
        sha256_transform(state, block);
        rem = 0;
    }
    memset(block + rem, 0, 56 - rem);
    uint64_t bits = (uint64_t)len * 8;
    for (int j = 7; j >= 0; j--)
        block[56 + (7 - j)] = (uint8_t)(bits >> (j * 8));
    sha256_transform(state, block);
    for (int j = 0; j < 8; j++) {
        out[j*4]   = (uint8_t)(state[j] >> 24);
        out[j*4+1] = (uint8_t)(state[j] >> 16);
        out[j*4+2] = (uint8_t)(state[j] >> 8);
        out[j*4+3] = (uint8_t)(state[j]);
    }
}

char *liva_sha256(const char *data) {
    if (!data) data = "";
    uint8_t hash[32];
    sha256_hash((const uint8_t *)data, strlen(data), hash);
    char *result = (char *)malloc(65);
    if (!result) liva_panic("out of memory");
    for (int i = 0; i < 32; i++)
        sprintf(result + i * 2, "%02x", hash[i]);
    result[64] = '\0';
    return result;
}

// === Crypto: MD5 (RFC 1321) ===

static const uint32_t md5_s[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

static const uint32_t md5_k[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static uint32_t md5_leftrotate(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

static void md5_hash(const uint8_t *data, size_t len, uint8_t out[16]) {
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    // Pre-processing: pad to 64-byte boundary
    size_t new_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)calloc(new_len, 1);
    if (!msg) liva_panic("out of memory");
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    memcpy(msg + new_len - 8, &bits, 8); // little-endian

    for (size_t offset = 0; offset < new_len; offset += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++)
            memcpy(&M[i], msg + offset + i * 4, 4); // little-endian
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t F, g;
            if (i < 16) {
                F = (B & C) | (~B & D); g = (uint32_t)i;
            } else if (i < 32) {
                F = (D & B) | (~D & C); g = (5 * (uint32_t)i + 1) % 16;
            } else if (i < 48) {
                F = B ^ C ^ D; g = (3 * (uint32_t)i + 5) % 16;
            } else {
                F = C ^ (B | ~D); g = (7 * (uint32_t)i) % 16;
            }
            F = F + A + md5_k[i] + M[g];
            A = D; D = C; C = B; B = B + md5_leftrotate(F, md5_s[i]);
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    free(msg);
    memcpy(out, &a0, 4); memcpy(out+4, &b0, 4);
    memcpy(out+8, &c0, 4); memcpy(out+12, &d0, 4);
}

char *liva_md5(const char *data) {
    if (!data) data = "";
    uint8_t hash[16];
    md5_hash((const uint8_t *)data, strlen(data), hash);
    char *result = (char *)malloc(33);
    if (!result) liva_panic("out of memory");
    for (int i = 0; i < 16; i++)
        sprintf(result + i * 2, "%02x", hash[i]);
    result[32] = '\0';
    return result;
}

// === Crypto: HMAC-SHA256 (RFC 2104) ===

char *liva_hmac_sha256(const char *key, const char *data) {
    if (!key) key = "";
    if (!data) data = "";
    uint8_t k_pad[64];
    memset(k_pad, 0, 64);
    size_t key_len = strlen(key);
    if (key_len > 64) {
        sha256_hash((const uint8_t *)key, key_len, k_pad); // hash key if > 64 bytes
        key_len = 32;
    } else {
        memcpy(k_pad, key, key_len);
    }
    // Inner: SHA256( (k XOR ipad) || data )
    uint8_t i_pad[64], o_pad[64];
    for (int i = 0; i < 64; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }
    size_t data_len = strlen(data);
    size_t inner_len = 64 + data_len;
    uint8_t *inner_msg = (uint8_t *)malloc(inner_len);
    if (!inner_msg) liva_panic("out of memory");
    memcpy(inner_msg, i_pad, 64);
    memcpy(inner_msg + 64, data, data_len);
    uint8_t inner_hash[32];
    sha256_hash(inner_msg, inner_len, inner_hash);
    free(inner_msg);
    // Outer: SHA256( (k XOR opad) || inner_hash )
    uint8_t outer_msg[96]; // 64 + 32
    memcpy(outer_msg, o_pad, 64);
    memcpy(outer_msg + 64, inner_hash, 32);
    uint8_t final_hash[32];
    sha256_hash(outer_msg, 96, final_hash);
    char *result = (char *)malloc(65);
    if (!result) liva_panic("out of memory");
    for (int i = 0; i < 32; i++)
        sprintf(result + i * 2, "%02x", final_hash[i]);
    result[64] = '\0';
    return result;
}

// === Crypto: SHA-1 (FIPS 180-4) ===

static uint32_t sha1_rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16)
             | ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 80; i++)
        w[i] = sha1_rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8f1bbcdc; }
        else             { f = b ^ c ^ d;                     k = 0xca62c1d6; }
        uint32_t temp = sha1_rotl(a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rotl(b, 30); b = a; a = temp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t state[5] = {
        0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0
    };
    uint8_t block[64];
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        sha1_transform(state, data + i);
    size_t rem = len - i;
    memcpy(block, data + i, rem);
    block[rem++] = 0x80;
    if (rem > 56) {
        memset(block + rem, 0, 64 - rem);
        sha1_transform(state, block);
        rem = 0;
    }
    memset(block + rem, 0, 56 - rem);
    uint64_t bits = (uint64_t)len * 8;
    for (int j = 7; j >= 0; j--)
        block[56 + (7 - j)] = (uint8_t)(bits >> (j * 8));
    sha1_transform(state, block);
    for (int j = 0; j < 5; j++) {
        out[j*4]   = (uint8_t)(state[j] >> 24);
        out[j*4+1] = (uint8_t)(state[j] >> 16);
        out[j*4+2] = (uint8_t)(state[j] >> 8);
        out[j*4+3] = (uint8_t)(state[j]);
    }
}

char *liva_sha1(const char *data) {
    if (!data) data = "";
    uint8_t hash[20];
    sha1_hash((const uint8_t *)data, strlen(data), hash);
    char *result = (char *)malloc(41);
    if (!result) liva_panic("out of memory");
    for (int i = 0; i < 20; i++)
        sprintf(result + i * 2, "%02x", hash[i]);
    result[40] = '\0';
    return result;
}

// === Crypto: SHA-512 (FIPS 180-4) ===

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static uint64_t sha512_rotr(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

static void sha512_transform(uint64_t state[8], const uint8_t block[128]) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int b = 0; b < 8; b++)
            w[i] = (w[i] << 8) | (uint64_t)block[i*8 + b];
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = sha512_rotr(w[i-15], 1) ^ sha512_rotr(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = sha512_rotr(w[i-2], 19) ^ sha512_rotr(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint64_t a=state[0], b=state[1], c=state[2], d=state[3];
    uint64_t e=state[4], f=state[5], g=state[6], h=state[7];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = sha512_rotr(e,14) ^ sha512_rotr(e,18) ^ sha512_rotr(e,41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t temp1 = h + S1 + ch + sha512_k[i] + w[i];
        uint64_t S0 = sha512_rotr(a,28) ^ sha512_rotr(a,34) ^ sha512_rotr(a,39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = S0 + maj;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha512_hash(const uint8_t *data, size_t len, uint8_t out[64]) {
    uint64_t state[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    uint8_t block[128];
    size_t i = 0;
    for (; i + 128 <= len; i += 128)
        sha512_transform(state, data + i);
    size_t rem = len - i;
    memcpy(block, data + i, rem);
    block[rem++] = 0x80;
    if (rem > 112) {
        memset(block + rem, 0, 128 - rem);
        sha512_transform(state, block);
        rem = 0;
    }
    memset(block + rem, 0, 112 - rem);
    // 128-bit length, big-endian. Liva inputs never exceed 64 bits, so high half is zero.
    memset(block + 112, 0, 8);
    uint64_t bits = (uint64_t)len * 8;
    for (int j = 7; j >= 0; j--)
        block[120 + (7 - j)] = (uint8_t)(bits >> (j * 8));
    sha512_transform(state, block);
    for (int j = 0; j < 8; j++) {
        for (int b = 0; b < 8; b++)
            out[j*8 + b] = (uint8_t)(state[j] >> ((7 - b) * 8));
    }
}

char *liva_sha512(const char *data) {
    if (!data) data = "";
    uint8_t hash[64];
    sha512_hash((const uint8_t *)data, strlen(data), hash);
    char *result = (char *)malloc(129);
    if (!result) liva_panic("out of memory");
    for (int i = 0; i < 64; i++)
        sprintf(result + i * 2, "%02x", hash[i]);
    result[128] = '\0';
    return result;
}

// === Crypto: HMAC-SHA1 / HMAC-SHA512 (RFC 2104) ===

char *liva_hmac_sha1(const char *key, const char *data) {
    if (!key) key = "";
    if (!data) data = "";
    uint8_t k_pad[64];
    memset(k_pad, 0, 64);
    size_t key_len = strlen(key);
    if (key_len > 64) {
        sha1_hash((const uint8_t *)key, key_len, k_pad);
        key_len = 20;
    } else {
        memcpy(k_pad, key, key_len);
    }
    uint8_t i_pad[64], o_pad[64];
    for (int i = 0; i < 64; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }
    size_t data_len = strlen(data);
    size_t inner_len = 64 + data_len;
    uint8_t *inner_msg = (uint8_t *)malloc(inner_len);
    if (!inner_msg) liva_panic("out of memory");
    memcpy(inner_msg, i_pad, 64);
    memcpy(inner_msg + 64, data, data_len);
    uint8_t inner_hash[20];
    sha1_hash(inner_msg, inner_len, inner_hash);
    free(inner_msg);
    uint8_t outer_msg[84]; // 64 + 20
    memcpy(outer_msg, o_pad, 64);
    memcpy(outer_msg + 64, inner_hash, 20);
    uint8_t final_hash[20];
    sha1_hash(outer_msg, 84, final_hash);
    char *result = (char *)malloc(41);
    if (!result) liva_panic("out of memory");
    for (int i = 0; i < 20; i++)
        sprintf(result + i * 2, "%02x", final_hash[i]);
    result[40] = '\0';
    return result;
}

char *liva_hmac_sha512(const char *key, const char *data) {
    if (!key) key = "";
    if (!data) data = "";
    // SHA-512 block size is 128 bytes, hash size 64 bytes
    uint8_t k_pad[128];
    memset(k_pad, 0, 128);
    size_t key_len = strlen(key);
    if (key_len > 128) {
        sha512_hash((const uint8_t *)key, key_len, k_pad);
        key_len = 64;
    } else {
        memcpy(k_pad, key, key_len);
    }
    uint8_t i_pad[128], o_pad[128];
    for (int i = 0; i < 128; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }
    size_t data_len = strlen(data);
    size_t inner_len = 128 + data_len;
    uint8_t *inner_msg = (uint8_t *)malloc(inner_len);
    if (!inner_msg) liva_panic("out of memory");
    memcpy(inner_msg, i_pad, 128);
    memcpy(inner_msg + 128, data, data_len);
    uint8_t inner_hash[64];
    sha512_hash(inner_msg, inner_len, inner_hash);
    free(inner_msg);
    uint8_t outer_msg[192]; // 128 + 64
    memcpy(outer_msg, o_pad, 128);
    memcpy(outer_msg + 128, inner_hash, 64);
    uint8_t final_hash[64];
    sha512_hash(outer_msg, 192, final_hash);
    char *result = (char *)malloc(129);
    if (!result) liva_panic("out of memory");
    for (int i = 0; i < 64; i++)
        sprintf(result + i * 2, "%02x", final_hash[i]);
    result[128] = '\0';
    return result;
}

// === JWT signature helpers ===
//
// JWT signatures cannot go through Liva's hex-decoded `string` because the
// raw HMAC bytes can contain '\0' (which liva strings treat as end-of-string).
// These functions compute HMAC raw bytes and base64url-encode them in one
// step, returning a malloc'd C string ready to splice into a token.

// Length-aware base64url encoder for binary input.
static char *b64u_encode_bytes(const uint8_t *data, size_t len) {
    // Output length: ceil(len/3) groups of 4 chars, minus padding.
    size_t full_groups = len / 3;
    size_t rem = len % 3;
    size_t out_len = full_groups * 4 + (rem == 0 ? 0 : (rem == 1 ? 2 : 3));
    char *out = (char *)malloc(out_len + 1);
    if (!out) liva_panic("out of memory");
    size_t j = 0;
    static const char b64u[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (size_t i = 0; i + 3 <= len; i += 3) {
        uint32_t triple = ((uint32_t)data[i] << 16)
                        | ((uint32_t)data[i + 1] << 8)
                        | (uint32_t)data[i + 2];
        out[j++] = b64u[(triple >> 18) & 0x3F];
        out[j++] = b64u[(triple >> 12) & 0x3F];
        out[j++] = b64u[(triple >> 6) & 0x3F];
        out[j++] = b64u[triple & 0x3F];
    }
    if (rem == 1) {
        uint32_t triple = (uint32_t)data[len - 1] << 16;
        out[j++] = b64u[(triple >> 18) & 0x3F];
        out[j++] = b64u[(triple >> 12) & 0x3F];
    } else if (rem == 2) {
        uint32_t triple = ((uint32_t)data[len - 2] << 16)
                        | ((uint32_t)data[len - 1] << 8);
        out[j++] = b64u[(triple >> 18) & 0x3F];
        out[j++] = b64u[(triple >> 12) & 0x3F];
        out[j++] = b64u[(triple >> 6) & 0x3F];
    }
    out[j] = '\0';
    return out;
}

// Inline HMAC-SHA256 producing raw 32-byte output, then base64url-encode.
char *liva_jwt_hs256_sig(const char *secret, const char *data) {
    if (!secret) secret = "";
    if (!data) data = "";
    uint8_t k_pad[64];
    memset(k_pad, 0, 64);
    size_t key_len = strlen(secret);
    if (key_len > 64) {
        sha256_hash((const uint8_t *)secret, key_len, k_pad);
        key_len = 32;
    } else {
        memcpy(k_pad, secret, key_len);
    }
    uint8_t i_pad[64], o_pad[64];
    for (int i = 0; i < 64; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }
    size_t data_len = strlen(data);
    size_t inner_len = 64 + data_len;
    uint8_t *inner_msg = (uint8_t *)malloc(inner_len);
    if (!inner_msg) liva_panic("out of memory");
    memcpy(inner_msg, i_pad, 64);
    memcpy(inner_msg + 64, data, data_len);
    uint8_t inner_hash[32];
    sha256_hash(inner_msg, inner_len, inner_hash);
    free(inner_msg);
    uint8_t outer_msg[96];
    memcpy(outer_msg, o_pad, 64);
    memcpy(outer_msg + 64, inner_hash, 32);
    uint8_t final_hash[32];
    sha256_hash(outer_msg, 96, final_hash);
    return b64u_encode_bytes(final_hash, 32);
}

// Inline HMAC-SHA512 producing raw 64-byte output, then base64url-encode.
char *liva_jwt_hs512_sig(const char *secret, const char *data) {
    if (!secret) secret = "";
    if (!data) data = "";
    uint8_t k_pad[128];
    memset(k_pad, 0, 128);
    size_t key_len = strlen(secret);
    if (key_len > 128) {
        sha512_hash((const uint8_t *)secret, key_len, k_pad);
        key_len = 64;
    } else {
        memcpy(k_pad, secret, key_len);
    }
    uint8_t i_pad[128], o_pad[128];
    for (int i = 0; i < 128; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }
    size_t data_len = strlen(data);
    size_t inner_len = 128 + data_len;
    uint8_t *inner_msg = (uint8_t *)malloc(inner_len);
    if (!inner_msg) liva_panic("out of memory");
    memcpy(inner_msg, i_pad, 128);
    memcpy(inner_msg + 128, data, data_len);
    uint8_t inner_hash[64];
    sha512_hash(inner_msg, inner_len, inner_hash);
    free(inner_msg);
    uint8_t outer_msg[192];
    memcpy(outer_msg, o_pad, 128);
    memcpy(outer_msg + 128, inner_hash, 64);
    uint8_t final_hash[64];
    sha512_hash(outer_msg, 192, final_hash);
    return b64u_encode_bytes(final_hash, 64);
}

void *liva_str_to_bytes(const char *s, int64_t *out_len) {
    if (!s) {
        if (out_len) *out_len = 0;
        return malloc(1); // non-null empty buffer
    }
    size_t n = strlen(s);
    void *buf = malloc(n > 0 ? n : 1);
    if (!buf) liva_panic("out of memory");
    if (n > 0) memcpy(buf, s, n);
    if (out_len) *out_len = (int64_t)n;
    return buf;
}

char *liva_bytes_to_str(const void *data, int64_t len) {
    if (len < 0) len = 0;
    char *out = (char *)malloc((size_t)len + 1);
    if (!out) liva_panic("out of memory");
    if (data && len > 0) memcpy(out, data, (size_t)len);
    out[len] = '\0';
    return out;
}

void *liva_hex_decode_bytes(const char *data, int64_t *out_len, int8_t *ok) {
    if (ok) *ok = 0;
    if (out_len) *out_len = 0;
    if (!data) return nullptr;
    size_t len = strlen(data);
    if (len % 2 != 0) return nullptr;
    size_t n = len / 2;
    void *buf = malloc(n > 0 ? n : 1);
    if (!buf) liva_panic("out of memory");
    auto *out = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_char_val(data[i * 2]);
        int lo = hex_char_val(data[i * 2 + 1]);
        if (hi < 0 || lo < 0) { free(buf); return nullptr; }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (out_len) *out_len = (int64_t)n;
    if (ok) *ok = 1;
    return buf;
}

char *liva_hex_encode_bytes(const void *data, int64_t len) {
    if (len < 0) len = 0;
    char *out = (char *)malloc((size_t)len * 2 + 1);
    if (!out) liva_panic("out of memory");
    static const char hex[] = "0123456789abcdef";
    auto *bytes = (const uint8_t *)data;
    for (int64_t i = 0; i < len; i++) {
        out[i * 2]     = hex[(bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[bytes[i] & 0xF];
    }
    out[len * 2] = '\0';
    return out;
}

char *liva_base64_url_encode_bytes(const void *data, int64_t len) {
    if (len < 0) len = 0;
    return b64u_encode_bytes((const uint8_t *)data, (size_t)len);
}

void *liva_base64_url_decode_bytes(const char *data, int64_t *out_len, int8_t *ok) {
    if (ok) *ok = 0;
    if (out_len) *out_len = 0;
    if (!data) return nullptr;
    char *as_str = liva_base64_url_decode(data);
    if (!as_str) return nullptr;
    // base64url_decode → standard decode → byte count = strlen of result
    // BUT decoded bytes may contain NUL. Recompute via the explicit
    // formula: pad input to multiple of 4, then 3 output bytes per group
    // minus padding equivalents. Easier: decode directly here.
    free(as_str);
    size_t len = strlen(data);
    size_t pad = (4 - (len % 4)) % 4;
    char *padded = (char *)malloc(len + pad + 1);
    if (!padded) liva_panic("out of memory");
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '-')      padded[i] = '+';
        else if (c == '_') padded[i] = '/';
        else               padded[i] = c;
    }
    for (size_t i = 0; i < pad; i++) padded[len + i] = '=';
    padded[len + pad] = '\0';
    size_t total = len + pad;
    // Standard base64 decode with explicit byte count.
    size_t out_bytes = total / 4 * 3;
    if (total > 0 && padded[total - 1] == '=') out_bytes--;
    if (total > 1 && padded[total - 2] == '=') out_bytes--;
    void *buf = malloc(out_bytes > 0 ? out_bytes : 1);
    if (!buf) { free(padded); liva_panic("out of memory"); }
    auto *out = (uint8_t *)buf;
    size_t j = 0;
    for (size_t i = 0; i < total; i += 4) {
        int a = b64_decode_char(padded[i]);
        int b = b64_decode_char(padded[i + 1]);
        int c = padded[i + 2] == '=' ? 0 : b64_decode_char(padded[i + 2]);
        int d = padded[i + 3] == '=' ? 0 : b64_decode_char(padded[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            free(buf); free(padded);
            return nullptr;
        }
        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12)
                        | ((uint32_t)c << 6) | (uint32_t)d;
        if (j < out_bytes) out[j++] = (uint8_t)((triple >> 16) & 0xFF);
        if (j < out_bytes) out[j++] = (uint8_t)((triple >> 8) & 0xFF);
        if (j < out_bytes) out[j++] = (uint8_t)(triple & 0xFF);
    }
    free(padded);
    if (out_len) *out_len = (int64_t)out_bytes;
    if (ok) *ok = 1;
    return buf;
}

// === gzip (RFC 1952) with full deflate (RFC 1951) ===
//
// gzipEncode emits a single fixed-Huffman block (BTYPE=01) preceded by
// a small amount of LZ77 backreference matching. Output is valid gzip
// and typically smaller than the input for compressible data; for
// already-incompressible / very short inputs it falls back to a stored
// (uncompressed) block.
//
// gzipDecode handles all three deflate block types: stored, fixed
// Huffman, and dynamic Huffman — so it reads gzip data produced by
// any conformant tool, not just Liva's own encoder.

static uint32_t gzip_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

// --- RFC 1951 deflate constants -------------------------------------

namespace {

// Length code 257..285 → (base_length, extra_bits). Code 256 is end-of-block
// and not represented here.
static const int kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const int kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
// Distance code 0..29 → (base_dist, extra_bits).
static const int kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
    12289, 16385, 24577
};
static const int kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
// Code-length code permutation order from RFC 1951 §3.2.7.
static const int kCodeLenOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

// --- Bit reader (LSB-first per RFC 1951 §3.1.1) ---------------------

struct BitReader {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    uint32_t bits;
    int nbits;
    bool err;
};

static void br_init(BitReader *br, const uint8_t *buf, size_t len, size_t pos) {
    br->buf = buf; br->len = len; br->pos = pos;
    br->bits = 0; br->nbits = 0; br->err = false;
}
static bool br_fill(BitReader *br, int n) {
    while (br->nbits < n) {
        if (br->pos >= br->len) { br->err = true; return false; }
        br->bits |= ((uint32_t)br->buf[br->pos++]) << br->nbits;
        br->nbits += 8;
    }
    return true;
}
static uint32_t br_read(BitReader *br, int n) {
    if (!br_fill(br, n)) return 0;
    uint32_t v = br->bits & ((1u << n) - 1u);
    br->bits >>= n;
    br->nbits -= n;
    return v;
}
static void br_align(BitReader *br) {
    int drop = br->nbits & 7;
    br->bits >>= drop;
    br->nbits -= drop;
}

// --- Canonical Huffman decoder --------------------------------------
//
// Stores symbols sorted by (length, code value). Decoding reads one bit
// at a time, accumulating MSB-first, and looks up the (length, code)
// pair in the count/symbol arrays.

struct HuffTable {
    int counts[16];     // counts[len] = number of symbols with bit-length `len`
    int symbols[288];   // symbols sorted: first counts[1], then counts[2], ...
};

static bool huff_build(HuffTable *ht, const uint8_t *lens, int n) {
    int offs[16];
    for (int i = 0; i < 16; i++) ht->counts[i] = 0;
    for (int s = 0; s < n; s++) {
        int L = lens[s];
        if (L >= 16) return false;
        ht->counts[L]++;
    }
    ht->counts[0] = 0;
    offs[0] = 0;
    for (int i = 1; i < 16; i++) offs[i] = offs[i - 1] + ht->counts[i - 1];
    for (int s = 0; s < n; s++) {
        int L = lens[s];
        if (L != 0) ht->symbols[offs[L]++] = s;
    }
    return true;
}

static int huff_decode(BitReader *br, const HuffTable *ht) {
    int code = 0, first = 0, idx = 0;
    for (int len = 1; len < 16; len++) {
        if (!br_fill(br, 1)) return -1;
        int bit = (int)(br->bits & 1u);
        br->bits >>= 1; br->nbits -= 1;
        code = (code << 1) | bit;
        int cnt = ht->counts[len];
        int delta = code - first;
        if (delta < cnt) return ht->symbols[idx + delta];
        idx += cnt;
        first = (first + cnt) << 1;
    }
    br->err = true;
    return -1;
}

// --- Fixed Huffman tables (RFC 1951 §3.2.6) -------------------------

static void build_fixed_tables(HuffTable *lit, HuffTable *dist) {
    uint8_t llens[288];
    for (int i = 0; i <= 143; i++) llens[i] = 8;
    for (int i = 144; i <= 255; i++) llens[i] = 9;
    for (int i = 256; i <= 279; i++) llens[i] = 7;
    for (int i = 280; i <= 287; i++) llens[i] = 8;
    huff_build(lit, llens, 288);
    uint8_t dlens[30];
    for (int i = 0; i < 30; i++) dlens[i] = 5;
    huff_build(dist, dlens, 30);
}

// --- Inflate ---------------------------------------------------------

struct OutBuf {
    uint8_t *buf;
    size_t cap;
    size_t len;
};

static bool ob_grow(OutBuf *ob, size_t need) {
    if (need <= ob->cap) return true;
    size_t nc = ob->cap == 0 ? (need < 256 ? 256 : need) : ob->cap * 2;
    while (nc < need) nc *= 2;
    uint8_t *nb = (uint8_t *)realloc(ob->buf, nc);
    if (!nb) return false;
    ob->buf = nb; ob->cap = nc;
    return true;
}
static bool ob_put(OutBuf *ob, uint8_t b) {
    if (!ob_grow(ob, ob->len + 1)) return false;
    ob->buf[ob->len++] = b;
    return true;
}

static bool inflate_block(BitReader *br, int btype, OutBuf *ob) {
    HuffTable lit_h, dist_h;
    if (btype == 1) {
        build_fixed_tables(&lit_h, &dist_h);
    } else if (btype == 2) {
        if (!br_fill(br, 14)) return false;
        int hlit  = (int)br_read(br, 5) + 257;
        int hdist = (int)br_read(br, 5) + 1;
        int hclen = (int)br_read(br, 4) + 4;
        if (hlit > 286 || hdist > 30) return false;
        uint8_t cllens[19];
        for (int i = 0; i < 19; i++) cllens[i] = 0;
        for (int i = 0; i < hclen; i++)
            cllens[kCodeLenOrder[i]] = (uint8_t)br_read(br, 3);
        if (br->err) return false;
        HuffTable cl_h;
        if (!huff_build(&cl_h, cllens, 19)) return false;

        uint8_t lens[286 + 30];
        int total = hlit + hdist;
        int i = 0;
        while (i < total) {
            int sym = huff_decode(br, &cl_h);
            if (sym < 0) return false;
            if (sym < 16) {
                lens[i++] = (uint8_t)sym;
            } else if (sym == 16) {
                if (i == 0) return false;
                int rep = (int)br_read(br, 2) + 3;
                if (i + rep > total) return false;
                uint8_t prev = lens[i - 1];
                for (int k = 0; k < rep; k++) lens[i++] = prev;
            } else if (sym == 17) {
                int rep = (int)br_read(br, 3) + 3;
                if (i + rep > total) return false;
                for (int k = 0; k < rep; k++) lens[i++] = 0;
            } else { // 18
                int rep = (int)br_read(br, 7) + 11;
                if (i + rep > total) return false;
                for (int k = 0; k < rep; k++) lens[i++] = 0;
            }
        }
        if (!huff_build(&lit_h, lens, hlit)) return false;
        if (!huff_build(&dist_h, lens + hlit, hdist)) return false;
    } else {
        return false;
    }

    while (true) {
        int sym = huff_decode(br, &lit_h);
        if (sym < 0) return false;
        if (sym < 256) {
            if (!ob_put(ob, (uint8_t)sym)) return false;
        } else if (sym == 256) {
            return true;
        } else {
            int li = sym - 257;
            if (li < 0 || li >= 29) return false;
            int length = kLenBase[li] + (int)br_read(br, kLenExtra[li]);
            int dsym = huff_decode(br, &dist_h);
            if (dsym < 0 || dsym >= 30) return false;
            int dist = kDistBase[dsym] + (int)br_read(br, kDistExtra[dsym]);
            if (br->err) return false;
            if (dist <= 0 || (size_t)dist > ob->len) return false;
            if (!ob_grow(ob, ob->len + length)) return false;
            // LZ77 backreference: copy bytes one-at-a-time so that
            // dist < length (run-length expansion) works correctly.
            for (int k = 0; k < length; k++) {
                ob->buf[ob->len] = ob->buf[ob->len - dist];
                ob->len++;
            }
        }
    }
}

static bool inflate(const uint8_t *src, size_t src_len, size_t start,
                    size_t end, OutBuf *ob) {
    BitReader br;
    br_init(&br, src, end, start);
    (void)src_len;
    bool last = false;
    while (!last) {
        if (!br_fill(&br, 3)) return false;
        last = (br_read(&br, 1) != 0);
        int btype = (int)br_read(&br, 2);
        if (btype == 0) {
            br_align(&br);
            if (br.pos + 4 > br.len) return false;
            uint16_t L = (uint16_t)br.buf[br.pos] |
                ((uint16_t)br.buf[br.pos + 1] << 8);
            uint16_t N = (uint16_t)br.buf[br.pos + 2] |
                ((uint16_t)br.buf[br.pos + 3] << 8);
            br.pos += 4;
            if ((uint16_t)~L != N) return false;
            if (br.pos + L > br.len) return false;
            if (!ob_grow(ob, ob->len + L)) return false;
            memcpy(ob->buf + ob->len, br.buf + br.pos, L);
            ob->len += L;
            br.pos += L;
            br.bits = 0;
            br.nbits = 0;
        } else if (btype == 1 || btype == 2) {
            if (!inflate_block(&br, btype, ob)) return false;
        } else {
            return false;
        }
    }
    return !br.err;
}

// --- Bit writer (encoder side) ---------------------------------------

struct BitWriter {
    uint8_t *buf;
    size_t cap;
    size_t len;
    uint32_t bits;
    int nbits;
};

static bool bw_grow(BitWriter *bw, size_t need) {
    if (need <= bw->cap) return true;
    size_t nc = bw->cap == 0 ? (need < 256 ? 256 : need) : bw->cap * 2;
    while (nc < need) nc *= 2;
    uint8_t *nb = (uint8_t *)realloc(bw->buf, nc);
    if (!nb) return false;
    bw->buf = nb; bw->cap = nc;
    return true;
}
static bool bw_write(BitWriter *bw, uint32_t v, int n) {
    bw->bits |= (v & ((1u << n) - 1u)) << bw->nbits;
    bw->nbits += n;
    while (bw->nbits >= 8) {
        if (!bw_grow(bw, bw->len + 1)) return false;
        bw->buf[bw->len++] = (uint8_t)(bw->bits & 0xFF);
        bw->bits >>= 8;
        bw->nbits -= 8;
    }
    return true;
}
// Huffman codes pack MSB-first within their length, so we reverse the bits.
static uint32_t bw_reverse(uint32_t v, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}
static bool bw_huff(BitWriter *bw, uint32_t code, int len) {
    return bw_write(bw, bw_reverse(code, len), len);
}
static bool bw_flush(BitWriter *bw) {
    if (bw->nbits > 0) {
        if (!bw_grow(bw, bw->len + 1)) return false;
        bw->buf[bw->len++] = (uint8_t)(bw->bits & 0xFF);
        bw->bits = 0;
        bw->nbits = 0;
    }
    return true;
}

// --- Fixed Huffman code tables for the encoder ----------------------

// Literal/length symbol → (code, length). Built from the canonical
// length distribution at the top of every encode call (cheap).
struct FixedCodes {
    uint32_t lit_code[288];
    int lit_len[288];
    uint32_t dist_code[30];
    int dist_len[30];
};

static void build_canonical_codes(const uint8_t *lens, int n,
                                   uint32_t *codes) {
    int bl_count[16] = {0};
    for (int i = 0; i < n; i++) bl_count[lens[i]]++;
    bl_count[0] = 0;
    uint32_t code = 0;
    uint32_t next_code[16] = {0};
    for (int b = 1; b < 16; b++) {
        code = (code + bl_count[b - 1]) << 1;
        next_code[b] = code;
    }
    for (int i = 0; i < n; i++) {
        if (lens[i] != 0) codes[i] = next_code[lens[i]]++;
        else codes[i] = 0;
    }
}

static void build_fixed_codes(FixedCodes *fc) {
    uint8_t llens[288];
    for (int i = 0; i <= 143; i++) llens[i] = 8;
    for (int i = 144; i <= 255; i++) llens[i] = 9;
    for (int i = 256; i <= 279; i++) llens[i] = 7;
    for (int i = 280; i <= 287; i++) llens[i] = 8;
    build_canonical_codes(llens, 288, fc->lit_code);
    for (int i = 0; i < 288; i++) fc->lit_len[i] = llens[i];

    uint8_t dlens[30];
    for (int i = 0; i < 30; i++) dlens[i] = 5;
    build_canonical_codes(dlens, 30, fc->dist_code);
    for (int i = 0; i < 30; i++) fc->dist_len[i] = dlens[i];
}

// Map a length 3..258 to (length-code-index, extra_bits, extra_value).
static void length_to_code(int length, int *code_idx, int *extra_bits,
                            int *extra_val) {
    for (int i = 28; i >= 0; i--) {
        if (length >= kLenBase[i]) {
            *code_idx = i;
            *extra_bits = kLenExtra[i];
            *extra_val = length - kLenBase[i];
            return;
        }
    }
    *code_idx = 0; *extra_bits = 0; *extra_val = 0;
}

// Map a distance 1..32768 to (distance-code, extra_bits, extra_value).
static void dist_to_code(int dist, int *code_idx, int *extra_bits,
                          int *extra_val) {
    for (int i = 29; i >= 0; i--) {
        if (dist >= kDistBase[i]) {
            *code_idx = i;
            *extra_bits = kDistExtra[i];
            *extra_val = dist - kDistBase[i];
            return;
        }
    }
    *code_idx = 0; *extra_bits = 0; *extra_val = 0;
}

// --- LZ77 matcher (hash-chain over a 32k window) --------------------

// Minimum match length per RFC 1951.
static const int kMinMatch = 3;
static const int kMaxMatch = 258;
static const size_t kWindowSize = 32768;
static const size_t kHashBits = 14;
static const size_t kHashSize = 1u << kHashBits;
static const size_t kHashMask = kHashSize - 1;

static inline uint32_t lz_hash(const uint8_t *p) {
    return (((uint32_t)p[0] << 16) ^ ((uint32_t)p[1] << 8) ^ (uint32_t)p[2])
        * 0x1E35A7BDu >> (32 - (uint32_t)kHashBits);
}

// Encode `src[0..src_len)` as a single fixed-Huffman block written into
// `bw`. Block must be marked BFINAL by the caller.
static bool encode_fixed_block(const uint8_t *src, size_t src_len,
                                BitWriter *bw, const FixedCodes *fc) {
    if (!bw_write(bw, 1, 1)) return false; // BFINAL
    if (!bw_write(bw, 1, 2)) return false; // BTYPE = 01

    // hash_head[h] = index into src of the most recent position that
    // hashed to h (or -1 for none). hash_prev chains older positions.
    int *hash_head = (int *)malloc(kHashSize * sizeof(int));
    int *hash_prev = (int *)malloc(src_len > 0 ? src_len * sizeof(int) : sizeof(int));
    if (!hash_head || !hash_prev) {
        free(hash_head); free(hash_prev);
        return false;
    }
    for (size_t i = 0; i < kHashSize; i++) hash_head[i] = -1;

    auto emit_lit = [&](uint8_t c) {
        bw_huff(bw, fc->lit_code[c], fc->lit_len[c]);
    };
    auto emit_match = [&](int length, int dist) {
        int li, le, lv;
        length_to_code(length, &li, &le, &lv);
        int sym = 257 + li;
        bw_huff(bw, fc->lit_code[sym], fc->lit_len[sym]);
        if (le > 0) bw_write(bw, (uint32_t)lv, le);
        int di, de, dv;
        dist_to_code(dist, &di, &de, &dv);
        bw_huff(bw, fc->dist_code[di], fc->dist_len[di]);
        if (de > 0) bw_write(bw, (uint32_t)dv, de);
    };

    size_t i = 0;
    while (i < src_len) {
        if (i + kMinMatch > src_len) {
            emit_lit(src[i]);
            i++;
            continue;
        }
        uint32_t h = lz_hash(src + i) & kHashMask;
        int chain = hash_head[h];
        int best_len = 0, best_dist = 0;
        // Limit chain search depth so worst-case stays bounded.
        int chain_limit = 16;
        while (chain >= 0 && (size_t)((int64_t)i - chain) <= kWindowSize &&
               chain_limit-- > 0) {
            size_t prev = (size_t)chain;
            // Quick reject on the fourth byte (we already know first 3 hash
            // matched, but they may collide — recheck).
            if (src[prev] == src[i] && src[prev + 1] == src[i + 1] &&
                src[prev + 2] == src[i + 2]) {
                size_t maxL = src_len - i;
                if (maxL > (size_t)kMaxMatch) maxL = (size_t)kMaxMatch;
                size_t L = 3;
                while (L < maxL && src[prev + L] == src[i + L]) L++;
                if ((int)L > best_len) {
                    best_len = (int)L;
                    best_dist = (int)(i - prev);
                    if (best_len == kMaxMatch) break;
                }
            }
            chain = hash_prev[prev];
        }
        // Insert current position into the chain.
        hash_prev[i] = hash_head[h];
        hash_head[h] = (int)i;

        if (best_len >= kMinMatch) {
            emit_match(best_len, best_dist);
            // Insert intermediate positions so future matches can find them.
            for (int k = 1; k < best_len && i + k + kMinMatch <= src_len; k++) {
                uint32_t h2 = lz_hash(src + i + k) & kHashMask;
                hash_prev[i + k] = hash_head[h2];
                hash_head[h2] = (int)(i + k);
            }
            i += best_len;
        } else {
            emit_lit(src[i]);
            i++;
        }
    }
    free(hash_head); free(hash_prev);
    // End-of-block marker (literal/length symbol 256).
    bw_huff(bw, fc->lit_code[256], fc->lit_len[256]);
    return bw_flush(bw);
}

} // anonymous namespace

void *liva_gzip_encode_bytes(const void *data, int64_t len, int64_t *out_len) {
    if (len < 0) len = 0;
    auto *src = (const uint8_t *)data;

    BitWriter bw{};
    if (!bw_grow(&bw, 256)) liva_panic("out of memory");

    // Gzip header: 1F 8B 08 00 00 00 00 00 00 FF
    static const uint8_t hdr[10] = {
        0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xFF
    };
    if (!bw_grow(&bw, 10)) liva_panic("out of memory");
    memcpy(bw.buf, hdr, 10);
    bw.len = 10;

    FixedCodes fc;
    build_fixed_codes(&fc);
    if (!encode_fixed_block(src, (size_t)len, &bw, &fc)) {
        free(bw.buf);
        liva_panic("deflate encode failed");
    }

    // Trailer: CRC32 + ISIZE little-endian.
    uint32_t crc = gzip_crc32(src, (size_t)len);
    uint32_t isize = (uint32_t)len;
    if (!bw_grow(&bw, bw.len + 8)) liva_panic("out of memory");
    bw.buf[bw.len + 0] = (uint8_t)(crc & 0xFF);
    bw.buf[bw.len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    bw.buf[bw.len + 2] = (uint8_t)((crc >> 16) & 0xFF);
    bw.buf[bw.len + 3] = (uint8_t)((crc >> 24) & 0xFF);
    bw.buf[bw.len + 4] = (uint8_t)(isize & 0xFF);
    bw.buf[bw.len + 5] = (uint8_t)((isize >> 8) & 0xFF);
    bw.buf[bw.len + 6] = (uint8_t)((isize >> 16) & 0xFF);
    bw.buf[bw.len + 7] = (uint8_t)((isize >> 24) & 0xFF);
    bw.len += 8;

    if (out_len) *out_len = (int64_t)bw.len;
    return bw.buf;
}

void *liva_gzip_decode_bytes(const void *data, int64_t len,
                              int64_t *out_len, int8_t *ok) {
    if (ok) *ok = 0;
    if (out_len) *out_len = 0;
    if (!data || len < 18) return nullptr;
    auto *src = (const uint8_t *)data;

    if (src[0] != 0x1F || src[1] != 0x8B || src[2] != 0x08) return nullptr;
    uint8_t flg = src[3];
    size_t pos = 10;
    if (flg & 0x04) {
        if (pos + 2 > (size_t)len) return nullptr;
        uint16_t xlen = (uint16_t)src[pos] | ((uint16_t)src[pos + 1] << 8);
        pos += 2 + xlen;
        if (pos > (size_t)len) return nullptr;
    }
    if (flg & 0x08) {
        while (pos < (size_t)len && src[pos] != 0) pos++;
        if (pos >= (size_t)len) return nullptr;
        pos++;
    }
    if (flg & 0x10) {
        while (pos < (size_t)len && src[pos] != 0) pos++;
        if (pos >= (size_t)len) return nullptr;
        pos++;
    }
    if (flg & 0x02) {
        if (pos + 2 > (size_t)len) return nullptr;
        pos += 2;
    }
    if ((size_t)len < pos + 8) return nullptr;
    size_t deflate_end = (size_t)len - 8;

    OutBuf ob{};
    if (!inflate(src, (size_t)len, pos, deflate_end, &ob)) {
        free(ob.buf);
        return nullptr;
    }

    uint32_t exp_crc = (uint32_t)src[deflate_end] |
                      ((uint32_t)src[deflate_end + 1] << 8) |
                      ((uint32_t)src[deflate_end + 2] << 16) |
                      ((uint32_t)src[deflate_end + 3] << 24);
    uint32_t exp_isize = (uint32_t)src[deflate_end + 4] |
                        ((uint32_t)src[deflate_end + 5] << 8) |
                        ((uint32_t)src[deflate_end + 6] << 16) |
                        ((uint32_t)src[deflate_end + 7] << 24);
    if ((uint32_t)ob.len != exp_isize) { free(ob.buf); return nullptr; }
    uint32_t actual_crc = gzip_crc32(ob.buf, ob.len);
    if (actual_crc != exp_crc) { free(ob.buf); return nullptr; }

    if (out_len) *out_len = (int64_t)ob.len;
    if (ok) *ok = 1;
    if (!ob.buf) {
        ob.buf = (uint8_t *)malloc(1);
        if (!ob.buf) liva_panic("out of memory");
    }
    return ob.buf;
}

void *liva_array_clone(const void *data, int64_t count, int64_t elem_size) {
    // Always allocate at least an 8-element buffer so the resulting
    // DynArray can be pushed-into without an immediate realloc.
    int64_t cap = count > 8 ? count : 8;
    if ((size_t)cap > SIZE_MAX / (size_t)elem_size)
        liva_panic("array clone overflow");
    void *new_data = malloc((size_t)cap * (size_t)elem_size);
    if (!new_data) liva_panic("out of memory");
    if (data && count > 0)
        memcpy(new_data, data, (size_t)count * (size_t)elem_size);
    return new_data;
}

// Constant-time string equality — used for HMAC signatures, password
// hashes, etc. where leaking byte-by-byte position via timing matters.
int8_t liva_const_time_eq(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a);
    size_t lb = strlen(b);
    // Different-length strings can't be equal. Returning early on length
    // doesn't leak which byte differed (the only side channel we care
    // about); time still depends on input length, which is public.
    if (la != lb) return 0;
    int diff = 0;
    for (size_t i = 0; i < la; i++)
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return diff == 0 ? 1 : 0;
}

// === ISO 8601 / RFC 3339 ===

char *liva_iso_format_utc(double timestamp) {
    time_t t = (time_t)timestamp;
    struct tm tm_utc;
#ifdef _WIN32
    if (gmtime_s(&tm_utc, &t) != 0) return nullptr;
#else
    if (!gmtime_r(&t, &tm_utc)) return nullptr;
#endif
    char *buf = (char *)malloc(32);
    if (!buf) liva_panic("out of memory");
    snprintf(buf, 32, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

double liva_iso_parse(const char *str, int8_t *ok) {
    if (ok) *ok = 0;
    if (!str) return 0.0;
    int year = 0, mon = 0, day = 0, hour = 0, minute = 0, sec = 0;
    int consumed = 0;
    bool have_time = false;
    if (sscanf(str, "%d-%d-%dT%d:%d:%d%n",
               &year, &mon, &day, &hour, &minute, &sec, &consumed) == 6) {
        have_time = true;
    } else if (sscanf(str, "%d-%d-%d %d:%d:%d%n",
                      &year, &mon, &day, &hour, &minute, &sec, &consumed) == 6) {
        have_time = true;
    } else if (sscanf(str, "%d-%d-%d%n", &year, &mon, &day, &consumed) == 3) {
        // date-only — already at midnight UTC
    } else {
        return 0.0;
    }

    const char *p = str + consumed;
    int tz_offset_secs = 0;

    if (have_time) {
        // Optional fractional seconds — accept and ignore (timestamp is whole seconds).
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9') p++;
        }
        if (*p == 'Z' || *p == 'z') {
            p++;
        } else if (*p == '+' || *p == '-') {
            char sign = *p;
            int tz_h = 0, tz_m = 0;
            if (sscanf(p + 1, "%d:%d", &tz_h, &tz_m) != 2) {
                // Some forms write +HHMM without colon; try that.
                if (sscanf(p + 1, "%2d%2d", &tz_h, &tz_m) != 2) return 0.0;
            }
            tz_offset_secs = tz_h * 3600 + tz_m * 60;
            if (sign == '-') tz_offset_secs = -tz_offset_secs;
        }
        // If no Z and no offset, treat as UTC (lenient — RFC 3339 requires
        // some indicator, but many sources omit it).
    }

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = sec;
#ifdef _WIN32
    time_t utc_ts = _mkgmtime(&t);
#else
    time_t utc_ts = timegm(&t);
#endif
    if (utc_ts == (time_t)-1) return 0.0;

    if (ok) *ok = 1;
    return (double)(utc_ts - tz_offset_secs);
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
