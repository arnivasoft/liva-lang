# net + http Redesign — Design Spec

**Date:** 2026-06-04
**Status:** Approved (brainstorming complete)
**Scope:** Replace the stub `net::net` and the thin `http::http` wrapper with a cohesive HTTP-client + URL subsystem: a real URL parser/builder, a fluent request builder, request-header support, JSON integration, and a leak-free response model.

---

## Goals

1. **Request headers** — let callers attach custom headers (Authorization, Content-Type, …). The runtime (`curl_request_full` / `winhttp_request_full`) already accepts headers; only the wrapper needs to wire them through.
2. **JSON integration** — `response.json()` parses the body into the new `json::json` DOM; `.json(body)` on the request sets the body and `Content-Type: application/json`.
3. **Fluent request builder** — `HttpRequest.get(url).header(..).query(..).timeout(..).send()`, with an `HttpClient` carrying reusable defaults (baseUrl, default headers, default timeout).
4. **Automatic resource management** — `HttpResponse` holds **no** native handle; `send()` eagerly copies status/body/headers and frees the handle immediately. No `Drop`, no leak, safe to chain off a temporary.

This is a **breaking** replacement (same approach as the JSON redesign). Old API surface is removed, not deprecated.

---

## Architecture

Two cooperating modules:

- **`net::net`** — pure, I/O-free URL utilities. `Url` becomes a real parser/builder. The only native surface is percent-encode/decode.
- **`http::http`** — the HTTP client, built on `net::Url` and runtime natives. Contains `HttpRequest` (fluent builder), `HttpClient` (defaults), and `HttpResponse` (handle-free value).

`net::net` has no dependency on `http`; `http::http` imports `net::net` and `json::json`.

### Critical compiler constraint (verified 2026-06-04)

`return self` from a `mut self` method that returns the struct **by value** fails LLVM verification (`ret ptr %self` vs struct return type) — the receiver is a pointer and is returned as-is instead of being loaded. **Therefore all chainable builder methods take `self` by value (NOT `mut self`) and return a freshly-constructed struct** copying all fields and modifying one. This immutable-builder pattern was compiled and run successfully:

```liva
pub func header(self, name: String, value: String) -> HttpRequest {
    return HttpRequest { method: self.method, url: self.url, body: self.body,
                         headers: "" + self.headers + name + ": " + value + "\r\n",
                         timeoutMs: self.timeoutMs }
}
```

We do **not** fix the compiler `return self` limitation here (out of scope; the immutable pattern is idiomatic and sufficient).

---

## `net::net` — Url

```liva
pub struct Url {
    var scheme: String      // "http", "https", "" if relative
    var host: String
    var port: i32           // 0 if unspecified
    var path: String        // "/..." (may be "")
    var query: String       // raw "k=v&k2=v2" without leading '?', already encoded
    var fragment: String    // without leading '#'
}
```

**Static / construction**
- `Url.parse(s: String) -> Url` — splits `scheme://host:port/path?query#fragment` using Liva string operations. Missing components become empty / port 0. Robust to no-scheme, no-port, no-query, no-fragment.
- `Url.encode(s: String) -> String` — percent-encode a single component (native `urlEncode`).
- `Url.decode(s: String) -> String` — percent-decode (native `urlDecode`).

**Builder (immutable, chainable — `self` by value, returns new `Url`)**
- `withScheme(scheme: String) -> Url`
- `withHost(host: String) -> Url`
- `withPort(port: i32) -> Url`
- `withPath(path: String) -> Url`
- `withFragment(fragment: String) -> Url`
- `withQuery(key: String, value: String) -> Url` — appends `encode(key)=encode(value)` to `query`, joining with `&` when `query` is non-empty.

**Serialization**
- `toString() -> String` — reassembles: `scheme://host[:port][path][?query][#fragment]`. Omits empty parts. If `scheme` is empty, emits `host`/`path` without `scheme://`.

Parsing and reassembly are pure Liva string code; only `encode`/`decode` are native (hex conversion is error-prone in-language).

---

## `http::http` — HttpRequest (fluent builder)

```liva
pub struct HttpRequest {
    var method: String
    var url: String
    var body: String
    var headers: String     // "Name: Value\r\n" blob (same format as the runtime splits)
    var timeoutMs: i64
}
```

**Static constructors** (each returns a fresh `HttpRequest`, default `timeoutMs = 30000`, empty headers/body):
- `HttpRequest.get(url: String) -> HttpRequest`
- `HttpRequest.post(url: String) -> HttpRequest`
- `HttpRequest.put(url: String) -> HttpRequest`
- `HttpRequest.patch(url: String) -> HttpRequest`
- `HttpRequest.delete(url: String) -> HttpRequest`

