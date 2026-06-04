# net + http Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the `net::net` stub and thin `http::http` wrapper with a cohesive HTTP-client + URL subsystem: a native-backed `Url` parser/builder, a fluent immutable `HttpRequest` builder with request headers and JSON integration, and a handle-free eager-copy `HttpResponse`.

**Architecture:** `net::net` exposes a `Url` value type whose `parse` is backed by six runtime accessor natives; `http::http` builds on `net::Url` + runtime natives and the `json::json` DOM. Chainable builders are **immutable** (take `self` by value, return a fresh struct) because `return self` from `mut self` is broken. A general compiler fix lands first so `len()`/`indexOf()` results mix with integer literals.

**Tech Stack:** C++20, LLVM 21, libcurl/WinHTTP runtime, GoogleTest. Build: `.\build_clang.bat` (Ninja/Clang → `build-clang/`). Test: `ctest --test-dir build-clang --output-on-failure` (run SERIALLY, no `-j`).

**Spec:** `docs/superpowers/specs/2026-06-04-net-http-redesign-design.md`

---

## Conventions for every task

- **Rebuild before testing.** After editing ANY `.cpp`/`.h` OR any `.liva` (the build copies stdlib into `build-clang/`), run `.\build_clang.bat` before `ctest`. Never test stale artifacts.
- **Run tests serially.** `ctest --test-dir build-clang --output-on-failure` with no `-j`. `-j` triggers pre-existing races (SelfHostTest/BuildCacheTest/IncrementalBenchmarkTest). `SelfHostTest.StrSplitWithAnnotationReturnsParts` is flaky even serially — a single failure is not a regression; re-run to confirm.
- **Six-layer native binding** (for every `liva_*` native exposed to Liva): (1) `stdlib/runtime/runtime.cpp` impl; (2) `stdlib/runtime/runtime.h` decl; (3) `src/IR/IRGen.cpp` `createRuntimeDecls()` `getOrInsertFunction` — omission → `getOrPanic` crash at codegen; (4) `src/IR/IRGenCall.cpp` lowering `if (funcName == ...)`; (5) `src/Sema/TypeChecker.cpp` builtin-name loop (~line 72) **and** return-type switch (~line 2379); (6) `src/Sema/ModuleLoader.cpp` `std::net` list (~line 70). Plus `src/JIT/JITEngine.cpp` `REG(...)` for REPL/JIT and `src/LSP/LSPServer.cpp` builtin completion list.
- **IRGen gotchas:** `??` nil-coalescing fails in IRGen (use `if let`); bool is i1 in IR / i8 in runtime; string returns need `trackStringTemp(result)`; nullable `char*` → `Optional<string>` wrap pattern (see existing http lowering).
- **Liva language gotchas (verified):** integer literals are i32 (after Task 1 they widen to i64 in mixed binary ops); `String` has no `==`/`!=` (use `len(x) > 0`); `toString(intVal)` converts i32/i64/f64/bool → String; `parseInt(s) -> i32?`; one statement per line (no `;` separators); string methods: `indexOf(sub)->i64`, `substring(start,len)->String`, `contains/startsWith/endsWith->bool`, `split(delim)->[String]`, `trim/toLower/toUpper`.

---

## Task 1: Compiler — widen i32 literals to i64 in mixed-width binary ops

**Files:**
- Modify: `src/IR/IRGenExpr.cpp` (`visitBinaryExpr`, just before the op `switch` at ~line 230)
- Test: `tests/unit/RuntimeExecTest.cpp` (new test)

