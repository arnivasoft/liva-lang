# WebSocket Redesign — Design Spec

**Date:** 2026-06-05
**Status:** Approved (brainstorming complete)
**Scope:** Replace the minimal text-only `websocket::websocket` wrapper with a full-featured WebSocket client: binary frames, a fluent connect builder with custom headers + subprotocol, JSON integration, `Drop`-based auto-close, WinHTTP auto-keepalive (ping/pong), and transparent auto-reconnect.

---

## Goals

1. **Binary frames** — `sendBinary([u8])` and a `recv()` that exposes message type (text vs binary), so a binary message is never silently decoded as text.
2. **Connect options** — a fluent builder for custom request headers (e.g. Authorization) and `Sec-WebSocket-Protocol` subprotocol.
3. **JSON integration** — `WsMessage.json() -> JsonValue` (parse) and `WebSocket.sendJson(JsonValue)` (serialize + send), over the `json::json` DOM.
4. **Auto resource management** — `WebSocket` conforms to `Drop`; the connection auto-closes at scope end (no manual `close()` required). A WebSocket connection is a genuinely long-lived owned resource, so `Drop` (not the HTTP eager-copy model) is the right fit.
5. **Keepalive (ping/pong)** — configurable auto-keepalive interval. WinHTTP sends keepalive ping frames automatically and validates pongs; it also auto-responds to inbound pings.
6. **Transparent auto-reconnect** — on a dropped connection, `send`/`recv` transparently reconnect using stored connect parameters, governed by a `(maxRetries, backoffMs)` policy.

This is a **breaking** redesign (same approach as the JSON and net/http redesigns): `recv()` changes from `String?` to `WsMessage?`, and the legacy text-only natives are replaced.

---

## Platform & feasibility notes (verified 2026-06-05)

- **Windows-backed only.** Implementation uses WinHTTP `WinHttpWebSocket*`. Non-Windows builds keep the existing stub (every native returns 0/nullptr/false). Adding a POSIX backend is out of scope.
- **Manual WS ping frames are NOT possible via WinHTTP.** `WinHttpWebSocketSend` accepts only UTF8/BINARY/CLOSE buffer types — there is no "send a ping frame" call. Keepalive is therefore done via `WinHttpSetOption(hWebSocket, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &ms, sizeof(ms))`, which makes WinHTTP emit keepalive pings automatically. **Minimum interval is 15000 ms** (WinHTTP clamps/rejects lower); the wrapper clamps any `0 < ms < 15000` to 15000 and documents it. App-level "ping messages" (a text/binary payload the server must understand) are explicitly out of scope.
- **`Drop` reliability dictates a non-optional `connect`.** Verified by compile+run: Liva runs `drop` per **named struct variable**, but (a) unwrapping an `Optional` with `if let` does **not** run the inner value's `drop` (so a `connect() -> WebSocket?` + `if let` path would never auto-close — a leak), and (b) explicit aliasing (`let b = a`) **copies** the struct and drops **both** (double-free for a handle). Therefore `connect` returns a **non-optional `WebSocket`** (failure → `handle = 0`, `isOpen()` returns false), so the result is a plain named variable whose `drop` fires exactly once at scope end. Method calls take `ref self`/`mut self` (no copy, no extra drop — verified). The remaining footgun, copying the socket (`let b = ws`), is documented as unsupported (same single-owner contract as json's `Drop`). `recv()` still returns `WsMessage?` — safe, because `WsMessage` is a non-`Drop` view.
- **`Drop` protocol is sourced from `json::json`.** `Drop` is declared `pub protocol Drop` in `stdlib/json/json.liva`. A duplicate `protocol Drop` in another module that ends up in the same compilation is a hard "redefinition of 'Drop'" error (verified). Since `websocket::websocket` imports `json::json` anyway (for JSON integration), it **reuses** json's `Drop` — `import json::json` brings `Drop` into scope and `impl WebSocket: Drop {…}` resolves and fires at scope end (verified by compile+run). No new core module and no change to json.liva. (Future option: hoist `Drop` to a shared core module; out of scope here.)
- **`[u8]` bridge is proven.** `stdlib/sqlite/sqlite.liva` already passes `[u8]` into a native (`bindBlob(data: [u8])` → `sqliteBindBlob`) and returns `[u8]` from a native (`columnBlob() -> [u8]` → `sqliteColumnBlob`). The WS binary natives (`wsSendBinary`, `wsMsgBytes`) are modeled on those exact two functions.
- **`mut self` returning a value works.** `recv(mut self) -> WsMessage?` / `send(mut self) -> bool` mutate `self.handle` (on reconnect) and return a value — verified via the existing `json` precedent `setObject(mut self) -> JsonObject`. (Only `mut self` returning the **same struct type by value** is broken — not used here.)

