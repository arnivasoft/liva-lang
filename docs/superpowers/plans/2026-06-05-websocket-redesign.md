# WebSocket Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the text-only `websocket::websocket` wrapper with a full WebSocket client: binary frames, a fluent connect builder (headers/subprotocol/keepAlive/autoReconnect), JSON integration, `Drop` auto-close, WinHTTP auto-keepalive, and transparent auto-reconnect.

**Architecture:** Windows-backed over WinHTTP. New natives carry a CRLF header blob + subprotocol + keepalive into the upgrade, expose received messages via a kind code + text/bytes accessors over an internal buffer, and add a binary-send path. The `.liva` wrapper is three units (`WsClient` builder, `WebSocket` connection with `Drop` + reconnect, `WsMessage` view) reusing json's `Drop` protocol and the `[u8]` bridge proven by sqlite blobs.

**Tech Stack:** C++20, LLVM 21, WinHTTP WebSocket API, GoogleTest. Build: `.\build_clang.bat` (Ninja/Clang → `build-clang/`). Test: `ctest --test-dir build-clang --output-on-failure` (SERIAL, no `-j`).

**Spec:** `docs/superpowers/specs/2026-06-05-websocket-redesign-design.md`

---

## Conventions for every task

- **Rebuild before testing.** After editing ANY `.cpp`/`.h` OR any `.liva` (the build copies stdlib into `build-clang/`), run `.\build_clang.bat` before `ctest`.
- **Run tests serially.** `ctest --test-dir build-clang --output-on-failure`, no `-j`. `SelfHostTest.StrSplitWithAnnotationReturnsParts` is a known pre-existing flake even serially — a single failure is not a regression; re-run to confirm.
- **Six-layer native binding** (per `liva_*` native exposed to Liva): (1) `stdlib/runtime/runtime.cpp` impl; (2) `stdlib/runtime/runtime.h` decl; (3) `src/IR/IRGen.cpp` `createRuntimeDecls()` `getOrInsertFunction` — omission → `getOrPanic` crash; (4) `src/IR/IRGenCall.cpp` lowering; (5) `src/Sema/TypeChecker.cpp` builtin-name loop (~line 74) **and** return-type switch (~line 2393); (6) `src/Sema/ModuleLoader.cpp` `std::websocket` list (~line 74). JIT REG and LSP completion are OPTIONAL for ws (the existing ws natives are not JIT-registered and ws is not used in the REPL) — skip unless noted.
- **Liva language constraints (verified in prior work):** builders take `ref self` (NOT `self`-by-value, which miscompiles) and return a fresh struct; `mut self` returning a value works (e.g. `recv(mut self) -> WsMessage?`); `String` has no `==`/`!=` (use `len(x) > 0`); int literals are i32 but widen to i64 in mixed binary ops; `handle == 0` and `kind != 0` work; one statement per line; `toString(intVal)` converts to String; `sleep(ms)` is a builtin. The `Drop` protocol is `pub` in `json::json` — `import json::json` brings it into scope; do NOT declare a second `protocol Drop` (that is a "redefinition of 'Drop'" error).
- **`[u8]` bridge** (modeled exactly on `sqliteBindBlob`/`sqliteColumnBlob`): inbound, load the DynArray (`getDynArrayStructTy()`; field 0 = data `ptr`, field 1 = `len` i64) and pass `{data, len}`. Outbound, native takes an `i64* out_len` out-param and returns `i8*`; build a DynArray `{data, len, len}` (fields 0,1,2) and return it.

---

## Task 1: Runtime natives (additive) + internal receive buffer

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (the WebSocket section, ~lines 2447-2640)
- Modify: `stdlib/runtime/runtime.h` (ws decls)

Add the new natives WITHOUT removing the old `liva_ws_connect` / `liva_ws_recv_text` (those are removed in Task 4 after the wrapper migrates).

- [ ] **Step 1: Extend `LivaWebSocket` with a receive buffer.**

In `stdlib/runtime/runtime.cpp`, change the struct (~line 2447) to:

```cpp
struct LivaWebSocket {
#ifdef _WIN32
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hWebSocket;
#endif
    int open;
    std::string recvBuf;   // last received message payload
    int recvKind;          // 1 = text, 2 = binary, 0 = none
};
```

- [ ] **Step 2: Add `liva_ws_connect_ex` (headers + subprotocol + keepalive).**

Insert after `liva_ws_connect` (after ~line 2555). It mirrors `liva_ws_connect` but adds headers/subprotocol before send and keepalive after upgrade:

```cpp
int64_t liva_ws_connect_ex(const char *url, const char *headers_blob,
                           const char *subprotocol, int64_t keep_alive_ms) {
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

    // Build a combined header block: caller headers + optional subprotocol.
    std::string hdrs = headers_blob ? headers_blob : "";
    if (subprotocol && *subprotocol) {
        hdrs += "Sec-WebSocket-Protocol: ";
        hdrs += subprotocol;
        hdrs += "\r\n";
    }
    std::wstring whdrs(hdrs.begin(), hdrs.end());
    if (!whdrs.empty()) {
        WinHttpAddRequestHeaders(hRequest, whdrs.c_str(), (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
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
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
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

    if (keep_alive_ms > 0) {
        DWORD interval = (DWORD)(keep_alive_ms < 15000 ? 15000 : keep_alive_ms);
        WinHttpSetOption(hWs, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL,
                         &interval, sizeof(interval));
    }

    LivaWebSocket *ws = new LivaWebSocket();
    ws->hSession = hSession;
    ws->hConnect = hConnect;
    ws->hWebSocket = hWs;
    ws->open = 1;
    ws->recvKind = 0;
    return (int64_t)(uintptr_t)ws;
#else
    (void)url; (void)headers_blob; (void)subprotocol; (void)keep_alive_ms;
    return 0;
#endif
}
```

> Note: if `WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL` is undefined at compile time (older SDK), guard that single `WinHttpSetOption` block with `#ifdef WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL` and leave a comment; the rest of the function is unaffected.

- [ ] **Step 3: Add `liva_ws_recv` (kind into internal buffer), `liva_ws_msg_text`, `liva_ws_msg_bytes`, `liva_ws_send_binary`.**

Insert after `liva_ws_recv_text` (after ~line 2611):

```cpp
// Receive the next message into ws->recvBuf; return kind: 0 closed/error, 1 text, 2 binary.
int32_t liva_ws_recv(int64_t handle) {
    if (!handle) return 0;
#ifdef _WIN32
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    if (!ws->open) return 0;
    ws->recvBuf.clear();
    ws->recvKind = 0;
    char buf[4096];
    for (;;) {
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType;
        DWORD err = WinHttpWebSocketReceive(ws->hWebSocket, buf, sizeof(buf),
                                            &bytesRead, &bufType);
        if (err != NO_ERROR) { ws->open = 0; return 0; }
        if (bufType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) { ws->open = 0; return 0; }
        ws->recvBuf.append(buf, bytesRead);
        if (bufType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) { ws->recvKind = 1; return 1; }
        if (bufType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) { ws->recvKind = 2; return 2; }
        // *_FRAGMENT_BUFFER_TYPE: keep accumulating.
    }
#else
    (void)handle;
    return 0;
#endif
}

// The last received message as a malloc'd C string.
char *liva_ws_msg_text(int64_t handle) {
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    const std::string &s = ws ? ws->recvBuf : std::string();
    char *out = (char *)malloc(s.size() + 1);
    if (out) { memcpy(out, s.data(), s.size()); out[s.size()] = '\0'; }
    return out;
}

// The last received message as a malloc'd byte buffer; writes length via out_len.
char *liva_ws_msg_bytes(int64_t handle, int64_t *out_len) {
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    size_t n = ws ? ws->recvBuf.size() : 0;
    char *out = (char *)malloc(n > 0 ? n : 1);
    if (out && n > 0) memcpy(out, ws->recvBuf.data(), n);
    if (out_len) *out_len = (int64_t)n;
    return out;
}

// Send a binary frame. Returns 0 on success, -1 on failure.
int32_t liva_ws_send_binary(int64_t handle, const uint8_t *data, int64_t len) {
    if (!handle) return -1;
#ifdef _WIN32
    auto *ws = (LivaWebSocket *)(uintptr_t)handle;
    if (!ws->open) return -1;
    DWORD err = WinHttpWebSocketSend(ws->hWebSocket,
                                     WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                     (PVOID)data, (DWORD)len);
    return err == NO_ERROR ? 0 : -1;
#else
    (void)handle; (void)data; (void)len;
    return -1;
#endif
}
```

> `liva_ws_msg_text`/`liva_ws_msg_bytes` use a local `std::string` fallback when `ws` is null — note the `const std::string &s = ws ? ws->recvBuf : std::string();` binds a temporary; rewrite defensively as: `std::string empty; const std::string &s = ws ? ws->recvBuf : empty;` to avoid a dangling reference. Apply that form.

- [ ] **Step 4: Declare the five new natives in `stdlib/runtime/runtime.h`.**