**Chainable methods** (immutable — `self` by value, return new `HttpRequest`):
- `header(name: String, value: String) -> HttpRequest` — appends `name: value\r\n` to `headers`.
- `query(key: String, value: String) -> HttpRequest` — appends an encoded query param to `url` (`?` if no `?` present yet, else `&`), using `Url.encode`.
- `body(content: String) -> HttpRequest` — sets the raw body.
- `json(content: String) -> HttpRequest` — sets `body = content` and appends `Content-Type: application/json\r\n` to `headers`.
- `timeout(ms: i64) -> HttpRequest` — sets `timeoutMs`.

**Execution**
- `send(self) -> HttpResponse` — calls `httpRequestEx(method, url, body, headers, timeoutMs)` → handle; reads `httpStatus`, `httpBody`, `httpRawHeaders`; calls `httpClose`; returns an `HttpResponse` value. The builder holds no native resource, so chaining off a temporary (`HttpRequest.get(u).send()`) is safe.

---

## `http::http` — HttpResponse (handle-free, eager-copy)

```liva
pub struct HttpResponse {
    var status: i32
    var body: String
    var rawHeaders: String    // CRLF-joined header block captured at send() time
    var ok: bool
}
```

- `statusCode() -> i32`
- `text() -> String` — returns `body`.
- `isOk() -> bool` — `status` in `[200,300)`.
- `is2xx() / is3xx() / is4xx() / is5xx() -> bool`
- `header(name: String) -> String?` — native `httpHeaderLookup(rawHeaders, name)`, case-insensitive; `nil` if absent.
- `json() -> JsonValue` — `Json.parse(self.body)`. The returned `JsonValue` owns its document (owns:true) — **bind it to a `let`/`var`** (same contract as `json::json`). On parse failure, returns a `JsonValue` of kind `Null`.

No `Drop`: `send()` already freed the native handle.

---

## `http::http` — HttpClient (defaults)

```liva
pub struct HttpClient {
    var baseUrl: String
    var timeoutMs: i64
    var defaultHeaders: String   // "Name: Value\r\n" blob
}
```

- `HttpClient.new() -> HttpClient` — empty baseUrl, `timeoutMs = 30000`, empty defaultHeaders.
- `HttpClient.withBaseUrl(url: String) -> HttpClient`
- `withTimeout(self, ms: i64) -> HttpClient` — immutable, returns new client.
- `withHeader(self, name: String, value: String) -> HttpClient` — immutable, appends to `defaultHeaders`.
- `request(self, method: String, path: String) -> HttpRequest` — seeds an `HttpRequest` with `baseUrl + path` as the url, `defaultHeaders` as headers, `timeoutMs`. Caller chains `.header()/.query()/.json()/.body()` then `.send()`.
- Convenience (seed + send): `get(path) -> HttpResponse`, `post(path, body) -> HttpResponse`, `put(path, body) -> HttpResponse`, `patch(path, body) -> HttpResponse`, `delete(path) -> HttpResponse`.

`baseUrl + path` concatenation is plain string concat (caller supplies a `path` that begins with `/` or a full URL; no normalization beyond concat — documented).

---

## Data flow (send)

```
HttpRequest{method,url,body,headers,timeoutMs}
  → httpRequestEx(method, url, body, headers, timeoutMs) : i64 handle   (0 on failure)
  → status     = httpStatus(handle)        : i32
  → body       = httpBody(handle)           : String
  → rawHeaders = httpRawHeaders(handle)     : String
  → httpClose(handle)                       : void   (handle freed here)
  → HttpResponse{status, body, rawHeaders, ok: status>=200 && status<300}
```

---

## Runtime natives

### New
| Liva builtin | C symbol | Signature | Notes |
|---|---|---|---|
| `urlEncode` | `liva_url_encode` | `(char* s) -> char*` | percent-encode a component (RFC 3986 unreserved kept) |
| `urlDecode` | `liva_url_decode` | `(char* s) -> char*` | percent-decode |
| `httpRequestEx` | `liva_http_req_ex` | `(char* method, char* url, char* body, char* headersBlob, i64 timeout) -> i64` | splits `headersBlob` on `\r\n` into name/value pairs, calls existing `curl_request_full`/`winhttp_request_full`; returns handle (0 on failure) |
| `httpRawHeaders` | `liva_http_raw_headers` | `(i64 handle) -> char*` | CRLF-joined header block from the parsed response |
| `httpHeaderLookup` | `liva_http_header_lookup` | `(char* blob, char* name) -> char*` | case-insensitive lookup in a CRLF header blob; `nullptr` if absent (→ `String?`) |

`httpRequestEx` reuses the already-present full-request implementations (`curl_request_full` takes `const char **headers, int64_t header_count`; the new native builds that array by splitting the blob). The header blob format (`Name: Value\r\n`) is shared between request assembly and response capture.

### Retained
- `httpStatus` → `liva_http_req_status`
- `httpBody` → `liva_http_req_body`
- `httpClose` → `liva_http_req_close`