---

## Architecture

A single rewritten module `websocket::websocket`, Windows-backed over WinHTTP, depending on `std::websocket` (natives) and `json::json` (JsonValue + the `Drop` protocol). Four units:

- **`WsClient`** — immutable fluent builder that collects connect options and opens the connection.
- **`WebSocket`** — the owned connection; `Drop`-closing; carries stored connect params for reconnect.
- **`WsMessage`** — a lightweight, non-owning view of the last received message (valid until the next `recv()`).
- Runtime `liva_ws_*` natives + an extended `LivaWebSocket` struct (adds an internal receive buffer).

Chainable builder methods take **`self` by value** is NOT used — per the net/http finding, builders take **`ref self`** and return a freshly-constructed struct (String fields are deep-copied on struct-literal init).

---

## `WsClient` — fluent connect builder

```liva
pub struct WsClient {
    var url: String
    var headers: String        // "Name: Value\r\n" blob
    var subprotocol: String    // "" if none
    var keepAliveMs: i64       // 0 = off; else WinHTTP keepalive interval (clamped ≥ 15000)
    var maxRetries: i32        // 0 = no auto-reconnect
    var backoffMs: i64         // delay between reconnect attempts
}
```

- `WsClient.to(url: String) -> WsClient` — static; defaults `headers="", subprotocol="", keepAliveMs=0, maxRetries=0, backoffMs=1000`.
- `header(ref self, name: String, value: String) -> WsClient` — appends `name: value\r\n`.
- `subprotocol(ref self, proto: String) -> WsClient` — sets `Sec-WebSocket-Protocol`.
- `keepAlive(ref self, ms: i64) -> WsClient` — enable WinHTTP auto-keepalive (effective ≥ 15000).
- `autoReconnect(ref self, maxRetries: i32, backoffMs: i64) -> WsClient` — transparent reconnect policy.
- `connect(ref self) -> WebSocket` — calls `wsConnectEx(url, headers, subprotocol, keepAliveMs)`; returns a `WebSocket` seeded with all stored params and the resulting handle (`0` on failure). Check `isOpen()` to detect failure. (Non-optional so `Drop` fires on the named result — see feasibility notes.)

---

## `WebSocket` — the connection

```liva
pub struct WebSocket {
    var handle: i64
    var url: String
    var headers: String
    var subprotocol: String
    var keepAliveMs: i64
    var maxRetries: i32
    var backoffMs: i64
}
```

- `WebSocket.connect(url: String) -> WebSocket` — convenience for `WsClient.to(url).connect()` (no options); `handle = 0` on failure (`isOpen()` false).
- `isOpen(ref self) -> bool` — `wsIsOpen(self.handle)` (false when `handle == 0`); the failure check after `connect`.
- `send(mut self, text: String) -> bool` — send a UTF-8 text frame; on failure, transparent reconnect per policy, then retry.
- `sendBinary(mut self, data: [u8]) -> bool` — send a binary frame (modeled on `sqliteBindBlob`).
- `sendJson(mut self, json: JsonValue) -> bool` — serialize via the JsonValue's `toString()` then `send` as text.
- `recv(mut self) -> WsMessage?` — receive the next message into the socket's internal buffer; returns a `WsMessage` view, or `nil` when the connection closed/errored (after exhausting the reconnect policy).
- `isOpen(ref self) -> bool`.
- `reconnect(mut self) -> bool` — explicitly close the current handle and re-open with stored params; updates `self.handle`; returns success.
- `close(mut self)` — `wsClose(handle, 1000, "")`, then `self.handle = 0`.
- `closeWith(mut self, status: i32, reason: String)` — `wsClose(handle, status, reason)`, then `self.handle = 0`.
- `impl WebSocket: Drop { func drop(mut self) { if self.handle != 0 { wsClose(self.handle, 1000, "") } } }` — auto-close at scope end. Idempotent with manual `close()` because `close()` zeroes `handle` and `wsClose(0)`/the `handle != 0` guard make a second close a no-op.