**Why:** `len()`/`indexOf()` return i64; `len(s) - 1`, `len(s) > 0`, `idx + 1` currently fail LLVM verification (`add i64, i32`). Fix by sign-extending a **constant** i32 operand to match an i64 operand. Constant-only avoids the unsigned-value sign-extension hazard (a non-constant unsigned operand is left untouched).

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/RuntimeExecTest.cpp` (uses the existing `compileAndRun(source, name)` → `RunResult{exit_code, stdout_output}` harness; copy the include/fixture style of neighboring tests):

```cpp
TEST_F(RuntimeExecTest, IntLiteralWidensToI64InBinaryOps) {
    std::string source = R"LIVA(
func main() {
    let s = "hello"
    let n = len(s) - 1
    print(toString(n))
    if len(s) > 0 {
        print("nonempty")
    }
    let idx = s.indexOf("l")
    let after = idx + 1
    print(toString(after))
}
)LIVA";
    auto r = compileAndRun(source, "int_literal_widen");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("4"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("nonempty"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R IntLiteralWidensToI64InBinaryOps --output-on-failure`
Expected: FAIL — LLVM module verification error (`add i64, i32` / `icmp ... i64 ... i32`).

- [ ] **Step 3: Implement the widening**

In `src/IR/IRGenExpr.cpp`, in `visitBinaryExpr`, **after** `lhs` and `rhs` are evaluated and **before** `bool isFloat = ...` (~line 230), insert:

```cpp
    // Widen a constant i32 operand to match an i64 operand (e.g. len()/indexOf()
    // results mixed with an integer literal). Constant-only: a non-constant
    // operand is left untouched to avoid unsigned sign-extension hazards.
    if (lhs && rhs && lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy() &&
        lhs->getType() != rhs->getType()) {
        unsigned lw = lhs->getType()->getIntegerBitWidth();
        unsigned rw = rhs->getType()->getIntegerBitWidth();
        if (lw < rw && llvm::isa<llvm::ConstantInt>(lhs)) {
            lhs = builder_->CreateSExt(lhs, rhs->getType(), "binop.lwiden");
        } else if (rw < lw && llvm::isa<llvm::ConstantInt>(rhs)) {
            rhs = builder_->CreateSExt(rhs, lhs->getType(), "binop.rwiden");
        }
    }
```

(`llvm/IR/Constants.h` is already transitively included in this file; if `llvm::isa`/`ConstantInt` do not resolve, add `#include "llvm/IR/Constants.h"` at the top with the other LLVM includes.)

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R IntLiteralWidensToI64InBinaryOps --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Run the FULL suite (regression gate)**

Run: `ctest --test-dir build-clang --output-on-failure`
Expected: all pass (2334 baseline + 1 new). If `SelfHostTest.StrSplit*` fails, re-run to confirm it is the known flake, not a regression.

- [ ] **Step 6: Commit**

```bash
git add src/IR/IRGenExpr.cpp tests/unit/RuntimeExecTest.cpp
git commit -m "irgen: widen i32 literal to i64 in mixed-width binary ops"
```

**Fallback if BLOCKED:** if the widening causes regressions that cannot be resolved, the stdlib code in later tasks can instead use explicit `(N as i64)` casts (verified working). Native-backed `Url` (Task 4) needs no index arithmetic, so the rewrites do not strictly depend on this task — but `len(x) > 0` emptiness checks become `len(x) > (0 as i64)` without it.

---

## Task 2: Runtime natives — Url component accessors

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (add near the existing `liva_url_encode` at ~line 4888)
- Modify: `stdlib/runtime/runtime.h` (add near `liva_url_encode` decl at ~line 915)

**Note:** `liva_url_encode`/`liva_url_decode` ALREADY exist — do not re-create them. This task adds only the six parsing accessors.

- [ ] **Step 1: Add the shared parser + six accessors to `runtime.cpp`**

Insert directly above `char *liva_url_encode(const char *data) {` (~line 4888):

```cpp
// Shared URL splitter: scheme://host:port/path?query#fragment (best-effort, no panic).
struct LivaUrlParts {
    std::string scheme, host, path, query, fragment;
    int port = 0;
};

static LivaUrlParts liva_parse_url_parts(const char *url) {
    LivaUrlParts p;
    if (!url) return p;
    std::string s(url);
    size_t hash = s.find('#');
    if (hash != std::string::npos) { p.fragment = s.substr(hash + 1); s = s.substr(0, hash); }
    size_t q = s.find('?');
    if (q != std::string::npos) { p.query = s.substr(q + 1); s = s.substr(0, q); }
    size_t sc = s.find("://");
    if (sc != std::string::npos) { p.scheme = s.substr(0, sc); s = s.substr(sc + 3); }
    std::string authority;
    size_t slash = s.find('/');
    if (slash != std::string::npos) { p.path = s.substr(slash); authority = s.substr(0, slash); }
    else { authority = s; }
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        p.host = authority.substr(0, colon);
        p.port = atoi(authority.substr(colon + 1).c_str());
    } else {
        p.host = authority;
    }
    return p;
}

char *liva_url_scheme(const char *url)   { return strdup_safe(liva_parse_url_parts(url).scheme.c_str()); }
char *liva_url_host(const char *url)     { return strdup_safe(liva_parse_url_parts(url).host.c_str()); }
int32_t liva_url_port(const char *url)   { return (int32_t)liva_parse_url_parts(url).port; }
char *liva_url_path(const char *url)     { return strdup_safe(liva_parse_url_parts(url).path.c_str()); }
char *liva_url_query(const char *url)    { return strdup_safe(liva_parse_url_parts(url).query.c_str()); }
char *liva_url_fragment(const char *url) { return strdup_safe(liva_parse_url_parts(url).fragment.c_str()); }
```

(`strdup_safe`, `<string>`, `atoi` are already used in this file.)

- [ ] **Step 2: Declare them in `runtime.h`**

Insert directly above `char *liva_url_encode(const char *data);` (~line 915):

```cpp
/// URL component accessors (best-effort parse of scheme://host:port/path?query#fragment).
char *liva_url_scheme(const char *url);
char *liva_url_host(const char *url);
int32_t liva_url_port(const char *url);
char *liva_url_path(const char *url);
char *liva_url_query(const char *url);
char *liva_url_fragment(const char *url);
```

- [ ] **Step 3: Bind layer 3 — `createRuntimeDecls()` in `src/IR/IRGen.cpp`**

After the networking block (after `liva_http_req_close` at ~line 723), add:

```cpp
    // URL component accessors: (ptr) -> ptr, except port (ptr) -> i32
    auto *urlStrTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_url_scheme", urlStrTy);
    module_->getOrInsertFunction("liva_url_host", urlStrTy);
    module_->getOrInsertFunction("liva_url_path", urlStrTy);
    module_->getOrInsertFunction("liva_url_query", urlStrTy);
    module_->getOrInsertFunction("liva_url_fragment", urlStrTy);
    auto *urlPortTy = llvm::FunctionType::get(i32Ty, {i8PtrTy}, false);
    module_->getOrInsertFunction("liva_url_port", urlPortTy);
```

- [ ] **Step 4: Bind layer 4 — lowering in `src/IR/IRGenCall.cpp`**

After the `urlDecode` lowering (~line 4588-4595), add (string accessors use `trackStringTemp`; port returns i32 directly):

```cpp
    if ((funcName == "urlScheme" || funcName == "urlHost" || funcName == "urlPath" ||
         funcName == "urlQuery" || funcName == "urlFragment") && !node->getArgs().empty()) {
        auto *urlArg = visit(node->getArgs()[0].get());
        if (!urlArg) return nullptr;
        const char *sym = funcName == "urlScheme" ? "liva_url_scheme"
                        : funcName == "urlHost" ? "liva_url_host"
                        : funcName == "urlPath" ? "liva_url_path"
                        : funcName == "urlQuery" ? "liva_url_query"
                                                 : "liva_url_fragment";
        auto *r = builder_->CreateCall(getOrPanic(sym), {urlArg}, "url.part");
        trackStringTemp(r);
        return r;
    }
    if (funcName == "urlPort" && !node->getArgs().empty()) {
        auto *urlArg = visit(node->getArgs()[0].get());
        if (!urlArg) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_url_port"), {urlArg}, "url.port");
    }
```

- [ ] **Step 5: Bind layer 5 — `src/Sema/TypeChecker.cpp`**

(a) Add the names to the recognized-builtin loop. The encoding builtins live near line 181 (`"urlEncode", "urlDecode"`); add on the next line:

```cpp
                        "urlScheme", "urlHost", "urlPort", "urlPath",
                        "urlQuery", "urlFragment",
```

(b) Add return-type resolution. Near the existing `urlEncode`/`urlDecode` resolution (~line 2606-2609), add:

```cpp
        } else if (ident->getName() == "urlScheme" || ident->getName() == "urlHost" ||
                   ident->getName() == "urlPath" || ident->getName() == "urlQuery" ||
                   ident->getName() == "urlFragment") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "urlPort") {
            node->setResolvedType(makeI32Type());
```

- [ ] **Step 6: Bind layer 6 — `src/Sema/ModuleLoader.cpp`**

The encoding builtins are listed at ~line 125 and ~line 136 (`"urlEncode", "urlDecode", "crc32",`). On the line after EACH occurrence, add:

```cpp
         "urlScheme", "urlHost", "urlPort", "urlPath", "urlQuery", "urlFragment",
```

(These accessors belong to the encoding module list where `urlEncode` lives — keep them together so they resolve without a `std::net` import, matching `urlEncode`.)

- [ ] **Step 7: Register for JIT — `src/JIT/JITEngine.cpp`**

Near the HTTP `REG(...)` block (~line 175), add:

```cpp
    REG(liva_url_scheme);
    REG(liva_url_host);
    REG(liva_url_port);
    REG(liva_url_path);
    REG(liva_url_query);
    REG(liva_url_fragment);
```

- [ ] **Step 8: Add to LSP builtin completion list — `src/LSP/LSPServer.cpp`**

Find the encoding builtins list containing `"base64UrlEncode"` (~line 1208) and add `"urlScheme", "urlHost", "urlPort", "urlPath", "urlQuery", "urlFragment"` to it.

- [ ] **Step 9: Write the binding test**

Add to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST_F(RuntimeExecTest, UrlComponentAccessors) {
    std::string source = R"LIVA(
func main() {
    let u = "https://api.example.com:8080/v1/users?page=2#top"
    print(urlScheme(u))
    print(urlHost(u))
    print(toString(urlPort(u)))
    print(urlPath(u))
    print(urlQuery(u))
    print(urlFragment(u))
}
)LIVA";
    auto r = compileAndRun(source, "url_accessors");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("https"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("api.example.com"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("8080"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("/v1/users"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("page=2"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("top"), std::string::npos);
}
```

- [ ] **Step 10: Build, run the new test, then the full suite**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R UrlComponentAccessors --output-on-failure`
Expected: PASS.
Then: `ctest --test-dir build-clang --output-on-failure` — all pass.

- [ ] **Step 11: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp src/JIT/JITEngine.cpp src/LSP/LSPServer.cpp tests/unit/RuntimeExecTest.cpp
git commit -m "runtime: add native Url component accessors (urlScheme/host/port/path/query/fragment)"
```

---

## Task 3: Runtime natives — HTTP request-with-headers + raw headers + header lookup

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (add near `liva_http_req` at ~line 2487)
- Modify: `stdlib/runtime/runtime.h` (add near `liva_http_req` decl at ~line 444)
- Bind across layers (3-6) + JIT.

- [ ] **Step 1: Add the three natives to `runtime.cpp`**

Insert after `void liva_http_req_close(int64_t handle) { ... }` (~line 2514):

```cpp
// Full request with a CRLF "Name: Value\r\n" header blob (userland-facing).
// Returns i64 handle (0 on failure); caller frees via liva_http_req_close.
int64_t liva_http_req_ex(const char *method, const char *url, const char *body,
                         const char *headers_blob, int64_t timeout_ms) {
    std::vector<std::string> hstore;   // name,value,name,value,... (stable backing)
    if (headers_blob && *headers_blob) {
        std::string raw(headers_blob);
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t eol = raw.find('\n', pos);
            if (eol == std::string::npos) eol = raw.size();
            std::string line = raw.substr(pos, eol - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pos = eol + 1;
            size_t colon = line.find(':');
            if (line.empty() || colon == std::string::npos) continue;
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            size_t vs = 0;
            while (vs < value.size() && value[vs] == ' ') vs++;
            if (vs > 0) value = value.substr(vs);
            hstore.push_back(name);
            hstore.push_back(value);
        }
    }
    int64_t header_count = (int64_t)(hstore.size() / 2);
    std::vector<const char *> hptrs;
    for (auto &s : hstore) hptrs.push_back(s.c_str());
    auto *resp = liva_http_request(method, url, body,
                                   header_count > 0 ? hptrs.data() : nullptr,
                                   header_count, timeout_ms);
    return (int64_t)(uintptr_t)resp;
}

// Reconstruct a CRLF "Name: Value\r\n" header blob from a response handle.
char *liva_http_raw_headers(int64_t handle) {
    auto *resp = (LivaHttpResponse *)(uintptr_t)handle;
    std::string out;
    if (resp) {
        for (int64_t i = 0; i < resp->header_count; i++) {
            if (resp->header_names[i] && resp->header_values[i]) {
                out += resp->header_names[i];
                out += ": ";
                out += resp->header_values[i];
                out += "\r\n";
            }
        }
    }
    char *r = (char *)malloc(out.size() + 1);
    if (r) memcpy(r, out.c_str(), out.size() + 1);
    return r;
}

// Case-insensitive lookup of `name` in a CRLF header blob; malloc'd value or NULL.
char *liva_http_header_lookup(const char *blob, const char *name) {
    if (!blob || !name) return nullptr;
    auto lower = [](std::string s) {
        for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        return s;
    };
    std::string target = lower(std::string(name));
    std::string raw(blob);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        if (eol == std::string::npos) eol = raw.size();
        std::string line = raw.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol + 1;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (lower(line.substr(0, colon)) == target) {
            std::string value = line.substr(colon + 1);
            size_t vs = 0;
            while (vs < value.size() && value[vs] == ' ') vs++;
            if (vs > 0) value = value.substr(vs);
            char *r = (char *)malloc(value.size() + 1);
            if (r) memcpy(r, value.c_str(), value.size() + 1);
            return r;
        }
    }
    return nullptr;
}
```

(`<vector>`, `<string>`, `malloc`, `memcpy` already used in this file.)

- [ ] **Step 2: Declare them in `runtime.h`**

Insert after `void liva_http_req_close(int64_t handle);` (~line 457):

```cpp
/// Full HTTP request with a CRLF header blob; returns i64 handle (0 on failure).
int64_t liva_http_req_ex(const char *method, const char *url, const char *body,
                         const char *headers_blob, int64_t timeout_ms);
/// Reconstruct a CRLF header blob from a response handle (malloc'd).
char *liva_http_raw_headers(int64_t handle);
/// Case-insensitive header lookup in a CRLF blob; malloc'd value or NULL.
char *liva_http_header_lookup(const char *blob, const char *name);
```

- [ ] **Step 3: Bind layer 3 — `createRuntimeDecls()` in `src/IR/IRGen.cpp`**

After `liva_http_req_close` decl (~line 723), add (reuse `httpBodyTy` = `(i64)->i8*` defined at ~line 714):

```cpp
    // httpRequestEx(method, url, body, headersBlob, timeout) -> i64 handle
    auto *httpReqExTy = llvm::FunctionType::get(i64Ty,
        {i8PtrTy, i8PtrTy, i8PtrTy, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_http_req_ex", httpReqExTy);
    // httpRawHeaders(handle) -> char*
    module_->getOrInsertFunction("liva_http_raw_headers", httpBodyTy);
    // httpHeaderLookup(blob, name) -> char* (nullable)
    auto *httpHdrLookupTy = llvm::FunctionType::get(i8PtrTy, {i8PtrTy, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_http_header_lookup", httpHdrLookupTy);
```

- [ ] **Step 4: Bind layer 4 — lowering in `src/IR/IRGenCall.cpp`**

Insert before the `httpClose` lowering (~line 3285):

```cpp
    // httpRequestEx(method, url, body, headersBlob, timeout_ms) -> i64 handle
    if (funcName == "httpRequestEx" && node->getArgs().size() >= 5) {
        auto *methodArg = visit(node->getArgs()[0].get());
        auto *urlArg = visit(node->getArgs()[1].get());
        auto *bodyArg = visit(node->getArgs()[2].get());
        auto *hdrArg = visit(node->getArgs()[3].get());
        auto *timeoutArg = visit(node->getArgs()[4].get());
        if (!methodArg || !urlArg || !bodyArg || !hdrArg || !timeoutArg) return nullptr;
        if (timeoutArg->getType()->isIntegerTy(32))
            timeoutArg = builder_->CreateSExt(timeoutArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_http_req_ex");
        return builder_->CreateCall(fn, {methodArg, urlArg, bodyArg, hdrArg, timeoutArg},
                                    "http.reqex.handle");
    }

    // httpRawHeaders(handle) -> string
    if (funcName == "httpRawHeaders" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_http_raw_headers"), {handleArg}, "http.rawhdr");
        trackStringTemp(r);
        return r;
    }

    // httpHeaderLookup(blob, name) -> string?
    if (funcName == "httpHeaderLookup" && node->getArgs().size() >= 2) {
        auto *blobArg = visit(node->getArgs()[0].get());
        auto *nameArg = visit(node->getArgs()[1].get());
        if (!blobArg || !nameArg) return nullptr;
        auto *result = builder_->CreateCall(getOrPanic("liva_http_header_lookup"),
                                            {blobArg, nameArg}, "http.hdrlookup.raw");
        trackStringTemp(result);
        auto *curFunc = builder_->GetInsertBlock()->getParent();
        auto *isNull = builder_->CreateICmpEQ(result, llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*context_)), "http.hdrlookup.isnull");
        auto *hasVal = builder_->CreateNot(isNull, "http.hdrlookup.hasval");
        auto *optTy = getOptionalType(llvm::PointerType::getUnqual(*context_));
        auto *optAlloca = createEntryBlockAlloca(curFunc, "http.hdrlookup.opt", optTy);
        builder_->CreateStore(hasVal, builder_->CreateStructGEP(optTy, optAlloca, 0));
        builder_->CreateStore(result, builder_->CreateStructGEP(optTy, optAlloca, 1));
        return builder_->CreateLoad(optTy, optAlloca, "http.hdrlookup.result");
    }
```

- [ ] **Step 5: Bind layer 5 — `src/Sema/TypeChecker.cpp`**

(a) In the builtin-name loop, change the http line (~line 74-75) from:

```cpp
                        "httpRequest", "httpStatus", "httpBody",
                        "httpHeader", "httpClose",
```
to (keep the old names for now — they are removed in Task 6):
```cpp
                        "httpRequest", "httpRequestEx", "httpStatus", "httpBody",
                        "httpHeader", "httpRawHeaders", "httpHeaderLookup", "httpClose",
```

(b) In the return-type switch, after the `httpRequest` case (~line 2385), add:

```cpp
        } else if (ident->getName() == "httpRequestEx") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "httpRawHeaders") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "httpHeaderLookup") {
            auto optType = std::make_unique<OptionalTypeRepr>(makeStringType());
            node->setResolvedType(std::move(optType));
```

- [ ] **Step 6: Bind layer 6 — `src/Sema/ModuleLoader.cpp`**

Change the `std::net` list (~line 70-71) to add the three names:

```cpp
    cache_["std::net"] = createBuiltinModule("std::net",
        {"httpGet", "httpPost", "httpPut", "httpPatch", "httpDelete",
         "httpRequest", "httpRequestEx", "httpStatus", "httpBody",
         "httpHeader", "httpRawHeaders", "httpHeaderLookup", "httpClose"});
```

- [ ] **Step 7: Register for JIT — `src/JIT/JITEngine.cpp`**

In the HTTP `REG(...)` block (~line 175-183), add (the handle-API functions used by the new wrapper):

```cpp
    REG(liva_http_req_ex);
    REG(liva_http_raw_headers);
    REG(liva_http_header_lookup);
    REG(liva_http_req_status);
    REG(liva_http_req_body);
    REG(liva_http_req_close);
```

- [ ] **Step 8: Write the binding test**

Add to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST_F(RuntimeExecTest, HttpHeaderLookupCaseInsensitive) {
    std::string source = R"LIVA(
import std::net
func main() {
    let blob = "Content-Type: application/json\r\nX-Count: 7\r\n"
    if let ct = httpHeaderLookup(blob, "content-type") {
        print(ct)
    }
    if let missing = httpHeaderLookup(blob, "Nope") {
        print("FOUND")
    } else {
        print("absent")
    }
}
)LIVA";
    auto r = compileAndRun(source, "http_header_lookup");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("application/json"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("absent"), std::string::npos);
}
```

- [ ] **Step 9: Build, run the new test, then the full suite**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R HttpHeaderLookupCaseInsensitive --output-on-failure`
Expected: PASS. Then full suite — all pass.

- [ ] **Step 10: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp src/JIT/JITEngine.cpp tests/unit/RuntimeExecTest.cpp
git commit -m "runtime: add httpRequestEx/httpRawHeaders/httpHeaderLookup natives"
```

---

## Task 4: Rewrite `net::net` — native-backed `Url`

**Files:**
- Rewrite: `stdlib/net/net.liva`
- Test: `tests/unit/RuntimeExecTest.cpp` (+ `tests/unit/StdlibModuleTest.cpp` type-check)

- [ ] **Step 1: Write the failing RuntimeExec test**

Add to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST_F(RuntimeExecTest, UrlParseAndBuild) {
    std::string source = R"LIVA(
import net::net
func main() {
    let u = Url.parse("https://api.example.com:8080/v1/users?page=2#top")
    print(u.scheme)
    print(u.host)
    print(toString(u.port))
    print(u.path)
    print(u.query)
    print(u.fragment)
    print(u.toString())
    let u2 = Url.parse("http://localhost/api").withQuery("q", "a b").withQuery("n", "1")
    print(u2.toString())
    print(Url.encode("a b&c"))
}
)LIVA";
    auto r = compileAndRun(source, "url_parse_build");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("api.example.com"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("8080"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("page=2"), std::string::npos);
    // withQuery percent-encodes: space -> %20, & -> %26
    EXPECT_NE(r.stdout_output.find("q=a%20b"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("n=1"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("a%20b%26c"), std::string::npos);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R UrlParseAndBuild --output-on-failure`
Expected: FAIL (old `Url` only has `parse`/`toString` over a raw string; no `scheme`/`withQuery`).

- [ ] **Step 3: Rewrite `stdlib/net/net.liva`**

Replace the ENTIRE file with:

```liva
// net::net — URL parsing and building utilities
// Import with: import net::net

import std::net

// =============================================================
// Url — parsed/buildable URL (native-backed parsing)
// =============================================================

pub struct Url {
    var scheme: String      // "http", "https", "" if relative
    var host: String        // host without port
    var port: i32           // 0 if unspecified
    var path: String        // "/..." (may be "")
    var query: String       // raw "k=v&k2=v2" without leading '?'
    var fragment: String    // without leading '#'
}

impl Url {
    pub func parse(s: String) -> Url {
        return Url {
            scheme: urlScheme(s),
            host: urlHost(s),
            port: urlPort(s),
            path: urlPath(s),
            query: urlQuery(s),
            fragment: urlFragment(s),
        }
    }

    pub func encode(s: String) -> String {
        return urlEncode(s)
    }

    pub func decode(s: String) -> String {
        return urlDecode(s)
    }

    // --- immutable builders (self by value -> fresh Url) ---

    pub func withScheme(self, scheme: String) -> Url {
        return Url { scheme: scheme, host: self.host, port: self.port,
                     path: self.path, query: self.query, fragment: self.fragment }
    }

    pub func withHost(self, host: String) -> Url {
        return Url { scheme: self.scheme, host: host, port: self.port,
                     path: self.path, query: self.query, fragment: self.fragment }
    }

    pub func withPort(self, port: i32) -> Url {
        return Url { scheme: self.scheme, host: self.host, port: port,
                     path: self.path, query: self.query, fragment: self.fragment }
    }

    pub func withPath(self, path: String) -> Url {
        return Url { scheme: self.scheme, host: self.host, port: self.port,
                     path: path, query: self.query, fragment: self.fragment }
    }

    pub func withFragment(self, fragment: String) -> Url {
        return Url { scheme: self.scheme, host: self.host, port: self.port,
                     path: self.path, query: self.query, fragment: fragment }
    }

    pub func withQuery(self, key: String, value: String) -> Url {
        var q = self.query
        let pair = "" + urlEncode(key) + "=" + urlEncode(value)
        if len(q) > 0 {
            q = "" + q + "&" + pair
        } else {
            q = pair
        }
        return Url { scheme: self.scheme, host: self.host, port: self.port,
                     path: self.path, query: q, fragment: self.fragment }
    }

    pub func toString(self) -> String {
        var out = ""
        if len(self.scheme) > 0 {
            out = "" + self.scheme + "://" + self.host
        } else {
            out = "" + self.host
        }
        if self.port > 0 {
            out = "" + out + ":" + toString(self.port)
        }
        out = "" + out + self.path
        if len(self.query) > 0 {
            out = "" + out + "?" + self.query
        }
        if len(self.fragment) > 0 {
            out = "" + out + "#" + self.fragment
        }
        return out
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R UrlParseAndBuild --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Replace the net type-check test in `StdlibModuleTest.cpp`**

Search `StdlibModuleTest.cpp` for any `net::net`/`Url`/`Request` test. Replace/add:

```cpp
TEST_F(StdlibModuleTest, ImportNetUrl) {
    auto r = check(
        "import net::net\n"
        "func main() {\n"
        "    let u = Url.parse(\"https://x.com/a?b=1\")\n"
        "    let s = u.scheme\n"
        "    let u2 = u.withQuery(\"k\", \"v\").withPath(\"/c\")\n"
        "    let out = u2.toString()\n"
        "    let e = Url.encode(\"a b\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "net::net Url should type-check";
}
```

If an old `Request`-based net test exists, delete it (Request is removed).

- [ ] **Step 6: Build and run the full suite**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang --output-on-failure`
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add stdlib/net/net.liva tests/unit/RuntimeExecTest.cpp tests/unit/StdlibModuleTest.cpp
git commit -m "net: native-backed Url parser/builder (replaces Url/Request stubs)"
```

---

## Task 5: Rewrite `http::http` — HttpRequest / HttpResponse / HttpClient

**Files:**
- Rewrite: `stdlib/http/http.liva`
- Test: `tests/unit/StdlibModuleTest.cpp` (type-check) + `tests/unit/RuntimeExecTest.cpp` (header-blob)

- [ ] **Step 1: Write the failing tests**

Add the header-blob RuntimeExec test to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST_F(RuntimeExecTest, HttpRequestBuilderAssemblesHeaders) {
    std::string source = R"LIVA(
import http::http
func main() {
    let req = HttpRequest.post("http://x.com")
        .header("Authorization", "Bearer t")
        .json("{}")
    print(req.headers)
    print(req.body)
    print(req.method)
}
)LIVA";
    auto r = compileAndRun(source, "http_req_headers");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("Authorization: Bearer t"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("application/json"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("POST"), std::string::npos);
}
```

Replace the http type-check tests in `StdlibModuleTest.cpp` (the block at ~lines 394-496) with:

```cpp
TEST_F(StdlibModuleTest, ImportHttpModule) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let client = HttpClient.new()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import http::http should resolve HttpClient";
}

TEST_F(StdlibModuleTest, HttpRequestBuilderChain) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let resp = HttpRequest.get(\"http://x.com\")\n"
        "        .header(\"Accept\", \"application/json\")\n"
        "        .query(\"page\", \"2\")\n"
        "        .timeout(5000)\n"
        "        .send()\n"
        "    let s = resp.statusCode()\n"
        "    let ok = resp.isOk()\n"
        "    let body = resp.text()\n"
        "    let ct = resp.header(\"Content-Type\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpRequest builder chain should type-check";
}

TEST_F(StdlibModuleTest, HttpRequestJsonBody) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let resp = HttpRequest.post(\"http://x.com/api\").json(\"{\\\"a\\\":1}\").send()\n"
        "    let cat = resp.is2xx()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpRequest.json + send should type-check";
}

TEST_F(StdlibModuleTest, HttpResponseJsonParse) {
    auto r = check(
        "import http::http\n"
        "import json::json\n"
        "func main() {\n"
        "    let resp = HttpRequest.get(\"http://x.com\").send()\n"
        "    let doc = resp.json()\n"
        "    let k = doc.kind()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpResponse.json() should type-check as JsonValue";
}

TEST_F(StdlibModuleTest, HttpClientDefaultsAndRequest) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let c = HttpClient.withBaseUrl(\"https://api.example.com\")\n"
        "        .withTimeout(5000)\n"
        "        .withHeader(\"Authorization\", \"Bearer t\")\n"
        "    let r1 = c.get(\"/users\")\n"
        "    let r2 = c.post(\"/users\", \"{}\")\n"
        "    let req = c.request(\"GET\", \"/items\").query(\"page\", \"1\").send()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpClient defaults + request seed should type-check";
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R "Http" --output-on-failure`
Expected: FAIL (old API: no `HttpRequest`, no `.json()`, `HttpResponse` has `handle` field).

- [ ] **Step 3: Rewrite `stdlib/http/http.liva`**

Replace the ENTIRE file with:

```liva
// http::http — ergonomic HTTP client (fluent builder + handle-free response)
// Import with: import http::http

import std::net
import net::net
import json::json

// =============================================================
// HttpResponse — handle-free; send() eager-copies everything
// =============================================================

pub struct HttpResponse {
    var status: i32
    var body: String
    var rawHeaders: String
    var ok: bool
}

impl HttpResponse {
    pub func statusCode(self) -> i32 {
        return self.status
    }

    pub func text(self) -> String {
        return self.body
    }

    pub func isOk(self) -> bool {
        return self.ok
    }

    pub func is2xx(self) -> bool {
        return self.status >= 200 && self.status < 300
    }

    pub func is3xx(self) -> bool {
        return self.status >= 300 && self.status < 400
    }

    pub func is4xx(self) -> bool {
        return self.status >= 400 && self.status < 500
    }

    pub func is5xx(self) -> bool {
        return self.status >= 500 && self.status < 600
    }

    pub func header(self, name: String) -> String? {
        return httpHeaderLookup(self.rawHeaders, name)
    }

    pub func json(self) -> JsonValue {
        return Json.parse(self.body)
    }
}

// =============================================================
// HttpRequest — fluent immutable builder
// =============================================================

pub struct HttpRequest {
    var method: String
    var url: String
    var body: String
    var headers: String     // "Name: Value\r\n" blob
    var timeoutMs: i64
}

impl HttpRequest {
    pub func get(url: String) -> HttpRequest {
        return HttpRequest { method: "GET", url: url, body: "", headers: "", timeoutMs: 30000 }
    }

    pub func post(url: String) -> HttpRequest {
        return HttpRequest { method: "POST", url: url, body: "", headers: "", timeoutMs: 30000 }
    }

    pub func put(url: String) -> HttpRequest {
        return HttpRequest { method: "PUT", url: url, body: "", headers: "", timeoutMs: 30000 }
    }

    pub func patch(url: String) -> HttpRequest {
        return HttpRequest { method: "PATCH", url: url, body: "", headers: "", timeoutMs: 30000 }
    }

    pub func delete(url: String) -> HttpRequest {
        return HttpRequest { method: "DELETE", url: url, body: "", headers: "", timeoutMs: 30000 }
    }

    pub func header(self, name: String, value: String) -> HttpRequest {
        let h = "" + self.headers + name + ": " + value + "\r\n"
        return HttpRequest { method: self.method, url: self.url, body: self.body,
                             headers: h, timeoutMs: self.timeoutMs }
    }

    pub func query(self, key: String, value: String) -> HttpRequest {
        var u = self.url
        let pair = "" + urlEncode(key) + "=" + urlEncode(value)
        if u.contains("?") {
            u = "" + u + "&" + pair
        } else {
            u = "" + u + "?" + pair
        }
        return HttpRequest { method: self.method, url: u, body: self.body,
                             headers: self.headers, timeoutMs: self.timeoutMs }
    }

    pub func body(self, content: String) -> HttpRequest {
        return HttpRequest { method: self.method, url: self.url, body: content,
                             headers: self.headers, timeoutMs: self.timeoutMs }
    }

    pub func json(self, content: String) -> HttpRequest {
        let h = "" + self.headers + "Content-Type: application/json\r\n"
        return HttpRequest { method: self.method, url: self.url, body: content,
                             headers: h, timeoutMs: self.timeoutMs }
    }

    pub func timeout(self, ms: i64) -> HttpRequest {
        return HttpRequest { method: self.method, url: self.url, body: self.body,
                             headers: self.headers, timeoutMs: ms }
    }

    pub func send(self) -> HttpResponse {
        let handle = httpRequestEx(self.method, self.url, self.body, self.headers, self.timeoutMs)
        if handle == 0 {
            return HttpResponse { status: 0, body: "", rawHeaders: "", ok: false }
        }
        let status = httpStatus(handle)
        let body = httpBody(handle)
        let rawHeaders = httpRawHeaders(handle)
        httpClose(handle)
        return HttpResponse {
            status: status,
            body: body,
            rawHeaders: rawHeaders,
            ok: status >= 200 && status < 300,
        }
    }
}

// =============================================================
// HttpClient — reusable defaults (baseUrl, headers, timeout)
// =============================================================

pub struct HttpClient {
    var baseUrl: String
    var timeoutMs: i64
    var defaultHeaders: String
}

impl HttpClient {
    pub func new() -> HttpClient {
        return HttpClient { baseUrl: "", timeoutMs: 30000, defaultHeaders: "" }
    }

    pub func withBaseUrl(url: String) -> HttpClient {
        return HttpClient { baseUrl: url, timeoutMs: 30000, defaultHeaders: "" }
    }

    pub func withTimeout(self, ms: i64) -> HttpClient {
        return HttpClient { baseUrl: self.baseUrl, timeoutMs: ms, defaultHeaders: self.defaultHeaders }
    }

    pub func withHeader(self, name: String, value: String) -> HttpClient {
        let h = "" + self.defaultHeaders + name + ": " + value + "\r\n"
        return HttpClient { baseUrl: self.baseUrl, timeoutMs: self.timeoutMs, defaultHeaders: h }
    }

    pub func request(self, method: String, path: String) -> HttpRequest {
        let fullUrl = "" + self.baseUrl + path
        return HttpRequest { method: method, url: fullUrl, body: "",
                             headers: self.defaultHeaders, timeoutMs: self.timeoutMs }
    }

    pub func get(self, path: String) -> HttpResponse {
        return self.request("GET", path).send()
    }

    pub func post(self, path: String, body: String) -> HttpResponse {
        return self.request("POST", path).body(body).send()
    }

    pub func put(self, path: String, body: String) -> HttpResponse {
        return self.request("PUT", path).body(body).send()
    }

    pub func patch(self, path: String, body: String) -> HttpResponse {
        return self.request("PATCH", path).body(body).send()
    }

    pub func delete(self, path: String) -> HttpResponse {
        return self.request("DELETE", path).send()
    }
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R "Http" --output-on-failure`
Expected: PASS. (Note: the chain `HttpRequest.post(url).header(..).json(..)` requires Task 1's literal-widening only for `len()` uses — there are none here; chaining itself was verified working.)

- [ ] **Step 5: Run the full suite**

Run: `ctest --test-dir build-clang --output-on-failure`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add stdlib/http/http.liva tests/unit/RuntimeExecTest.cpp tests/unit/StdlibModuleTest.cpp
git commit -m "http: fluent immutable HttpRequest builder + handle-free HttpResponse + HttpClient defaults"
```

---

## Task 6: Remove the old HTTP builtins + migrate remaining consumers

**Files:**
- `src/Sema/TypeChecker.cpp`, `src/Sema/ModuleLoader.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/JIT/JITEngine.cpp`, `src/LSP/LSPServer.cpp`
- `stdlib/runtime/runtime.cpp`, `stdlib/runtime/runtime.h`
- `tests/unit/SemaTest.cpp`, `tests/unit/LSPTest.cpp`

**Removed builtins:** `httpGet`, `httpPost`, `httpPut`, `httpPatch`, `httpDelete`, `httpRequest` (old 4-arg), `httpHeader` (handle-based). Their natives `liva_http_get/post/put/patch/delete`, `liva_http_req`, `liva_http_req_header` are removed too. Retained: `httpStatus`/`httpBody`/`httpClose` (+ `liva_http_response_*` internals used by them).

> **Note:** `src/Driver/PackageManager.cpp` uses its own C++ `httpGetFn` function pointer — NOT the Liva builtin. Do not touch it.

- [ ] **Step 1: Grep to confirm no remaining Liva-side references**

Run: `grep -rn "httpGet\|httpPost\|httpPut\|httpPatch\|httpDelete\|httpRequest\b\|httpHeader\b" stdlib/ tests/ examples/ docs/ --include=*.liva`
Expected: no `.liva` references remain (Tasks 4-5 removed them). Note any doc/example hits for Task 7.

- [ ] **Step 2: Remove from `runtime.cpp` / `runtime.h`**

Delete the implementations `liva_http_get`, `liva_http_post`, `liva_http_put`, `liva_http_patch`, `liva_http_delete` (~lines 2382-2443), `liva_http_req` (~line 2487-2494), and `liva_http_req_header` (~line 2506-2509) from `runtime.cpp`, plus their decls in `runtime.h` (~lines 403-419, 444, 454). Keep `liva_http_request`, `liva_http_response_*`, `liva_http_req_status/body/close`, `liva_http_req_ex`, `liva_http_raw_headers`, `liva_http_header_lookup`. (Keep the WinHTTP `winhttp_request_simple`/curl `curl_request_simple` only if still referenced; if they are now dead, remove them — verify with a grep before deleting.)

- [ ] **Step 3: Remove from binding layers**

- `src/IR/IRGen.cpp` (~lines 691-707): delete the `getOrInsertFunction` calls for `liva_http_get`, `liva_http_post`, `liva_http_put`, `liva_http_patch`, `liva_http_delete`, `liva_http_req`.
- `src/IR/IRGenCall.cpp` (~lines 3131-3246): delete the `httpGet`/`httpPost`/`httpPut`/`httpPatch`/`httpDelete`/`httpRequest` lowering blocks, and the `httpHeader` block (~lines 3266-3283).
- `src/Sema/TypeChecker.cpp`: in the builtin-name loop remove `"httpGet", "httpPost", "httpPut", "httpPatch", "httpDelete"`, `"httpRequest"`, `"httpHeader"`; in the return-type switch remove the `httpGet|httpPost|httpPut|httpPatch|httpDelete` branch, the `httpRequest` branch, and the `httpHeader` branch.
- `src/Sema/ModuleLoader.cpp` (~line 70): reduce the `std::net` list to `{"httpRequestEx", "httpStatus", "httpBody", "httpRawHeaders", "httpHeaderLookup", "httpClose"}`.
- `src/JIT/JITEngine.cpp` (~lines 175-183): remove `REG(liva_http_get/post/put/patch/delete)` and `REG(liva_http_response_header)`. Keep `REG(liva_http_response_free/status/body)` (used internally) and the new REGs from Task 3.
- `src/LSP/LSPServer.cpp` (~line 1187): replace the Network builtin line `"httpGet", "httpPost", "httpPut", "httpPatch", "httpDelete",` with `"httpRequestEx", "httpStatus", "httpBody", "httpRawHeaders", "httpHeaderLookup", "httpClose",`.

- [ ] **Step 4: Update the LSP auto-import map — `src/LSP/LSPServer.cpp` (~line 2714-2716)**

Change:
```cpp
                    // http::http wrapper types
                    {"HttpClient", "http::http"}, {"HttpResponse", "http::http"},
                    {"HttpHeaders", "http::http"},
```
to:
```cpp
                    // http::http wrapper types
                    {"HttpClient", "http::http"}, {"HttpResponse", "http::http"},
                    {"HttpRequest", "http::http"},
                    // net::net wrapper types
                    {"Url", "net::net"},
```

- [ ] **Step 5: Fix any SemaTest/LSPTest references**

Run: `grep -rn "httpGet\|httpRequest\b\|httpHeader\b\|HttpHeaders\|net::Request" tests/unit/SemaTest.cpp tests/unit/LSPTest.cpp`
For each hit, update to the new API (e.g. an LSP completion test expecting `httpGet` should expect `httpRequestEx`; a `HttpHeaders` reference should be removed). If a test specifically asserted an old builtin's existence, rewrite it to assert the new builtin.

- [ ] **Step 6: Build and run the full suite**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang --output-on-failure`
Expected: all pass. A `getOrPanic` crash means a lowering still references a removed symbol — grep and fix.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "http: remove legacy string-based http builtins; update LSP auto-import for Url/HttpRequest"
```

---

## Task 7: Docs + live-network opt-in test

**Files:**
- `docs/en/API-REFERENCE.md`, `docs/tr/API-REFERENCE.md`
- `docs/en/README.md`, `docs/tr/README.md`
- `docs/en/COOKBOOK.md`, `docs/tr/COOKBOOK.md`
- `docs/en/TUTORIAL.md`, `docs/tr/TUTORIAL.md`, `docs/en/LANGUAGE-REFERENCE.md`, `docs/tr/LANGUAGE-REFERENCE.md` (only if they reference the old http/net API)
- `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Rewrite the HTTP + net sections in API-REFERENCE (en + tr)**

Grep each doc for the old API (`HttpClient.get`, `getFull`, `HttpHeaders`, `net::Request`, `httpGet`). Replace with the new API: `Url.parse/withQuery/encode/toString`, `HttpRequest.get(url).header().query().json().timeout().send()`, `HttpResponse.statusCode/text/isOk/isNxx/header/json`, `HttpClient.withBaseUrl().withHeader().withTimeout().request()/get()/post()`. Document the eager-copy/no-Drop model and the `json()` "bind to a let" contract.

Example block to include:

````markdown
```liva
import http::http
import json::json

let resp = HttpRequest.get("https://api.example.com/users")
    .header("Authorization", "Bearer TOKEN")
    .query("page", "2")
    .timeout(5000)
    .send()

if resp.is2xx() {
    let doc = resp.json()        // bind it — JsonValue owns its document
    // ... read doc ...
}
```
````

- [ ] **Step 2: Refresh README + COOKBOOK + TUTORIAL/LANGUAGE-REFERENCE blurbs (en + tr)**

Update any module blurb that describes `http::http`/`net::net` to mention the fluent builder, request headers, JSON integration, and the `Url` type. Fix any code snippet using the removed API.

- [ ] **Step 3: Add the live-network opt-in test**

Add to `tests/unit/RuntimeExecTest.cpp` (modeled on `RuntimeExecTest.PgRealRoundTrip` env gating):

```cpp
TEST_F(RuntimeExecTest, HttpLiveRoundTrip) {
    if (std::getenv("LIVA_HTTP_TEST") == nullptr) {
        GTEST_SKIP() << "Set LIVA_HTTP_TEST=1 to run the live HTTP round-trip test";
    }
    std::string source = R"LIVA(
import http::http
func main() {
    let resp = HttpRequest.get("https://example.com").send()
    if resp.is2xx() {
        if len(resp.text()) > 0 {
            print("OK")
        }
    }
}
)LIVA";
    auto r = compileAndRun(source, "http_live");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("OK"), std::string::npos);
}
```

- [ ] **Step 4: Build and run the full suite (live test skipped by default)**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang --output-on-failure`
Expected: all pass; `HttpLiveRoundTrip` shows as SKIPPED.

- [ ] **Step 5: Commit**

```bash
git add docs/ tests/unit/RuntimeExecTest.cpp
git commit -m "docs: rewrite http/net sections for new API; add opt-in live HTTP test"
```

---

## Final review

After all tasks: dispatch a final code reviewer over the whole branch diff (per subagent-driven-development), then use `superpowers:finishing-a-development-branch`. Verify the full serial suite is green (2334 baseline + new tests; live test skipped) and re-run once if `SelfHostTest.StrSplit*` flakes.