### Removed (breaking)
- `httpGet`/`httpPost`/`httpPut`/`httpPatch`/`httpDelete` → `liva_http_get/post/put/patch/delete`
- `httpRequest` (old 4-arg, no headers) → `liva_http_req`
- `httpHeader` (handle-based lazy lookup) → `liva_http_req_header`

Removal touches all six binding layers plus JIT registration (`src/JIT/JITEngine.cpp`) and LSP builtin lists. The underlying `liva_http_response_*` helpers may be retained if still referenced internally, but their Liva-facing builtins are removed.

### Six-layer binding (per new native)
1. `stdlib/runtime/runtime.cpp` — implementation
2. `stdlib/runtime/runtime.h` — declaration
3. `src/IR/IRGen.cpp` `createRuntimeDecls()` — `getOrInsertFunction` (omission → `getOrPanic` crash)
4. `src/IR/IRGenCall.cpp` — lowering (`if (funcName == ...)`), incl. bool ZExt/Trunc and i64-index rules where relevant
5. `src/Sema/TypeChecker.cpp` — builtin-name loop (~line 72) + return-type switch (~line 2379)
6. `src/Sema/ModuleLoader.cpp` — `std::net`/builtin list (~line 70)

---

## Error handling

- **Network failure / bad host:** `httpRequestEx` returns handle `0`. `send()` detects `handle == 0` and returns `HttpResponse{status:0, body:"", rawHeaders:"", ok:false}`. No exceptions.
- **`header(name)` absent:** native returns `nullptr` → `String?` nil.
- **`json()` on a non-JSON body:** `Json.parse` returns a `JsonValue` of kind `Null`; caller inspects `kind()` / `isNull()`.
- **`Url.parse` on a malformed string:** best-effort; unparsed remainder lands in `path`. No panic.

---

## Testing

Mirrors the existing convention: stdlib API tests are **type-check only** (no live network); pure logic gets RuntimeExec tests; live network is opt-in.

- **net (RuntimeExec, no network):**
  - `Url.parse` of a full URL → component round-trip via `toString`.
  - `withQuery` appends encoded params: space → `%20` (RFC 3986 percent-encoding, **not** `+`), `&`/`=`/`/` etc. percent-encoded; unreserved (`A-Za-z0-9-._~`) kept verbatim.
  - `Url.encode` / `Url.decode` round-trip.
- **http (type-check):**
  - `HttpRequest.get(u).header(..).query(..).json(..).timeout(..).send()` chain compiles and types as `HttpResponse`.
  - `HttpClient.new()/.withBaseUrl()/.withHeader()/.request()/.get()` compile.
  - `HttpResponse` accessors (`statusCode/text/isOk/isNxx/header/json`) compile; `json()` types as `JsonValue`.
- **http (RuntimeExec, no network):**
  - header-blob assembly: build a request, assert the `headers` field equals the expected `Name: Value\r\n…` blob.
  - `httpHeaderLookup` correctness: case-insensitive hit, miss → nil, value trimming.
- **Live network:** gated on env var `LIVA_HTTP_TEST` (default skipped), one GET round-trip asserting `is2xx()` and a non-empty body — modeled on `RuntimeExecTest.PgRealRoundTrip`.

All tests run **serially** (`ctest --test-dir build-clang --output-on-failure`, no `-j`).

---

## Migration / breaking changes

Update every consumer of the old API:
- `stdlib/http/http.liva`, `stdlib/net/net.liva` — rewritten.
- `tests/unit/StdlibModuleTest.cpp` — replace old http/net type-check tests with new-API equivalents.
- `tests/unit/SemaTest.cpp`, `tests/unit/LSPTest.cpp` — update any http/net references.
- `src/LSP/LSPServer.cpp` — builtin lists, auto-import mapping, completions/hover for the new types.
- `src/JIT/JITEngine.cpp` — drop removed `REG(...)` entries, add new ones.
- Docs: `docs/{en,tr}/API-REFERENCE.md`, `README.md`, `COOKBOOK.md`, `TUTORIAL.md`, `LANGUAGE-REFERENCE.md` — rewrite the http/net sections.
- Editor highlighters (`editors/...`) — refresh builtin word lists (cosmetic; lowest priority).
- `src/Driver/PackageManager.cpp` uses its own `httpGetFn` (a C++ function pointer, **not** the Liva builtin) — **unaffected**, do not touch.

---

## Out of scope (YAGNI)

- Streaming / chunked request or response bodies.
- multipart/form-data file upload.
- Cookie jar / session persistence.
- Redirect configuration (curl's default follow-redirects stays).
- Async / concurrent requests.
- Retry / backoff.
- WebSocket (separate module).
- Fixing the compiler `return self` limitation (use the immutable-builder pattern instead).
- `Url` userinfo (`user:pass@`) parsing.