### Transparent reconnect mechanics

`send*`/`recv` share this shape (shown for `recv`):

```liva
pub func recv(mut self) -> WsMessage? {
    var attempt = 0
    while true {
        let kind = wsRecv(self.handle)
        if kind != 0 {
            return WsMessage { handle: self.handle, kind: kind }
        }
        if attempt >= self.maxRetries {
            return nil
        }
        attempt = attempt + 1
        sleep(self.backoffMs)
        wsClose(self.handle, 1000, "")                                   // free the dead handle
        self.handle = 0                                                  // avoid use-after-free if reconnect fails
        let h = wsConnectEx(self.url, self.headers, self.subprotocol, self.keepAliveMs)
        if h != 0 {
            self.handle = h
        }
    }
}
```

With `maxRetries == 0` the loop runs once and returns `nil`/`false` on failure — identical to the legacy behavior. Reconnect frees the old handle first (the dead `LivaWebSocket` is otherwise leaked) and zeroes `self.handle` before opening a new one — so if `wsConnectEx` also fails, the next `wsRecv(0)` / `wsClose(0)` is a safe no-op (the natives guard `!handle`) rather than a use-after-free on the freed pointer. A reconnected stream starts fresh — messages queued on the prior connection are not recovered (inherent to reconnect; documented).

---

## `WsMessage` — received-message view

```liva
pub struct WsMessage {
    var handle: i64    // the socket handle; non-owning (no Drop)
    var kind: i32      // 1 = text, 2 = binary
}
```

- `isText(ref self) -> bool` — `self.kind == 1`.
- `isBinary(ref self) -> bool` — `self.kind == 2`.
- `text(ref self) -> String` — `wsMsgText(self.handle)` (the internal buffer as text).
- `bytes(ref self) -> [u8]` — `wsMsgBytes(self.handle)` (the internal buffer as bytes; modeled on `sqliteColumnBlob`).
- `json(ref self) -> JsonValue` — `Json.parse(wsMsgText(self.handle))`; the returned `JsonValue` owns its DOM, so **bind it to a `let`**.

`WsMessage` is a **cursor-style view**: it reads the socket's internal buffer, which the next `recv()` overwrites. Read a message before calling `recv()` again. (Same contract as DB row/column accessors.)

---

## Runtime natives

### New / changed
| Liva builtin | C symbol | Signature | Notes |
|---|---|---|---|
| `wsConnectEx` | `liva_ws_connect_ex` | `(char* url, char* headersBlob, char* subprotocol, i64 keepAliveMs) -> i64` | replaces `wsConnect`; ws/wss parse + WinHTTP upgrade; adds request headers via `WinHttpAddRequestHeaders`, subprotocol via a `Sec-WebSocket-Protocol` header; after `WinHttpWebSocketCompleteUpgrade`, if `keepAliveMs>0` sets `WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL` (clamped to ≥15000). Returns handle (0 on failure). |
| `wsRecv` | `liva_ws_recv` | `(i64 handle) -> i32` | replaces the old text-returning recv; receives the next message into `LivaWebSocket::recvBuf`, sets `recvKind`, returns `0`=closed/error, `1`=text, `2`=binary. Accumulates fragments like the current code. |
| `wsMsgText` | `liva_ws_msg_text` | `(i64 handle) -> char*` | the internal `recvBuf` as a malloc'd C string. |
| `wsMsgBytes` | `liva_ws_msg_bytes` | `(i64 handle) -> [u8]` | the internal `recvBuf` as a `[u8]` DynArray (same outbound pattern as `sqliteColumnBlob`). |
| `wsSendBinary` | `liva_ws_send_binary` | `(i64 handle, [u8] data) -> bool` | `WinHttpWebSocketSend` with `BINARY_MESSAGE` buffer type (inbound `[u8]` pattern from `sqliteBindBlob`). |

### Retained
- `wsSend` → `liva_ws_send_text` (text frame).
- `wsClose` → `liva_ws_close`.
- `wsIsOpen` → `liva_ws_is_open`.