Find the ws decls (search `liva_ws_connect`) and add after them:

```cpp
int64_t liva_ws_connect_ex(const char *url, const char *headers_blob,
                           const char *subprotocol, int64_t keep_alive_ms);
int32_t liva_ws_recv(int64_t handle);
char *liva_ws_msg_text(int64_t handle);
char *liva_ws_msg_bytes(int64_t handle, int64_t *out_len);
int32_t liva_ws_send_binary(int64_t handle, const uint8_t *data, int64_t len);
```

- [ ] **Step 5: Build to confirm the runtime compiles.**

Run: `.\build_clang.bat`
Expected: builds clean. (No Liva binding yet — these natives are not callable from Liva until Task 2. The existing suite is unaffected.)

- [ ] **Step 6: Commit.**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h
git commit -m "runtime: add ws connect_ex/recv-kind/msg_text/msg_bytes/send_binary natives + recv buffer"
```

---

## Task 2: Bind the five new ws builtins (additive)

**Files:** `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/TypeChecker.cpp`, `src/Sema/ModuleLoader.cpp`, `tests/unit/RuntimeExecTest.cpp`

New Liva builtins (old `wsConnect`/`wsRecv`/`wsSend`/`wsClose`/`wsIsOpen` stay untouched this task):
`wsConnectEx`→`liva_ws_connect_ex`, `wsRecvKind`→`liva_ws_recv`, `wsMsgText`→`liva_ws_msg_text`, `wsMsgBytes`→`liva_ws_msg_bytes`, `wsSendBinary`→`liva_ws_send_binary`.

- [ ] **Step 1: `src/IR/IRGen.cpp` createRuntimeDecls() — after the `liva_ws_is_open` decl (~line 742) add:**

```cpp
    // wsConnectEx(url, headersBlob, subprotocol, keepAliveMs) -> i64
    auto *wsConnectExTy = llvm::FunctionType::get(i64Ty,
        {i8PtrTy, i8PtrTy, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_connect_ex", wsConnectExTy);
    // wsRecvKind(handle) -> i32
    auto *wsRecvKindTy = llvm::FunctionType::get(i32Ty, {i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_recv", wsRecvKindTy);
    // wsMsgText(handle) -> i8*
    auto *wsMsgTextTy = llvm::FunctionType::get(i8PtrTy, {i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_msg_text", wsMsgTextTy);
    // wsMsgBytes(handle, out_len*) -> i8*
    auto *wsMsgBytesTy = llvm::FunctionType::get(i8PtrTy,
        {i64Ty, llvm::PointerType::getUnqual(*context_)}, false);
    module_->getOrInsertFunction("liva_ws_msg_bytes", wsMsgBytesTy);
    // wsSendBinary(handle, data*, len) -> i32
    auto *wsSendBinTy = llvm::FunctionType::get(i32Ty,
        {i64Ty, i8PtrTy, i64Ty}, false);
    module_->getOrInsertFunction("liva_ws_send_binary", wsSendBinTy);
```

- [ ] **Step 2: `src/IR/IRGenCall.cpp` — after the `wsIsOpen` lowering block (~line 3278) add:**

```cpp
    // wsConnectEx(url, headersBlob, subprotocol, keepAliveMs) -> i64
    if (funcName == "wsConnectEx" && node->getArgs().size() >= 4) {
        auto *urlArg = visit(node->getArgs()[0].get());
        auto *hdrArg = visit(node->getArgs()[1].get());
        auto *subArg = visit(node->getArgs()[2].get());
        auto *kaArg = visit(node->getArgs()[3].get());
        if (!urlArg || !hdrArg || !subArg || !kaArg) return nullptr;
        if (kaArg->getType()->isIntegerTy(32))
            kaArg = builder_->CreateSExt(kaArg, builder_->getInt64Ty());
        auto *fn = getOrPanic("liva_ws_connect_ex");
        return builder_->CreateCall(fn, {urlArg, hdrArg, subArg, kaArg}, "ws.connectex");
    }

    // wsRecvKind(handle) -> i32
    if (funcName == "wsRecvKind" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *fn = getOrPanic("liva_ws_recv");
        return builder_->CreateCall(fn, {handleArg}, "ws.recvkind");
    }

    // wsMsgText(handle) -> string
    if (funcName == "wsMsgText" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_ws_msg_text"), {handleArg}, "ws.msgtext");
        trackStringTemp(r);
        return r;
    }

    // wsMsgBytes(handle) -> [u8]  (out_len out-param pattern, mirrors sqliteColumnBlob)
    if (funcName == "wsMsgBytes" && !node->getArgs().empty()) {
        auto *handleArg = visit(node->getArgs()[0].get());
        if (!handleArg) return nullptr;
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "ws.bytes.olen",
            builder_->getInt64Ty());
        builder_->CreateStore(builder_->getInt64(0), outLenAlloca);
        auto *fn = getOrPanic("liva_ws_msg_bytes");
        auto *dataPtr = builder_->CreateCall(fn, {handleArg, outLenAlloca}, "ws.bytes.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(), outLenAlloca, "ws.bytes.olen.v");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "ws.bytes.da", daTy);
        builder_->CreateStore(dataPtr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "ws.bytes.val");
    }

    // wsSendBinary(handle, data: [u8]) -> bool  (mirrors sqliteBindBlob inbound)
    if (funcName == "wsSendBinary" && node->getArgs().size() >= 2) {
        auto *handleArg = visit(node->getArgs()[0].get());
        auto *arr = visit(node->getArgs()[1].get());
        if (!handleArg || !arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "ws.bin.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy, builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_ws_send_binary");
        auto *rc = builder_->CreateCall(fn, {handleArg, data, len}, "ws.sendbin.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "ws.sendbin.ok");
    }
```

- [ ] **Step 3: `src/Sema/TypeChecker.cpp` — (a) builtin-name loop (~line 74) change the ws line to add the five names:**

```cpp
                        "wsConnect", "wsConnectEx", "wsSend", "wsSendBinary",
                        "wsRecv", "wsRecvKind", "wsMsgText", "wsMsgBytes",
                        "wsClose", "wsIsOpen",
```

(b) return-type switch — after the `wsIsOpen` case (~line 2403) add:

```cpp
        } else if (ident->getName() == "wsConnectEx") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "wsRecvKind") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "wsMsgText") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "wsMsgBytes") {
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            node->setResolvedType(std::make_unique<ArrayTypeRepr>(std::move(u8), -1));
        } else if (ident->getName() == "wsSendBinary") {
            node->setResolvedType(makeBoolType());
```

- [ ] **Step 4: `src/Sema/ModuleLoader.cpp` — extend the `std::websocket` list (~line 74):**

```cpp
    cache_["std::websocket"] = createBuiltinModule("std::websocket",
        {"wsConnect", "wsConnectEx", "wsSend", "wsSendBinary",
         "wsRecv", "wsRecvKind", "wsMsgText", "wsMsgBytes", "wsClose", "wsIsOpen"});
```

- [ ] **Step 5: Write a no-network binding test in `tests/unit/RuntimeExecTest.cpp`** (exercises lowering + null guards without a live peer; `wsConnectEx` on a bad URL returns 0, `wsSendBinary` on a dead handle returns false, `wsRecvKind` on 0 returns 0):

```cpp
TEST(RuntimeExecTest, WsNewBuiltinsCompileAndGuard) {
    std::string source = R"LIVA(
import std::websocket
func main() {
    let h = wsConnectEx("ws://127.0.0.1:1/none", "", "", 0 as i64)
    print(toString(h))
    let bytes: [u8] = [1 as u8, 2 as u8, 3 as u8]
    let sent = wsSendBinary(h, bytes)
    if sent { print("sent") } else { print("notsent") }
    let kind = wsRecvKind(h)
    print(toString(kind))
}
)LIVA";
    auto r = compileAndRun(source, "ws_new_builtins");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("0"), std::string::npos);   // connect failed -> handle 0
    EXPECT_NE(r.stdout_output.find("notsent"), std::string::npos);
}
```

- [ ] **Step 6: Build, run the new test, then the full suite.**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R WsNewBuiltinsCompileAndGuard --output-on-failure` → PASS.
Then `ctest --test-dir build-clang --output-on-failure` → all pass.

- [ ] **Step 7: Commit.**

```bash
git add src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp tests/unit/RuntimeExecTest.cpp
git commit -m "ws: bind wsConnectEx/wsRecvKind/wsMsgText/wsMsgBytes/wsSendBinary builtins"
```

---

## Task 3: Rewrite `stdlib/websocket/websocket.liva`

**Files:** Rewrite `stdlib/websocket/websocket.liva`; tests in `tests/unit/StdlibModuleTest.cpp` + `tests/unit/RuntimeExecTest.cpp`.

- [ ] **Step 1: Write the failing type-check tests in `tests/unit/StdlibModuleTest.cpp`.**

First READ the current websocket test block (search `websocket::websocket`, around the module-6/7 area) to match the `check(source, expectPass, "stdlib")` + `TEST_F(StdlibModuleTest, ...)` style. Replace the old websocket tests with:

```cpp
TEST_F(StdlibModuleTest, ImportWebSocketModule) {
    auto r = check(
        "import websocket::websocket\n"
        "func main() {\n"
        "    let ws = WebSocket.connect(\"ws://x\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import websocket::websocket should resolve WebSocket";
}

TEST_F(StdlibModuleTest, WsClientBuilderChain) {
    auto r = check(
        "import websocket::websocket\n"
        "func main() {\n"
        "    let ws = WsClient.to(\"wss://x/y\")\n"
        "        .header(\"Authorization\", \"Bearer t\")\n"
        "        .subprotocol(\"chat\")\n"
        "        .keepAlive(30000)\n"
        "        .autoReconnect(3, 1000)\n"
        "        .connect()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "WsClient builder chain should type-check";
}

TEST_F(StdlibModuleTest, WsSendRecvBinaryJson) {
    auto r = check(
        "import websocket::websocket\n"
        "import json::json\n"
        "func main() {\n"
        "    var c = WebSocket.connect(\"ws://x\")\n"
        "    if c.isOpen() {\n"
        "        let ok = c.send(\"hi\")\n"
        "        let data: [u8] = [1 as u8, 2 as u8]\n"
        "        let okb = c.sendBinary(data)\n"
        "        let m = c.recv()\n"
        "        if let msg = m {\n"
        "            if msg.isText() { let t = msg.text() }\n"
        "            if msg.isBinary() { let b = msg.bytes() }\n"
        "            let doc = msg.json()\n"
        "        }\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "send/sendBinary/recv/WsMessage should type-check";
}
```

> If the `if let` + `var` reassignment pattern for calling `mut self` methods on an unwrapped optional does not type-check, simplify the test to construct the structs directly (e.g. `let m = WsMessage { handle: 0 as i64, kind: 1 }`) and call the accessors — the goal is to type-check the API surface, not model real usage. Adjust as needed but keep coverage of every method.

- [ ] **Step 2: Run to verify they FAIL.**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R "Ws\|WebSocket" --output-on-failure`
Expected: FAIL (old wrapper has no `WsClient`/`WsMessage`/`sendBinary`).

- [ ] **Step 3: Replace the ENTIRE `stdlib/websocket/websocket.liva` with:**

```liva
// websocket::websocket — WebSocket client (WinHTTP-backed on Windows)
// Import with: import websocket::websocket

import std::websocket
import json::json

// =============================================================
// WsMessage — a received message (view; valid until next recv())
// =============================================================

pub struct WsMessage {
    var handle: i64
    var kind: i32      // 1 = text, 2 = binary
}

impl WsMessage {
    pub func isText(ref self) -> bool {
        return self.kind == 1
    }

    pub func isBinary(ref self) -> bool {
        return self.kind == 2
    }

    pub func text(ref self) -> String {
        return wsMsgText(self.handle)
    }

    pub func bytes(ref self) -> [u8] {
        return wsMsgBytes(self.handle)
    }

    pub func json(ref self) -> JsonValue {
        return Json.parse(wsMsgText(self.handle))
    }
}

// =============================================================
// WsClient — fluent connect builder
// =============================================================

pub struct WsClient {
    var url: String
    var headers: String
    var subprotocol: String
    var keepAliveMs: i64
    var maxRetries: i32
    var backoffMs: i64
}

impl WsClient {
    pub func to(url: String) -> WsClient {
        return WsClient { url: url, headers: "", subprotocol: "",
                          keepAliveMs: 0, maxRetries: 0, backoffMs: 1000 }
    }

    pub func header(ref self, name: String, value: String) -> WsClient {
        let h = "" + self.headers + name + ": " + value + "\r\n"
        return WsClient { url: self.url, headers: h, subprotocol: self.subprotocol,
                          keepAliveMs: self.keepAliveMs, maxRetries: self.maxRetries,
                          backoffMs: self.backoffMs }
    }

    pub func subprotocol(ref self, proto: String) -> WsClient {
        return WsClient { url: self.url, headers: self.headers, subprotocol: proto,
                          keepAliveMs: self.keepAliveMs, maxRetries: self.maxRetries,
                          backoffMs: self.backoffMs }
    }

    pub func keepAlive(ref self, ms: i64) -> WsClient {
        return WsClient { url: self.url, headers: self.headers, subprotocol: self.subprotocol,
                          keepAliveMs: ms, maxRetries: self.maxRetries, backoffMs: self.backoffMs }
    }

    pub func autoReconnect(ref self, maxRetries: i32, backoffMs: i64) -> WsClient {
        return WsClient { url: self.url, headers: self.headers, subprotocol: self.subprotocol,
                          keepAliveMs: self.keepAliveMs, maxRetries: maxRetries, backoffMs: backoffMs }
    }

    pub func connect(ref self) -> WebSocket {
        // Non-optional so Drop fires on the named result (Optional+if-let does not drop).
        // Failure -> handle 0; caller checks isOpen().
        let h = wsConnectEx(self.url, self.headers, self.subprotocol, self.keepAliveMs)
        return WebSocket { handle: h, url: self.url, headers: self.headers,
                           subprotocol: self.subprotocol, keepAliveMs: self.keepAliveMs,
                           maxRetries: self.maxRetries, backoffMs: self.backoffMs }
    }
}

// =============================================================
// WebSocket — the connection (Drop auto-closes; transparent reconnect)
// =============================================================

pub struct WebSocket {
    var handle: i64
    var url: String
    var headers: String
    var subprotocol: String
    var keepAliveMs: i64
    var maxRetries: i32
    var backoffMs: i64
}

impl WebSocket {
    pub func connect(url: String) -> WebSocket {
        return WsClient.to(url).connect()
    }

    pub func send(mut self, text: String) -> bool {
        var attempt = 0
        while true {
            if wsSend(self.handle, text) {
                return true
            }
            if attempt >= self.maxRetries {
                return false
            }
            attempt = attempt + 1
            sleep(self.backoffMs)
            wsClose(self.handle, 1000, "")
            self.handle = 0
            let h = wsConnectEx(self.url, self.headers, self.subprotocol, self.keepAliveMs)
            if h != 0 {
                self.handle = h
            }
        }
        return false
    }

    pub func sendBinary(mut self, data: [u8]) -> bool {
        var attempt = 0
        while true {
            if wsSendBinary(self.handle, data) {
                return true
            }
            if attempt >= self.maxRetries {
                return false
            }
            attempt = attempt + 1
            sleep(self.backoffMs)
            wsClose(self.handle, 1000, "")
            self.handle = 0
            let h = wsConnectEx(self.url, self.headers, self.subprotocol, self.keepAliveMs)
            if h != 0 {
                self.handle = h
            }
        }
        return false
    }

    pub func sendJson(mut self, json: JsonValue) -> bool {
        return self.send(json.toString())
    }

    pub func recv(mut self) -> WsMessage? {
        var attempt = 0
        while true {
            let kind = wsRecvKind(self.handle)
            if kind != 0 {
                return WsMessage { handle: self.handle, kind: kind }
            }
            if attempt >= self.maxRetries {
                return nil
            }
            attempt = attempt + 1
            sleep(self.backoffMs)
            wsClose(self.handle, 1000, "")
            self.handle = 0
            let h = wsConnectEx(self.url, self.headers, self.subprotocol, self.keepAliveMs)
            if h != 0 {
                self.handle = h
            }
        }
        return nil
    }

    pub func isOpen(ref self) -> bool {
        return wsIsOpen(self.handle)
    }

    pub func reconnect(mut self) -> bool {
        wsClose(self.handle, 1000, "")
        self.handle = 0
        let h = wsConnectEx(self.url, self.headers, self.subprotocol, self.keepAliveMs)
        if h != 0 {
            self.handle = h
            return true
        }
        return false
    }

    pub func close(mut self) {
        wsClose(self.handle, 1000, "")
        self.handle = 0
    }

    pub func closeWith(mut self, status: i32, reason: String) {
        wsClose(self.handle, status, reason)
        self.handle = 0
    }
}

impl WebSocket: Drop {
    func drop(mut self) {
        if self.handle != 0 {
            wsClose(self.handle, 1000, "")
            self.handle = 0
        }
    }
}
```

> `json.toString()` — `JsonValue` exposes `toString()` (from the json::json DOM). If the method is actually named `toString` confirm by reading `stdlib/json/json.liva`; use that exact name.

- [ ] **Step 4: Add a no-network RuntimeExec smoke test to `tests/unit/RuntimeExecTest.cpp`** (connect to an unreachable URL → nil; ensures the wrapper + Drop compile and run end-to-end):

```cpp
TEST(RuntimeExecTest, WsConnectFailsGracefully) {
    std::string source = R"LIVA(
import websocket::websocket
func main() {
    let ws = WebSocket.connect("ws://127.0.0.1:1/none")
    if ws.isOpen() {
        print("connected")
    } else {
        print("notopen")
    }
}
)LIVA";
    auto r = compileAndRun(source, "ws_connect_fail");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("notopen"), std::string::npos);
}
```

- [ ] **Step 5: Build, run the websocket tests, then the full suite.**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang -R "Ws\|WebSocket" --output-on-failure` → PASS.
Then `ctest --test-dir build-clang --output-on-failure` → all pass.

- [ ] **Step 6: Commit.**

```bash
git add stdlib/websocket/websocket.liva tests/unit/StdlibModuleTest.cpp tests/unit/RuntimeExecTest.cpp
git commit -m "websocket: WsClient builder + WsMessage + binary/json/Drop/reconnect (new API)"
```

---

## Task 4: Remove the legacy ws builtins

**Files:** `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/TypeChecker.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/runtime/runtime.cpp`, `stdlib/runtime/runtime.h`, and any test referencing the old builtins.

Remove builtins `wsConnect` (old, url-only) and `wsRecv` (old text→String?), and their natives `liva_ws_connect` and `liva_ws_recv_text`. KEEP `wsSend`/`wsClose`/`wsIsOpen` and all the Task-1 natives.

- [ ] **Step 1: Confirm no remaining references.**

Run: `grep -rn "wsConnect\b\|wsRecv\b" stdlib/ tests/ --include=*.liva` — there must be ZERO `.liva` hits (Task 3 migrated the wrapper to `wsConnectEx`/`wsRecvKind`). If any remain, STOP and report.

- [ ] **Step 2: Remove from binding layers.**
- `src/Sema/TypeChecker.cpp`: in the builtin-name loop remove the bare `"wsConnect"` and `"wsRecv"` tokens (KEEP `wsConnectEx`, `wsRecvKind`). In the return-type switch remove the `wsConnect` branch (~line 2393) and the `wsRecv` branch (~line 2397).
- `src/Sema/ModuleLoader.cpp`: drop `"wsConnect"` and `"wsRecv"` from the `std::websocket` list (final list: `{"wsConnectEx", "wsSend", "wsSendBinary", "wsRecvKind", "wsMsgText", "wsMsgBytes", "wsClose", "wsIsOpen"}`).
- `src/IR/IRGen.cpp`: delete the `liva_ws_connect` and `liva_ws_recv_text` `getOrInsertFunction` decls (~lines 724-734; keep wsSend/wsClose/wsIsOpen and the Task-1 decls).
- `src/IR/IRGenCall.cpp`: delete the `funcName == "wsConnect"` block (~3222-3228) and the `funcName == "wsRecv"` block (~3240-3256). Keep wsSend/wsClose/wsIsOpen and the Task-2 blocks.