### Removed (breaking)
- `wsConnect` → `liva_ws_connect` (superseded by `wsConnectEx`).
- The old text-only `wsRecv` semantics (`liva_ws_recv_text` returning `char*`) — replaced by `liva_ws_recv` (i32 kind) + `liva_ws_msg_text`.

### `LivaWebSocket` struct change
Add `std::string recvBuf;` and `int recvKind = 0;` to hold the last received message between `wsRecv` and the `wsMsgText`/`wsMsgBytes` accessors.

### Six-layer binding (per native)
1. `stdlib/runtime/runtime.cpp` impl; 2. `stdlib/runtime/runtime.h` decl; 3. `src/IR/IRGen.cpp` `createRuntimeDecls()` `getOrInsertFunction` (omission → `getOrPanic`); 4. `src/IR/IRGenCall.cpp` lowering; 5. `src/Sema/TypeChecker.cpp` builtin-name loop + return-type switch; 6. `src/Sema/ModuleLoader.cpp` `std::websocket` list. Plus `src/JIT/JITEngine.cpp` `REG(...)` and `src/LSP/LSPServer.cpp` builtin list + auto-import map (`WebSocket`, `WsClient`, `WsMessage` → `websocket::websocket`). The `[u8]` in/out lowering mirrors `sqliteBindBlob`/`sqliteColumnBlob` exactly.

---

## Error handling

- **Connect failure / bad URL:** `wsConnectEx` returns 0 → the returned `WebSocket` has `handle = 0`; `isOpen()` returns false (the caller's failure check). Not an `Optional`.
- **`recv` on a closed/errored connection:** `wsRecv` returns 0; with a reconnect policy `recv` retries, else returns `nil`. `isOpen()` then reflects the live state.
- **`send*` failure:** retries per policy, else returns `false`.
- **`WsMessage.json()` on non-JSON text:** `Json.parse` returns a `JsonValue` of kind `Null`.
- **keepAlive below 15000 ms:** clamped to 15000 (documented).
- No exceptions anywhere; failures are `nil`/`false`.

---

## Testing

Mirrors the net/http convention — type-check + opt-in live (no local WS echo server is available in CI):

- **type-check (StdlibModuleTest):** `WsClient.to(u).header(..).subprotocol(..).keepAlive(..).autoReconnect(..).connect()` chain; `WebSocket.connect`, `send`/`sendBinary`/`sendJson`/`recv`/`isOpen`/`reconnect`/`close`; `WsMessage.isText/isBinary/text/bytes/json`; `WebSocket: Drop` conformance compiles.
- **RuntimeExec (no network):** programs that build the client/messages and exercise pure paths compile and run (binary `[u8]` round-trip cannot be exercised without a live peer — covered by the live test).
- **Live (opt-in):** gated on env `LIVA_WS_TEST` (default skipped), modeled on `HttpLiveRoundTrip`/`PgRealRoundTrip`: connect to a public echo server, send text + receive it back, send binary + receive it back, exercise `keepAlive`.

All tests run **serially** (`ctest --test-dir build-clang --output-on-failure`, no `-j`).

---

## Migration / breaking changes

- `stdlib/websocket/websocket.liva` — rewritten (new `WsClient`/`WebSocket`/`WsMessage`, `recv()->WsMessage?`).
- All six binding layers + JIT + LSP — add the new natives, remove `wsConnect`/old `wsRecv`.
- `tests/unit/StdlibModuleTest.cpp` (+ any SemaTest/LSPTest references) — replace the old text-only websocket tests with new-API tests.
- LSP auto-import map — add `WsClient`/`WsMessage` (keep/confirm `WebSocket`).
- Docs (`docs/{en,tr}/API-REFERENCE.md`, `README.md`, plus any TUTORIAL/LANGUAGE-REFERENCE/COOKBOOK websocket sections) — rewrite for the new API.

---

## Out of scope (YAGNI)

- Non-Windows (POSIX) WebSocket backend.
- Manual WS ping/pong frames (not exposed by WinHTTP; keepalive interval is the supported mechanism).
- per-message-deflate compression extension.
- Built-in inbound message queue / async event callbacks.
- Concurrent `recv` from multiple threads on one socket.
- TLS certificate pinning / custom cert validation.
- Hoisting `Drop` into a shared core module (reuse json's `Drop` for now).