- [ ] **Step 3: Remove the unused natives.**
- `stdlib/runtime/runtime.cpp`: delete `liva_ws_connect` (~2483-2555) and `liva_ws_recv_text` (~2574-2611). Keep `parse_ws_url`, `liva_ws_connect_ex`, `liva_ws_recv`, `liva_ws_msg_text`, `liva_ws_msg_bytes`, `liva_ws_send_binary`, `liva_ws_send_text`, `liva_ws_close`, `liva_ws_is_open`.
- `stdlib/runtime/runtime.h`: delete the `liva_ws_connect` and `liva_ws_recv_text` decls.

- [ ] **Step 4: Fix any SemaTest/LSPTest references.**

Run: `grep -rn "wsConnect\b\|wsRecv\b\|liva_ws_connect\b\|liva_ws_recv_text" tests/unit/ src/` — update or remove any remaining reference to the removed symbols (e.g. a Sema/LSP test asserting the old builtin). The only surviving `wsConnect`/`wsRecv` substrings should be `wsConnectEx`/`wsRecvKind`.

- [ ] **Step 5: Build and run the full suite.**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang --output-on-failure` → all pass. A `getOrPanic`/"unknown function" crash means a removed symbol is still referenced — grep and fix.

- [ ] **Step 6: Commit.**

```bash
git add -A
git commit -m "ws: remove legacy wsConnect/wsRecv builtins + natives"
```

---

## Task 5: LSP auto-import + docs + live opt-in test

**Files:** `src/LSP/LSPServer.cpp`, `docs/{en,tr}/API-REFERENCE.md`, `docs/{en,tr}/README.md`, plus any TUTORIAL/LANGUAGE-REFERENCE/COOKBOOK websocket section; `tests/unit/RuntimeExecTest.cpp`.

- [ ] **Step 1: LSP auto-import map — `src/LSP/LSPServer.cpp`.**

Find the `autoImports` array (search `{"HttpRequest", "http::http"}`, ~line 2715). Add:

```cpp
                    // websocket::websocket wrapper types
                    {"WebSocket", "websocket::websocket"},
                    {"WsClient", "websocket::websocket"},
                    {"WsMessage", "websocket::websocket"},
```

- [ ] **Step 2: Rewrite the WebSocket section in API-REFERENCE (en + tr).**

Grep each doc for the old websocket API (`WebSocket`, `wsConnect`, `.recv()` returning String). Replace with the new API. Example block to include (translate prose to Turkish in the tr file; code identical):

````markdown
```liva
import websocket::websocket
import json::json

var c = WsClient.to("wss://example.com/socket")
    .header("Authorization", "Bearer TOKEN")
    .subprotocol("chat")
    .keepAlive(30000)        // WinHTTP auto-keepalive (min 15000 ms)
    .autoReconnect(3, 1000)  // 3 retries, 1s backoff
    .connect()               // -> WebSocket (non-optional); check isOpen()

if c.isOpen() {
    c.send("hello")
    let bin: [u8] = [1 as u8, 2 as u8, 3 as u8]
    c.sendBinary(bin)
    if let m = c.recv() {     // WsMessage?; the view is valid until the next recv()
        if m.isText() {
            let doc = m.json()   // JsonValue — bind it (owns its DOM)
        }
    }
    // closes automatically at scope end (Drop); or c.close()
}
```
````

Document: `connect()` returns a **non-optional `WebSocket`** — check `isOpen()` for failure (it is non-optional so `Drop` auto-close works; do not copy the socket value); `recv()` returns `WsMessage?` with `isText/isBinary/text/bytes/json`; `WsMessage` is a view valid until the next `recv()`; `Drop` auto-closes at scope end; keepAlive min 15000 ms; manual WS ping is not available (keepAlive is the mechanism); Windows-backed (non-Windows is a stub).

- [ ] **Step 3: Refresh README + any TUTORIAL/LANGUAGE-REFERENCE/COOKBOOK websocket mention (en + tr).**

Grep: `grep -rn "wsConnect\|WebSocket\|websocket::websocket\|\.recv()" docs/` and update every occurrence to the new API. Ensure full Turkish orthography (ç,ğ,ı,ö,ş,ü) in tr/ prose.

- [ ] **Step 4: Add the live opt-in test to `tests/unit/RuntimeExecTest.cpp`** (gated like `HttpLiveRoundTrip`; URL overridable via env, defaulting to a public echo server):

```cpp
TEST(RuntimeExecTest, WsLiveEchoRoundTrip) {
    if (std::getenv("LIVA_WS_TEST") == nullptr) {
        GTEST_SKIP() << "Set LIVA_WS_TEST=1 to run the live WebSocket echo test";
    }
    const char *envUrl = std::getenv("LIVA_WS_TEST_URL");
    std::string url = envUrl ? envUrl : "wss://echo.websocket.events";
    std::string source = std::string(R"LIVA(
import websocket::websocket
func main() {
    var c = WebSocket.connect(")LIVA") + url + R"LIVA(")
    if c.isOpen() {
        if c.send("ping") {
            if let m = c.recv() {
                if len(m.text()) > 0 {
                    print("OK")
                }
            }
        }
    } else {
        print("noconnect")
    }
}
)LIVA";
    auto r = compileAndRun(source, "ws_live");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("OK"), std::string::npos);
}
```

> Some echo servers send a greeting frame first; if the chosen default server does so, the first `recv()` returns the greeting, not the echo — that still satisfies `len(text())>0`. Document `LIVA_WS_TEST_URL` for choosing a strict echo server.

- [ ] **Step 5: Build and run the full suite (live test skipped by default).**

Run: `.\build_clang.bat` then `ctest --test-dir build-clang --output-on-failure` → all pass; `WsLiveEchoRoundTrip` shows SKIPPED.

- [ ] **Step 6: Commit.**

```bash
git add src/LSP/LSPServer.cpp docs/ tests/unit/RuntimeExecTest.cpp
git commit -m "ws: LSP auto-import + docs rewrite + opt-in live echo test"
```

---

## Final review

After all tasks: dispatch a final whole-branch reviewer (per subagent-driven-development), then use `superpowers:finishing-a-development-branch`. Verify the full serial suite is green (live WS test skipped) and re-run once if `SelfHostTest.StrSplit*` flakes.
