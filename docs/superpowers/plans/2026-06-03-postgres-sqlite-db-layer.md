# PostgreSQL + Unified DB Layer + SQLite Extras Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fail-closed PostgreSQL client (`postgres::postgres`), a driver-agnostic `db::db` layer with `?`→`$n` parameter normalization, and four missing SQLite features (transactions, BLOB, NULL/type introspection, named params).

**Architecture:** Every native-backed feature follows the existing SQLite pattern across five layers: stdlib `.liva` wrapper → `liva_*` runtime functions (runtime.cpp/.h) → IRGen intrinsic lowering (IRGenCall.cpp) → TypeChecker (builtin name list + return-type switch) → ModuleLoader (`createBuiltinModule`). libpq is resolved at runtime via LoadLibrary/dlopen probing standard install paths (newest-first); absent → fail-closed. The `db::db` layer is pure Liva over a `dyn Database` protocol; `?`→`$n` rewriting is done by a libpq-independent native helper for robustness.

**Tech Stack:** C++20, LLVM 21 IRBuilder, winsqlite3.dll (Windows) / libsqlite3 (POSIX), libpq (dynamic), GoogleTest. Build: `build_clang.bat` → `build-clang/`. Test: `ctest --test-dir build-clang --output-on-failure`.

---

## Reference: established patterns (read before starting)

- **Runtime fail-closed dispatch table:** `stdlib/runtime/runtime.cpp` `SqliteApi` struct + `sqlite_api()` (lines ~2763–2845). Every `liva_sqlite_*` function guards on `#ifdef LIVA_HAS_SQLITE` and a null function pointer, returning 0/nullptr/false otherwise.
- **IRGen intrinsic lowering:** `src/IR/IRGenCall.cpp` — each builtin is an `if (funcName == "..." && node->getArgs().size() >= N)` block using `visit()` on args, `getOrPanic("liva_...")`, `builder_->CreateCall(...)`. String returns call `trackStringTemp(result)`. Optionals built via `getOptionalType(elemTy)` + two `CreateStructGEP` stores (field 0 = has-value i1, field 1 = value). SQLite block at lines ~3255–3460.
- **`[u8]` ↔ runtime bridge:** `src/IR/IRGenCall.cpp` `bytesToStr` (~4787, passing a DynArray's data+len into a native) and `gzipDecode` (~4875, building a `[u8]` DynArray from a returned `(ptr,len)`). DynArray struct type via `getDynArrayStructTy()`: field 0 = data ptr, field 1 = length (i64), field 2 = capacity (i64).
- **TypeChecker two edit points:** (1) builtin-name declaration loop at `src/Sema/TypeChecker.cpp` ~line 77 (SQLite names) and ~162 (encoding names) — every new builtin name MUST be added to a declaration list or it won't resolve as a function; (2) return-type switch at ~line 2320 (SQLite) / ~2475 (encoding `[u8]`). `[u8]` is `std::make_unique<ArrayTypeRepr>(makePrimitiveType(TypeRepr::Kind::U8), -1)`.
- **ModuleLoader:** `src/Sema/ModuleLoader.cpp` ~line 76 — `cache_["std::sqlite"] = createBuiltinModule("std::sqlite", { ...names... });`.
- **Type-check tests:** `tests/unit/StdlibModuleTest.cpp` — `check(source, true, "stdlib")` parses + runs Sema with the stdlib search path; assert `r.passed`.
- **Behavioral tests:** `tests/unit/RuntimeExecTest.cpp` — `compileAndRun(source, "test_name")` compiles to a native exe, runs it, returns `{exit_code, stdout_output}`. SQLite runs for real here (winsqlite3.dll on Windows). Guarded by `#ifdef LIVA_HAS_LLVM`.
- **Protocol/dyn syntax:** `examples/dyn_protocol_demo.liva` — `protocol P { func m(ref self) -> T }`, `impl S: P { ... }`, `dyn P`, `[dyn P]`.
- **String/array primitives available in Liva:** `s.length` (i64, property), `s.substring(start, len)`, `s.indexOf(x)`, `s.split(x) -> [string]`, `s.contains/startsWith/endsWith`, `arr.length` (i64), `arr[i]`, `for x in arr`, `for i in 0..n`. (No `charAt`/`length()` method — use the `.length` property and `.substring`.)

---

## Phase A — SQLite extensions

winsqlite3.dll ships with Windows 10+, so these run for real in `RuntimeExecTest`. Each task adds a runtime function, an `sqlite_api()` symbol, an IRGen block, the two TypeChecker edits, and a wrapper method.

### Task A1: `sqliteColumnName` (needed by the SqliteDatabase adapter in Phase C)

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (SqliteApi struct + `sqlite_api()` + new function), `stdlib/runtime/runtime.h`
- Modify: `src/IR/IRGenCall.cpp` (new lowering block after `sqliteColumnCount`)
- Modify: `src/Sema/TypeChecker.cpp` (name list ~77 + return-type switch ~2350)
- Modify: `src/Sema/ModuleLoader.cpp` (name list ~76)
- Modify: `stdlib/sqlite/sqlite.liva` (Stmt method)
- Test: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Write the failing behavioral test**

Add to `tests/unit/RuntimeExecTest.cpp` (inside the `#ifdef LIVA_HAS_LLVM` block, near other tests):

```cpp
TEST(RuntimeExecTest, SqliteColumnName) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(alpha INTEGER, beta TEXT)\")\n"
        "        if let s = d.prepare(\"SELECT alpha, beta FROM t\") {\n"
        "            var stmt = s\n"
        "            println(stmt.columnName(0))\n"
        "            println(stmt.columnName(1))\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_column_name");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "alpha\nbeta\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.SqliteColumnName --output-on-failure`
Expected: FAIL — `columnName` is not a known method / `sqliteColumnName` does not resolve (compile failure marker in output).

- [ ] **Step 3: Add the runtime function pointer + resolution**

In `stdlib/runtime/runtime.cpp`, inside `struct SqliteApi` (after `column_double`):

```cpp
    const char *(*column_name)(void *, int) = nullptr;
```

In `sqlite_api()`, in the `#ifdef _WIN32` branch (after the `column_double` resolve line):

```cpp
        api.column_name = (decltype(api.column_name))resolve("sqlite3_column_name");
```

In the `#elif defined(LIVA_HAS_POSIX_SQLITE)` branch (after the `column_double` line):

```cpp
        api.column_name = reinterpret_cast<decltype(api.column_name)>(&sqlite3_column_name);
```

- [ ] **Step 4: Add the `liva_sqlite_column_name` runtime function**

In `stdlib/runtime/runtime.cpp`, after `liva_sqlite_column_double` (~line 3193):

```cpp
char *liva_sqlite_column_name(int64_t stmt, int32_t col) {
    if (!stmt) return nullptr;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.column_name) return nullptr;
    const char *n = api.column_name((void *)(uintptr_t)stmt, col);
    return strdup_safe(n ? n : "");
#else
    (void)stmt;
    (void)col;
    return nullptr;
#endif
}
```

In `stdlib/runtime/runtime.h`, after the `liva_sqlite_column_double` declaration (~line 542):

```cpp
/// Name of result column `col` (0-based). Caller frees.
char *liva_sqlite_column_name(int64_t stmt, int32_t col);
```

- [ ] **Step 5: Add the IRGen lowering**

In `src/IR/IRGenCall.cpp`, immediately after the `sqliteColumnCount` block (~line 3449):

```cpp
    // sqliteColumnName(stmt, col) -> string
    if (funcName == "sqliteColumnName" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_name");
        auto *r = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.colname");
        trackStringTemp(r);
        return r;
    }
```

- [ ] **Step 6: Register the builtin in TypeChecker and ModuleLoader**

In `src/Sema/TypeChecker.cpp`, add `"sqliteColumnName"` to the SQLite name list (~line 83, alongside `"sqliteColumnText"`):

```cpp
                        "sqliteColumnText", "sqliteColumnInt", "sqliteColumnDouble",
                        "sqliteColumnName",
                        "sqliteFinalize"}) {
```

In the return-type switch, after the `sqliteColumnDouble` case (~line 2357):

```cpp
        } else if (ident->getName() == "sqliteColumnName") {
            node->setResolvedType(makeStringType());
```

In `src/Sema/ModuleLoader.cpp`, add `"sqliteColumnName"` to the `std::sqlite` list (~line 83):

```cpp
         "sqliteColumnText", "sqliteColumnInt", "sqliteColumnDouble",
         "sqliteColumnName",
         "sqliteFinalize"});
```

- [ ] **Step 7: Add the wrapper method**

In `stdlib/sqlite/sqlite.liva`, inside `impl Stmt`, after `columnDouble`:

```liva
    pub func columnName(ref self, col: i32) -> String {
        return sqliteColumnName(self.handle, col)
    }
```

- [ ] **Step 8: Rebuild and run the test**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.SqliteColumnName --output-on-failure`
Expected: PASS — stdout `alpha\nbeta\n`, exit 0.

- [ ] **Step 9: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp stdlib/sqlite/sqlite.liva tests/unit/RuntimeExecTest.cpp
git commit -m "sqlite: add columnName accessor"
```

### Task A2: `sqliteColumnType` + `columnIsNull`

SQLite column type codes: 1=INTEGER, 2=FLOAT, 3=TEXT, 4=BLOB, 5=NULL.

**Files:**
- Modify: `stdlib/runtime/runtime.cpp`, `stdlib/runtime/runtime.h`
- Modify: `src/IR/IRGenCall.cpp`
- Modify: `src/Sema/TypeChecker.cpp` (name list + switch)
- Modify: `src/Sema/ModuleLoader.cpp`
- Modify: `stdlib/sqlite/sqlite.liva`
- Test: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST(RuntimeExecTest, SqliteColumnTypeAndNull) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(a INTEGER, b TEXT)\")\n"
        "        d.exec(\"INSERT INTO t(a, b) VALUES (7, NULL)\")\n"
        "        if let s = d.prepare(\"SELECT a, b FROM t\") {\n"
        "            var stmt = s\n"
        "            if stmt.step() {\n"
        "                println(stmt.columnType(0))\n"
        "                if stmt.columnIsNull(1) { println(\"null\") } else { println(\"notnull\") }\n"
        "            }\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_column_type");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "1\nnull\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.SqliteColumnTypeAndNull --output-on-failure`
Expected: FAIL (unknown methods).

- [ ] **Step 3: Add the runtime function pointer + resolution**

In `struct SqliteApi` (after `column_name`):

```cpp
    int (*column_type)(void *, int) = nullptr;
```

In `sqlite_api()` `#ifdef _WIN32` branch:

```cpp
        api.column_type = (decltype(api.column_type))resolve("sqlite3_column_type");
```

In the POSIX branch:

```cpp
        api.column_type = reinterpret_cast<decltype(api.column_type)>(&sqlite3_column_type);
```

- [ ] **Step 4: Add the runtime function**

In `runtime.cpp`, after `liva_sqlite_column_name`:

```cpp
// SQLite column type code: 1=INT, 2=FLOAT, 3=TEXT, 4=BLOB, 5=NULL. Returns 0
// if unavailable (treated as "unknown", never NULL).
int32_t liva_sqlite_column_type(int64_t stmt, int32_t col) {
    if (!stmt) return 0;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.column_type) return 0;
    return (int32_t)api.column_type((void *)(uintptr_t)stmt, col);
#else
    (void)stmt;
    (void)col;
    return 0;
#endif
}
```

In `runtime.h`, after `liva_sqlite_column_name`:

```cpp
/// SQLite type code of column `col` in the current row: 1=INTEGER, 2=FLOAT,
/// 3=TEXT, 4=BLOB, 5=NULL. 0 if unavailable.
int32_t liva_sqlite_column_type(int64_t stmt, int32_t col);
```

- [ ] **Step 5: Add the IRGen lowering**

In `IRGenCall.cpp`, after the `sqliteColumnName` block:

```cpp
    // sqliteColumnType(stmt, col) -> i32
    if (funcName == "sqliteColumnType" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_type");
        return builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltype");
    }

    // sqliteColumnIsNull(stmt, col) -> bool  (column_type == 5)
    if (funcName == "sqliteColumnIsNull" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *fn = getOrPanic("liva_sqlite_column_type");
        auto *t = builder_->CreateCall(fn, {stmtArg, colArg}, "sqlite.coltype.n");
        return builder_->CreateICmpEQ(t, builder_->getInt32(5), "sqlite.isnull");
    }
```

- [ ] **Step 6: Register in TypeChecker + ModuleLoader**

TypeChecker name list (~83): add `"sqliteColumnType", "sqliteColumnIsNull"`.
TypeChecker switch, after the `sqliteColumnName` case:

```cpp
        } else if (ident->getName() == "sqliteColumnType") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "sqliteColumnIsNull") {
            node->setResolvedType(makeBoolType());
```

ModuleLoader `std::sqlite` list: add `"sqliteColumnType", "sqliteColumnIsNull"`.

- [ ] **Step 7: Add wrapper methods**

In `impl Stmt`, after `columnName`:

```liva
    // SQLite type code: 1=INTEGER, 2=FLOAT, 3=TEXT, 4=BLOB, 5=NULL.
    pub func columnType(ref self, col: i32) -> i32 {
        return sqliteColumnType(self.handle, col)
    }

    pub func columnIsNull(ref self, col: i32) -> bool {
        return sqliteColumnIsNull(self.handle, col)
    }
```

- [ ] **Step 8: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.SqliteColumnTypeAndNull --output-on-failure`
Expected: PASS — stdout `1\nnull\n`.

- [ ] **Step 9: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp stdlib/sqlite/sqlite.liva tests/unit/RuntimeExecTest.cpp
git commit -m "sqlite: add columnType + columnIsNull introspection"
```

### Task A3: `sqliteBindByName` (named parameters)

`sqlite3_bind_parameter_index(stmt, ":name")` returns the 1-based index (0 if not found). We resolve the index in the runtime, then bind text via the existing `bind_text`.

**Files:** same set as A2.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(RuntimeExecTest, SqliteBindByName) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(name TEXT)\")\n"
        "        if let s = d.prepare(\"INSERT INTO t(name) VALUES (:n)\") {\n"
        "            var stmt = s\n"
        "            let ok = stmt.bindByName(\":n\", \"Ada\")\n"
        "            if ok { println(\"bound\") } else { println(\"fail\") }\n"
        "            stmt.step()\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        if let v = d.queryString(\"SELECT name FROM t\") { println(v) }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_bind_by_name");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "bound\nAda\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.SqliteBindByName --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Add the runtime function pointer + resolution**

In `struct SqliteApi` (after `column_type`):

```cpp
    int (*bind_parameter_index)(void *, const char *) = nullptr;
```

In `sqlite_api()` `#ifdef _WIN32` branch:

```cpp
        api.bind_parameter_index = (decltype(api.bind_parameter_index))resolve("sqlite3_bind_parameter_index");
```

POSIX branch:

```cpp
        api.bind_parameter_index = reinterpret_cast<decltype(api.bind_parameter_index)>(&sqlite3_bind_parameter_index);
```

- [ ] **Step 4: Add the runtime function**

In `runtime.cpp`, after `liva_sqlite_column_type`:

```cpp
// Resolve a named parameter (":name", "@name", "$name") to its 1-based index,
// then bind `val` as text there. Returns 0 on success, -1 if the name is
// unknown or binding fails.
int32_t liva_sqlite_bind_by_name(int64_t stmt, const char *name, const char *val) {
    if (!stmt || !name) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.bind_parameter_index || !api.bind_text) return -1;
    int idx = api.bind_parameter_index((void *)(uintptr_t)stmt, name);
    if (idx <= 0) return -1;
    auto *transient = (void (*)(void *))(intptr_t)-1;
    int rc = api.bind_text((void *)(uintptr_t)stmt, idx,
                           val ? val : "",
                           val ? (int)strlen(val) : 0, transient);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)stmt;
    (void)name;
    (void)val;
    return -1;
#endif
}
```

In `runtime.h`, after `liva_sqlite_column_type`:

```cpp
/// Bind text `val` to the named parameter `name` (e.g. ":id"). Returns 0 on
/// success, -1 if the name is unknown or the bind fails.
int32_t liva_sqlite_bind_by_name(int64_t stmt, const char *name, const char *val);
```

- [ ] **Step 5: Add the IRGen lowering**

In `IRGenCall.cpp`, after the `sqliteColumnIsNull` block:

```cpp
    // sqliteBindByName(stmt, name, val) -> bool
    if (funcName == "sqliteBindByName" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *nameArg = visit(node->getArgs()[1].get());
        auto *valArg = visit(node->getArgs()[2].get());
        if (!stmtArg || !nameArg || !valArg) return nullptr;
        auto *fn = getOrPanic("liva_sqlite_bind_by_name");
        auto *rc = builder_->CreateCall(fn, {stmtArg, nameArg, valArg}, "sqlite.bindname.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bindname.ok");
    }
```

- [ ] **Step 6: Register in TypeChecker + ModuleLoader**

TypeChecker name list: add `"sqliteBindByName"`.
TypeChecker switch — extend the existing bool-returning bind group condition (~line 2343) by adding `|| ident->getName() == "sqliteBindByName"` so it resolves to bool:

```cpp
        } else if (ident->getName() == "sqliteBindText" ||
                   ident->getName() == "sqliteBindInt" ||
                   ident->getName() == "sqliteBindDouble" ||
                   ident->getName() == "sqliteBindNull" ||
                   ident->getName() == "sqliteBindByName" ||
                   ident->getName() == "sqliteStep" ||
                   ident->getName() == "sqliteReset") {
            node->setResolvedType(makeBoolType());
```

ModuleLoader `std::sqlite` list: add `"sqliteBindByName"`.

- [ ] **Step 7: Add the wrapper method**

In `impl Stmt`, after `bindNull`:

```liva
    // Bind text to a named parameter (":name", "@name", "$name").
    // Returns false if the name is not present in the statement.
    pub func bindByName(ref self, name: String, val: String) -> bool {
        return sqliteBindByName(self.handle, name, val)
    }
```

- [ ] **Step 8: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.SqliteBindByName --output-on-failure`
Expected: PASS — stdout `bound\nAda\n`.

- [ ] **Step 9: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp stdlib/sqlite/sqlite.liva tests/unit/RuntimeExecTest.cpp
git commit -m "sqlite: add bindByName for named parameters"
```

### Task A4: BLOB support (`sqliteBindBlob`, `sqliteColumnBlob`)

Follows the `[u8]` DynArray bridge from `bytesToStr` (passing data+len) and `gzipDecode` (building `[u8]` from returned ptr+len).

**Files:** same set as A2, plus the `[u8]` IRGen bridge.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(RuntimeExecTest, SqliteBlobRoundTrip) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    let payload: [u8] = [104, 105, 0, 1, 255]\n"  // "hi" + NUL + bytes
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(b BLOB)\")\n"
        "        if let s = d.prepare(\"INSERT INTO t(b) VALUES (?)\") {\n"
        "            var stmt = s\n"
        "            stmt.bindBlob(1, payload)\n"
        "            stmt.step()\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        if let s2 = d.prepare(\"SELECT b FROM t\") {\n"
        "            var stmt2 = s2\n"
        "            if stmt2.step() {\n"
        "                let got = stmt2.columnBlob(0)\n"
        "                println(got.length)\n"
        "                println(got[2])\n"
        "                println(got[4])\n"
        "            }\n"
        "            stmt2.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_blob");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "5\n0\n255\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.SqliteBlobRoundTrip --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Add the runtime function pointers + resolution**

In `struct SqliteApi` (after `bind_parameter_index`):

```cpp
    int (*bind_blob)(void *, int, const void *, int, void (*)(void *)) = nullptr;
    const void *(*column_blob)(void *, int) = nullptr;
    int (*column_bytes)(void *, int) = nullptr;
```

In `sqlite_api()` `#ifdef _WIN32` branch:

```cpp
        api.bind_blob = (decltype(api.bind_blob))resolve("sqlite3_bind_blob");
        api.column_blob = (decltype(api.column_blob))resolve("sqlite3_column_blob");
        api.column_bytes = (decltype(api.column_bytes))resolve("sqlite3_column_bytes");
```

POSIX branch:

```cpp
        api.bind_blob = reinterpret_cast<decltype(api.bind_blob)>(&sqlite3_bind_blob);
        api.column_blob = reinterpret_cast<decltype(api.column_blob)>(&sqlite3_column_blob);
        api.column_bytes = reinterpret_cast<decltype(api.column_bytes)>(&sqlite3_column_bytes);
```

- [ ] **Step 4: Add the runtime functions**

In `runtime.cpp`, after `liva_sqlite_bind_by_name`:

```cpp
int32_t liva_sqlite_bind_blob(int64_t stmt, int32_t idx, const void *data, int64_t len) {
    if (!stmt) return -1;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.bind_blob) return -1;
    auto *transient = (void (*)(void *))(intptr_t)-1;  // SQLITE_TRANSIENT
    int rc = api.bind_blob((void *)(uintptr_t)stmt, idx,
                           data ? data : "", (int)len, transient);
    return rc == LIVA_SQLITE_OK ? 0 : -1;
#else
    (void)stmt; (void)idx; (void)data; (void)len;
    return -1;
#endif
}

// Returns a freshly malloc'd copy of the column's blob bytes; *out_len gets the
// byte count. Returns nullptr (and *out_len = 0) if empty/unavailable. Caller
// frees.
void *liva_sqlite_column_blob(int64_t stmt, int32_t col, int64_t *out_len) {
    if (out_len) *out_len = 0;
    if (!stmt) return nullptr;
#ifdef LIVA_HAS_SQLITE
    auto &api = sqlite_api();
    if (!api.column_blob || !api.column_bytes) return nullptr;
    const void *src = api.column_blob((void *)(uintptr_t)stmt, col);
    int n = api.column_bytes((void *)(uintptr_t)stmt, col);
    if (n <= 0 || !src) return nullptr;
    void *out = malloc((size_t)n);
    if (!out) return nullptr;
    memcpy(out, src, (size_t)n);
    if (out_len) *out_len = n;
    return out;
#else
    (void)stmt; (void)col;
    return nullptr;
#endif
}
```

In `runtime.h`, after `liva_sqlite_bind_by_name`:

```cpp
/// Bind `len` raw bytes from `data` to parameter `idx` (1-based, BLOB).
/// Returns 0 on success.
int32_t liva_sqlite_bind_blob(int64_t stmt, int32_t idx, const void *data, int64_t len);

/// Read column `col` (0-based) as a blob: returns a fresh malloc'd byte buffer
/// and writes the length to *out_len. nullptr/0 if empty. Caller frees.
void *liva_sqlite_column_blob(int64_t stmt, int32_t col, int64_t *out_len);
```

- [ ] **Step 5: Add the IRGen lowering (the `[u8]` bridge)**

In `IRGenCall.cpp`, after the `sqliteBindByName` block:

```cpp
    // sqliteBindBlob(stmt, idx, data: [u8]) -> bool
    if (funcName == "sqliteBindBlob" && node->getArgs().size() >= 3) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *idxArg = visit(node->getArgs()[1].get());
        auto *arr = visit(node->getArgs()[2].get());
        if (!stmtArg || !idxArg || !arr) return nullptr;
        if (idxArg->getType()->isIntegerTy(64))
            idxArg = builder_->CreateTrunc(idxArg, builder_->getInt32Ty());
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "blob.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_sqlite_bind_blob");
        auto *rc = builder_->CreateCall(fn, {stmtArg, idxArg, data, len}, "sqlite.bindblob.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "sqlite.bindblob.ok");
    }

    // sqliteColumnBlob(stmt, col) -> [u8]
    if (funcName == "sqliteColumnBlob" && node->getArgs().size() >= 2) {
        auto *stmtArg = visit(node->getArgs()[0].get());
        auto *colArg = visit(node->getArgs()[1].get());
        if (!stmtArg || !colArg) return nullptr;
        if (colArg->getType()->isIntegerTy(64))
            colArg = builder_->CreateTrunc(colArg, builder_->getInt32Ty());
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *outLenAlloca = createEntryBlockAlloca(funcCur, "blob.olen",
            builder_->getInt64Ty());
        builder_->CreateStore(builder_->getInt64(0), outLenAlloca);
        auto *fn = getOrPanic("liva_sqlite_column_blob");
        auto *dataPtr = builder_->CreateCall(fn, {stmtArg, colArg, outLenAlloca}, "blob.data");
        auto *outLen = builder_->CreateLoad(builder_->getInt64Ty(), outLenAlloca, "blob.olen.v");
        auto *daTy = getDynArrayStructTy();
        auto *daAlloca = createEntryBlockAlloca(funcCur, "blob.da", daTy);
        builder_->CreateStore(dataPtr, builder_->CreateStructGEP(daTy, daAlloca, 0));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 1));
        builder_->CreateStore(outLen, builder_->CreateStructGEP(daTy, daAlloca, 2));
        return builder_->CreateLoad(daTy, daAlloca, "blob.val");
    }
```

- [ ] **Step 6: Register in TypeChecker + ModuleLoader**

TypeChecker name list: add `"sqliteBindBlob", "sqliteColumnBlob"`.
TypeChecker switch — add after the `sqliteColumnIsNull` case:

```cpp
        } else if (ident->getName() == "sqliteBindBlob") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "sqliteColumnBlob") {
            // [u8]
            auto u8 = makePrimitiveType(TypeRepr::Kind::U8);
            node->setResolvedType(std::make_unique<ArrayTypeRepr>(std::move(u8), -1));
```

ModuleLoader `std::sqlite` list: add `"sqliteBindBlob", "sqliteColumnBlob"`.

- [ ] **Step 7: Add the wrapper methods**

In `impl Stmt`, after `bindByName`:

```liva
    pub func bindBlob(ref self, idx: i32, data: [u8]) -> bool {
        return sqliteBindBlob(self.handle, idx, data)
    }

    pub func columnBlob(ref self, col: i32) -> [u8] {
        return sqliteColumnBlob(self.handle, col)
    }
```

- [ ] **Step 8: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.SqliteBlobRoundTrip --output-on-failure`
Expected: PASS — stdout `5\n0\n255\n`.

- [ ] **Step 9: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp stdlib/sqlite/sqlite.liva tests/unit/RuntimeExecTest.cpp
git commit -m "sqlite: add BLOB bind/read via [u8] bridge"
```

### Task A5: Transaction helpers (pure Liva — no new natives)

- [ ] **Step 1: Write the failing test**

```cpp
TEST(RuntimeExecTest, SqliteTransactionRollback) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(n INTEGER)\")\n"
        "        d.begin()\n"
        "        d.exec(\"INSERT INTO t(n) VALUES (1)\")\n"
        "        d.rollback()\n"
        "        d.begin()\n"
        "        d.exec(\"INSERT INTO t(n) VALUES (2)\")\n"
        "        d.commit()\n"
        "        if let c = d.queryInt(\"SELECT COUNT(*) FROM t\") { println(c) }\n"
        "        if let v = d.queryInt(\"SELECT n FROM t\") { println(v) }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_txn");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "1\n2\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.SqliteTransactionRollback --output-on-failure`
Expected: FAIL (`begin`/`commit`/`rollback` not methods).

- [ ] **Step 3: Add the wrapper methods (pure Liva)**

In `stdlib/sqlite/sqlite.liva`, inside `impl SqliteDB`, after `prepare`:

```liva
    // --- Transactions ---

    pub func begin(ref self) -> bool {
        return self.exec("BEGIN")
    }

    pub func commit(ref self) -> bool {
        return self.exec("COMMIT")
    }

    pub func rollback(ref self) -> bool {
        return self.exec("ROLLBACK")
    }
```

- [ ] **Step 4: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.SqliteTransactionRollback --output-on-failure`
Expected: PASS — stdout `1\n2\n`.

- [ ] **Step 5: Commit**

```bash
git add stdlib/sqlite/sqlite.liva tests/unit/RuntimeExecTest.cpp
git commit -m "sqlite: add begin/commit/rollback transaction helpers"
```

---

## Phase B — PostgreSQL module (`postgres::postgres`)

libpq is resolved at runtime. On this machine it lives at `C:\Program Files\PostgreSQL\{18,17,14}\bin\libpq.dll` (not on PATH). All entry points fail closed if libpq or a symbol is missing.

### Task B1: libpq loader + connection runtime functions

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (new PG section), `stdlib/runtime/runtime.h`
- Test: `tests/unit/RuntimeExecTest.cpp` (added in B3 once wired; this task is C++-only and verified by build)

- [ ] **Step 1: Add the PG dispatch table + loader to runtime.cpp**

Add a new section in `stdlib/runtime/runtime.cpp` after the SQLite section (after `liva_sqlite_finalize`, ~line 3203). The loader probes PATH, then standard install dirs newest-first, then `LIVA_LIBPQ_PATH`:

```cpp
// === PostgreSQL (dynamic-loaded libpq; fail-closed when absent) ===
//
// libpq is NOT a link-time dependency. We resolve it at runtime:
//   - Windows: LoadLibrary("libpq.dll") (PATH), then standard install dirs
//     newest-first (PostgreSQL\18\bin, \17\bin, \14\bin), then $LIVA_LIBPQ_PATH.
//   - POSIX: dlopen libpq.so / libpq.so.5 / libpq.dylib, then $LIVA_LIBPQ_PATH.
// If libpq or any required symbol is missing, every entry point fails closed
// (connect -> 0, exec -> false, query -> 0, accessors -> empty/0).

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace {

// PGRES_COMMAND_OK = 1, PGRES_TUPLES_OK = 2, CONNECTION_OK = 0.
constexpr int LIVA_PGRES_COMMAND_OK = 1;
constexpr int LIVA_PGRES_TUPLES_OK = 2;
constexpr int LIVA_PG_CONNECTION_OK = 0;

struct PgApi {
    bool loaded = false;
    void *(*connectdb)(const char *) = nullptr;
    int   (*status)(void *) = nullptr;
    void  (*finish)(void *) = nullptr;
    void *(*exec)(void *, const char *) = nullptr;
    void *(*execParams)(void *, const char *, int, const void *,
                        const char *const *, const int *, const int *, int) = nullptr;
    int   (*resultStatus)(void *) = nullptr;
    int   (*ntuples)(void *) = nullptr;
    int   (*nfields)(void *) = nullptr;
    char *(*getvalue)(void *, int, int) = nullptr;
    int   (*getisnull)(void *, int, int) = nullptr;
    char *(*fname)(void *, int) = nullptr;
    char *(*cmdTuples)(void *) = nullptr;
    char *(*errorMessage)(void *) = nullptr;
    void  (*clear)(void *) = nullptr;
};

#ifdef _WIN32
static HMODULE pg_load_library() {
    if (HMODULE h = LoadLibraryA("libpq.dll")) return h;
    const char *vers[] = {"18", "17", "16", "15", "14", "13"};
    char path[256];
    for (const char *v : vers) {
        snprintf(path, sizeof(path),
                 "C:\\Program Files\\PostgreSQL\\%s\\bin\\libpq.dll", v);
        if (HMODULE h = LoadLibraryA(path)) return h;
    }
    if (const char *env = getenv("LIVA_LIBPQ_PATH")) {
        if (HMODULE h = LoadLibraryA(env)) return h;
    }
    return nullptr;
}
#else
static void *pg_load_library() {
    const char *names[] = {"libpq.so", "libpq.so.5", "libpq.dylib"};
    for (const char *n : names) {
        if (void *h = dlopen(n, RTLD_NOW | RTLD_GLOBAL)) return h;
    }
    if (const char *env = getenv("LIVA_LIBPQ_PATH")) {
        if (void *h = dlopen(env, RTLD_NOW | RTLD_GLOBAL)) return h;
    }
    return nullptr;
}
#endif

static PgApi &pg_api() {
    static PgApi api;
    static std::once_flag flag;
    std::call_once(flag, [&]() {
#ifdef _WIN32
        HMODULE h = pg_load_library();
        if (!h) return;
        auto resolve = [&](const char *name) -> FARPROC { return GetProcAddress(h, name); };
#else
        void *h = pg_load_library();
        if (!h) return;
        auto resolve = [&](const char *name) -> void * { return dlsym(h, name); };
#endif
        api.connectdb    = (decltype(api.connectdb))resolve("PQconnectdb");
        api.status       = (decltype(api.status))resolve("PQstatus");
        api.finish       = (decltype(api.finish))resolve("PQfinish");
        api.exec         = (decltype(api.exec))resolve("PQexec");
        api.execParams   = (decltype(api.execParams))resolve("PQexecParams");
        api.resultStatus = (decltype(api.resultStatus))resolve("PQresultStatus");
        api.ntuples      = (decltype(api.ntuples))resolve("PQntuples");
        api.nfields      = (decltype(api.nfields))resolve("PQnfields");
        api.getvalue     = (decltype(api.getvalue))resolve("PQgetvalue");
        api.getisnull    = (decltype(api.getisnull))resolve("PQgetisnull");
        api.fname        = (decltype(api.fname))resolve("PQfname");
        api.cmdTuples    = (decltype(api.cmdTuples))resolve("PQcmdTuples");
        api.errorMessage = (decltype(api.errorMessage))resolve("PQerrorMessage");
        api.clear        = (decltype(api.clear))resolve("PQclear");
        api.loaded = api.connectdb && api.status && api.finish && api.exec &&
                     api.resultStatus && api.errorMessage;
    });
    return api;
}

} // namespace

int64_t liva_pg_connect(const char *conninfo) {
    if (!conninfo) return 0;
    auto &api = pg_api();
    if (!api.loaded) return 0;
    void *conn = api.connectdb(conninfo);
    if (!conn) return 0;
    if (api.status(conn) != LIVA_PG_CONNECTION_OK) {
        api.finish(conn);
        return 0;
    }
    return (int64_t)(uintptr_t)conn;
}

void liva_pg_close(int64_t handle) {
    if (!handle) return;
    auto &api = pg_api();
    if (api.finish) api.finish((void *)(uintptr_t)handle);
}

// Run a no-result command. Returns 0 on success (COMMAND_OK or TUPLES_OK), -1 otherwise.
int32_t liva_pg_exec(int64_t handle, const char *sql) {
    if (!handle || !sql) return -1;
    auto &api = pg_api();
    if (!api.loaded || !api.exec || !api.clear) return -1;
    void *res = api.exec((void *)(uintptr_t)handle, sql);
    if (!res) return -1;
    int st = api.resultStatus(res);
    api.clear(res);
    return (st == LIVA_PGRES_COMMAND_OK || st == LIVA_PGRES_TUPLES_OK) ? 0 : -1;
}

char *liva_pg_errmsg(int64_t handle) {
    if (!handle) return nullptr;
    auto &api = pg_api();
    if (!api.loaded || !api.errorMessage) return strdup_safe("");
    const char *m = api.errorMessage((void *)(uintptr_t)handle);
    return strdup_safe(m ? m : "");
}
```

- [ ] **Step 2: Add the declarations to runtime.h**

In `stdlib/runtime/runtime.h`, after the SQLite block (after `liva_sqlite_finalize`, ~line 545):

```cpp
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
```

- [ ] **Step 3: Build to verify it compiles**

Run: `build_clang.bat`
Expected: build succeeds (no test yet — wiring lands in B3).

- [ ] **Step 4: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h
git commit -m "runtime: add libpq loader + pg connect/exec/errmsg (fail-closed)"
```

### Task B2: `pgNormalizeParams` native (quote-aware `?`→`$n`, libpq-independent)

Pure string processing — no libpq. Used by the `db::db` PgDatabase adapter. Implemented natively because Liva lacks per-character string indexing needed for a robust quote-aware scan.

**Files:**
- Modify: `stdlib/runtime/runtime.cpp`, `stdlib/runtime/runtime.h`
- Modify: `src/IR/IRGenCall.cpp`, `src/Sema/TypeChecker.cpp`, `src/Sema/ModuleLoader.cpp`
- Test: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST(RuntimeExecTest, PgNormalizeParams) {
    auto r = compileAndRun(
        "import postgres::postgres\n"
        "func main() {\n"
        "    println(pgNormalizeParams(\"SELECT * FROM t WHERE a=? AND b=?\"))\n"
        "    println(pgNormalizeParams(\"INSERT INTO t VALUES (?, '?lit?', ?)\"))\n"
        "    println(pgNormalizeParams(\"no params here\"))\n"
        "}\n",
        "pg_normalize");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output,
        "SELECT * FROM t WHERE a=$1 AND b=$2\n"
        "INSERT INTO t VALUES ($1, '?lit?', $2)\n"
        "no params here\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.PgNormalizeParams --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Add the runtime function**

In `stdlib/runtime/runtime.cpp`, after `liva_pg_errmsg`:

```cpp
// Rewrite '?' placeholders to PostgreSQL '$1','$2',... left to right. '?'
// inside single-quoted string literals ('...' with '' escape) is left alone.
// Known limitation: does NOT skip SQL comments (-- /* */) or dollar-quoting.
// Returns a fresh malloc'd string.
char *liva_pg_normalize_params(const char *sql) {
    if (!sql) return strdup_safe("");
    std::string out;
    bool inQuote = false;
    int n = 0;
    for (const char *p = sql; *p; ++p) {
        char c = *p;
        if (c == '\'') {
            // '' inside a quote is an escaped quote — stays inside.
            inQuote = !inQuote;
            out.push_back(c);
        } else if (c == '?' && !inQuote) {
            ++n;
            out.push_back('$');
            out.append(std::to_string(n));
        } else {
            out.push_back(c);
        }
    }
    char *res = (char *)malloc(out.size() + 1);
    if (!res) return nullptr;
    memcpy(res, out.data(), out.size());
    res[out.size()] = '\0';
    return res;
}
```

In `stdlib/runtime/runtime.h`, after `liva_pg_errmsg`:

```cpp
/// Rewrite '?' placeholders to '$1','$2',... (PostgreSQL style), skipping '?'
/// inside single-quoted string literals. Caller frees. libpq-independent.
char *liva_pg_normalize_params(const char *sql);
```

- [ ] **Step 4: Add the IRGen lowering**

In `src/IR/IRGenCall.cpp`, add a PG section (place it right before the SQLite `sqliteOpen` block at ~line 3255, so all DB intrinsics are together):

```cpp
    // pgNormalizeParams(sql) -> string
    if (funcName == "pgNormalizeParams" && !node->getArgs().empty()) {
        auto *sqlArg = visit(node->getArgs()[0].get());
        if (!sqlArg) return nullptr;
        auto *fn = getOrPanic("liva_pg_normalize_params");
        auto *r = builder_->CreateCall(fn, {sqlArg}, "pg.normparams");
        trackStringTemp(r);
        return r;
    }
```

- [ ] **Step 5: Register in TypeChecker + ModuleLoader**

In `src/Sema/TypeChecker.cpp`, add a new declaration list block near the SQLite one (~line 84, after the SQLite `for` loop closes) — or append to it. Add the names for the whole postgres module up front so later tasks don't re-touch this list:

```cpp
    for (const char *name : {"pgConnect", "pgClose", "pgExec", "pgErrmsg",
                             "pgNormalizeParams", "pgQuery", "pgQueryParams",
                             "pgResultRows", "pgResultCols", "pgResultText",
                             "pgResultIsNull", "pgColumnName", "pgClear"}) {
        Symbol sym;
        sym.name = name;
        sym.kind = Symbol::Kind::Function;
        scopes_.declare(name, sym);
    }
```

In the return-type switch (after the SQLite cases, ~line 2359), add the postgres cases up front (later tasks reference these names; defining them now avoids re-editing). For names whose backing function doesn't exist until B4/B6, that's fine — the type is resolved at compile time regardless:

```cpp
        // Stdlib: PostgreSQL
        } else if (ident->getName() == "pgConnect") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "pgClose" || ident->getName() == "pgClear") {
            // void
        } else if (ident->getName() == "pgExec") {
            node->setResolvedType(makeBoolType());
        } else if (ident->getName() == "pgErrmsg" ||
                   ident->getName() == "pgNormalizeParams" ||
                   ident->getName() == "pgResultText" ||
                   ident->getName() == "pgColumnName") {
            node->setResolvedType(makeStringType());
        } else if (ident->getName() == "pgQuery" ||
                   ident->getName() == "pgQueryParams") {
            node->setResolvedType(makeI64Type());
        } else if (ident->getName() == "pgResultRows" ||
                   ident->getName() == "pgResultCols") {
            node->setResolvedType(makeI32Type());
        } else if (ident->getName() == "pgResultIsNull") {
            node->setResolvedType(makeBoolType());
```

In `src/Sema/ModuleLoader.cpp`, after the `std::sqlite` registration (~line 84):

```cpp
    cache_["std::postgres"] = createBuiltinModule("std::postgres",
        {"pgConnect", "pgClose", "pgExec", "pgErrmsg", "pgNormalizeParams",
         "pgQuery", "pgQueryParams", "pgResultRows", "pgResultCols",
         "pgResultText", "pgResultIsNull", "pgColumnName", "pgClear"});
```

- [ ] **Step 6: Create the postgres wrapper stub so the import resolves**

Create `stdlib/postgres/postgres.liva` with just enough for the normalize test to import the module:

```liva
// postgres::postgres — PostgreSQL client (libpq, dynamic, fail-closed)
// Import with: import postgres::postgres
//
// libpq is resolved at runtime; if it (or a running server) is absent, open
// returns nil, exec returns false, and queries return nil. On this dev machine
// libpq ships with PostgreSQL 14/17/18 under C:\Program Files\PostgreSQL.

import std::postgres

// Rewrite '?' placeholders to PostgreSQL '$1','$2',... (skips quoted literals).
// Exposed for the db::db layer; libpq-independent.
pub func normalizeParams(sql: String) -> String {
    return pgNormalizeParams(sql)
}
```

> Note: the test calls the builtin `pgNormalizeParams` directly (it's a module symbol), so the stub's `normalizeParams` wrapper is optional for the test but used later by the db layer. Keep both.

- [ ] **Step 7: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.PgNormalizeParams --output-on-failure`
Expected: PASS — the three normalized lines.

- [ ] **Step 8: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp src/Sema/ModuleLoader.cpp stdlib/postgres/postgres.liva tests/unit/RuntimeExecTest.cpp
git commit -m "postgres: add pgNormalizeParams + module scaffolding"
```

### Task B3: `PgConn` wrapper — connect/exec/errmsg/close + IRGen for B1

The runtime for these landed in B1; TypeChecker/ModuleLoader names landed in B2. This task adds the IRGen blocks for `pgConnect`/`pgClose`/`pgExec`/`pgErrmsg` and the `PgConn` struct.

**Files:**
- Modify: `src/IR/IRGenCall.cpp`
- Modify: `stdlib/postgres/postgres.liva`
- Test: `tests/unit/RuntimeExecTest.cpp` (fail-closed), `tests/unit/StdlibModuleTest.cpp` (type-check)

- [ ] **Step 1: Write the failing fail-closed test**

When libpq/server is absent OR `LIVA_PG_TEST_CONN` is unset, `open` returns nil. This test asserts the fail-closed path with a deliberately bogus connstring (so it fails even if libpq loads):

```cpp
TEST(RuntimeExecTest, PgConnectFailClosed) {
    auto r = compileAndRun(
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"host=256.256.256.256 dbname=nope connect_timeout=1\") {\n"
        "        var conn = c\n"
        "        println(\"connected\")\n"
        "        conn.close()\n"
        "    } else {\n"
        "        println(\"failclosed\")\n"
        "    }\n"
        "}\n",
        "pg_failclosed");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "failclosed\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.PgConnectFailClosed --output-on-failure`
Expected: FAIL (`PgConn` unknown / `pgConnect` not lowered → compile failure).

- [ ] **Step 3: Add the IRGen lowering for connect/close/exec/errmsg**

In `src/IR/IRGenCall.cpp`, in the PG section (right after the `pgNormalizeParams` block):

```cpp
    // pgConnect(conninfo) -> i64
    if (funcName == "pgConnect" && !node->getArgs().empty()) {
        auto *s = visit(node->getArgs()[0].get());
        if (!s) return nullptr;
        auto *fn = getOrPanic("liva_pg_connect");
        return builder_->CreateCall(fn, {s}, "pg.connect");
    }

    // pgClose(handle) -> void
    if (funcName == "pgClose" && !node->getArgs().empty()) {
        auto *h = visit(node->getArgs()[0].get());
        if (!h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_pg_close"), {h});
        return nullptr;
    }

    // pgExec(handle, sql) -> bool
    if (funcName == "pgExec" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        if (!h || !sql) return nullptr;
        auto *rc = builder_->CreateCall(getOrPanic("liva_pg_exec"), {h, sql}, "pg.exec.rc");
        return builder_->CreateICmpEQ(rc, builder_->getInt32(0), "pg.exec.ok");
    }

    // pgErrmsg(handle) -> string
    if (funcName == "pgErrmsg" && !node->getArgs().empty()) {
        auto *h = visit(node->getArgs()[0].get());
        if (!h) return nullptr;
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_errmsg"), {h}, "pg.errmsg");
        trackStringTemp(r);
        return r;
    }
```

- [ ] **Step 4: Add the `PgConn` struct to the wrapper**

In `stdlib/postgres/postgres.liva`, after the `normalizeParams` function:

```liva
// =============================================================
// PgConn — a single PostgreSQL connection (PGconn*)
//
// Lifetime: PgConn.open() returns the only owner; .close() releases it.
// Subsequent calls fail closed.
// =============================================================

pub struct PgConn {
    var handle: i64
}

impl PgConn {
    pub func open(connString: String) -> PgConn? {
        let h: i64 = pgConnect(connString)
        if h == 0 as i64 { return nil }
        return PgConn { handle: h }
    }

    // Run a statement that produces no rows: CREATE, INSERT, UPDATE, etc.
    pub func exec(ref self, sql: String) -> bool {
        return pgExec(self.handle, sql)
    }

    pub func errorMessage(ref self) -> String {
        return pgErrmsg(self.handle)
    }

    pub func close(mut self) {
        pgClose(self.handle)
        self.handle = 0 as i64
    }
}
```

- [ ] **Step 5: Add a type-check test in StdlibModuleTest**

Add to `tests/unit/StdlibModuleTest.cpp`:

```cpp
TEST_F(StdlibModuleTest, ImportPostgresModule) {
    auto r = check(
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"host=localhost\") {\n"
        "        var conn = c\n"
        "        conn.exec(\"SELECT 1\")\n"
        "        let e = conn.errorMessage()\n"
        "        conn.close()\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import postgres::postgres should resolve PgConn";
}
```

- [ ] **Step 6: Rebuild and run both tests**

Run: `build_clang.bat` then
`ctest --test-dir build-clang -R "RuntimeExecTest.PgConnectFailClosed|StdlibModuleTest.ImportPostgresModule" --output-on-failure`
Expected: both PASS. (Fail-closed prints `failclosed`; type-check passes.)

- [ ] **Step 7: Commit**

```bash
git add src/IR/IRGenCall.cpp stdlib/postgres/postgres.liva tests/unit/RuntimeExecTest.cpp tests/unit/StdlibModuleTest.cpp
git commit -m "postgres: PgConn open/exec/errmsg/close (fail-closed)"
```

### Task B4: Result query + accessors (runtime)

**Files:**
- Modify: `stdlib/runtime/runtime.cpp`, `stdlib/runtime/runtime.h`

- [ ] **Step 1: Add the runtime functions**

In `stdlib/runtime/runtime.cpp`, after `liva_pg_normalize_params`:

```cpp
// Run a query, return an opaque PGresult* handle (0 on failure). Caller must
// call liva_pg_clear when done. Only TUPLES_OK results are returned; anything
// else is cleared and reported as failure.
int64_t liva_pg_query(int64_t handle, const char *sql) {
    if (!handle || !sql) return 0;
    auto &api = pg_api();
    if (!api.loaded || !api.exec) return 0;
    void *res = api.exec((void *)(uintptr_t)handle, sql);
    if (!res) return 0;
    if (api.resultStatus(res) != LIVA_PGRES_TUPLES_OK) {
        api.clear(res);
        return 0;
    }
    return (int64_t)(uintptr_t)res;
}

void liva_pg_clear(int64_t result) {
    if (!result) return;
    auto &api = pg_api();
    if (api.clear) api.clear((void *)(uintptr_t)result);
}

int32_t liva_pg_ntuples(int64_t result) {
    if (!result) return 0;
    auto &api = pg_api();
    if (!api.ntuples) return 0;
    return (int32_t)api.ntuples((void *)(uintptr_t)result);
}

int32_t liva_pg_nfields(int64_t result) {
    if (!result) return 0;
    auto &api = pg_api();
    if (!api.nfields) return 0;
    return (int32_t)api.nfields((void *)(uintptr_t)result);
}

char *liva_pg_getvalue(int64_t result, int32_t row, int32_t col) {
    if (!result) return nullptr;
    auto &api = pg_api();
    if (!api.getvalue) return strdup_safe("");
    const char *v = api.getvalue((void *)(uintptr_t)result, row, col);
    return strdup_safe(v ? v : "");
}

int32_t liva_pg_getisnull(int64_t result, int32_t row, int32_t col) {
    if (!result) return 1;
    auto &api = pg_api();
    if (!api.getisnull) return 1;
    return api.getisnull((void *)(uintptr_t)result, row, col) ? 1 : 0;
}

char *liva_pg_fname(int64_t result, int32_t col) {
    if (!result) return nullptr;
    auto &api = pg_api();
    if (!api.fname) return strdup_safe("");
    const char *n = api.fname((void *)(uintptr_t)result, col);
    return strdup_safe(n ? n : "");
}
```

In `stdlib/runtime/runtime.h`, after `liva_pg_normalize_params`:

```cpp
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
```

- [ ] **Step 2: Build to verify it compiles**

Run: `build_clang.bat`
Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h
git commit -m "runtime: add pg query + result accessors"
```

### Task B5: `PgResult` wrapper + `PgConn.query` + IRGen for accessors

TypeChecker/ModuleLoader names for these already landed in B2.

**Files:**
- Modify: `src/IR/IRGenCall.cpp`
- Modify: `stdlib/postgres/postgres.liva`
- Test: `tests/unit/StdlibModuleTest.cpp`

- [ ] **Step 1: Write the failing type-check test**

Add to `tests/unit/StdlibModuleTest.cpp`:

```cpp
TEST_F(StdlibModuleTest, PostgresResultMethods) {
    auto r = check(
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"host=localhost\") {\n"
        "        var conn = c\n"
        "        if let res = conn.query(\"SELECT 1\") {\n"
        "            var rs = res\n"
        "            let n = rs.rowCount()\n"
        "            let m = rs.colCount()\n"
        "            let t = rs.getText(0, 0)\n"
        "            let i = rs.getInt(0, 0)\n"
        "            let nul = rs.isNull(0, 0)\n"
        "            let name = rs.columnName(0)\n"
        "            rs.clear()\n"
        "        }\n"
        "        conn.close()\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "PgResult methods should type-check";
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R StdlibModuleTest.PostgresResultMethods --output-on-failure`
Expected: FAIL (`query`/`PgResult` unknown).

- [ ] **Step 3: Add the IRGen lowering for query + accessors**

In `src/IR/IRGenCall.cpp`, in the PG section after the `pgErrmsg` block:

```cpp
    // pgQuery(handle, sql) -> i64 (result handle)
    if (funcName == "pgQuery" && node->getArgs().size() >= 2) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        if (!h || !sql) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_pg_query"), {h, sql}, "pg.query");
    }

    // pgClear(result) -> void
    if (funcName == "pgClear" && !node->getArgs().empty()) {
        auto *res = visit(node->getArgs()[0].get());
        if (!res) return nullptr;
        builder_->CreateCall(getOrPanic("liva_pg_clear"), {res});
        return nullptr;
    }

    // pgResultRows(result) -> i32  /  pgResultCols(result) -> i32
    if ((funcName == "pgResultRows" || funcName == "pgResultCols") &&
        !node->getArgs().empty()) {
        auto *res = visit(node->getArgs()[0].get());
        if (!res) return nullptr;
        auto *fn = getOrPanic(funcName == "pgResultRows"
                              ? "liva_pg_ntuples" : "liva_pg_nfields");
        return builder_->CreateCall(fn, {res}, "pg.rescount");
    }

    // pgResultText(result, row, col) -> string
    if (funcName == "pgResultText" && node->getArgs().size() >= 3) {
        auto *res = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!res || !row || !col) return nullptr;
        if (row->getType()->isIntegerTy(64))
            row = builder_->CreateTrunc(row, builder_->getInt32Ty());
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_getvalue"), {res, row, col}, "pg.getval");
        trackStringTemp(r);
        return r;
    }

    // pgResultIsNull(result, row, col) -> bool
    if (funcName == "pgResultIsNull" && node->getArgs().size() >= 3) {
        auto *res = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!res || !row || !col) return nullptr;
        if (row->getType()->isIntegerTy(64))
            row = builder_->CreateTrunc(row, builder_->getInt32Ty());
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *rc = builder_->CreateCall(getOrPanic("liva_pg_getisnull"), {res, row, col}, "pg.isnull.rc");
        return builder_->CreateICmpNE(rc, builder_->getInt32(0), "pg.isnull");
    }

    // pgColumnName(result, col) -> string
    if (funcName == "pgColumnName" && node->getArgs().size() >= 2) {
        auto *res = visit(node->getArgs()[0].get());
        auto *col = visit(node->getArgs()[1].get());
        if (!res || !col) return nullptr;
        if (col->getType()->isIntegerTy(64))
            col = builder_->CreateTrunc(col, builder_->getInt32Ty());
        auto *r = builder_->CreateCall(getOrPanic("liva_pg_fname"), {res, col}, "pg.fname");
        trackStringTemp(r);
        return r;
    }
```

- [ ] **Step 4: Add `PgResult` + `PgConn.query` to the wrapper**

In `stdlib/postgres/postgres.liva`. Declare `PgResult` BEFORE `PgConn` (the import loader is single-pass over declarations, mirroring how `Stmt` precedes `SqliteDB`). Move the `PgResult` struct above `PgConn`, then add `query` to `impl PgConn`:

```liva
// =============================================================
// PgResult — a fully-materialized query result (PGresult*)
// Row and column indices are 0-based.
// =============================================================

pub struct PgResult {
    var handle: i64
}

impl PgResult {
    pub func rowCount(ref self) -> i32 {
        return pgResultRows(self.handle)
    }

    pub func colCount(ref self) -> i32 {
        return pgResultCols(self.handle)
    }

    pub func getText(ref self, row: i32, col: i32) -> String {
        return pgResultText(self.handle, row, col)
    }

    pub func getInt(ref self, row: i32, col: i32) -> i64 {
        let s = pgResultText(self.handle, row, col)
        return s.indexOf("")  // placeholder — replaced below
    }

    pub func isNull(ref self, row: i32, col: i32) -> bool {
        return pgResultIsNull(self.handle, row, col)
    }

    pub func columnName(ref self, col: i32) -> String {
        return pgColumnName(self.handle, col)
    }

    pub func clear(mut self) {
        pgClear(self.handle)
        self.handle = 0 as i64
    }
}
```

`getInt` parses the text to i64 using `convert::convert`'s `toInt64(s) -> i64?`, unwrapped with nil-coalescing. Add the import at the top of the file (after `import std::postgres`):

```liva
import convert::convert
```

And the correct `getInt` (the placeholder body shown in the struct above must be replaced with this):

```liva
    pub func getInt(ref self, row: i32, col: i32) -> i64 {
        let s = pgResultText(self.handle, row, col)
        return toInt64(s) ?? (0 as i64)
    }
```

> Verified against the codebase: `stdlib/convert/convert.liva` exposes `pub func toInt64(s: String) -> i64?` and `??` nil-coalescing is supported (`examples/optional_chaining.liva`). No invented names.

Add `query` to `impl PgConn` (after `exec`):

```liva
    pub func query(ref self, sql: String) -> PgResult? {
        let h: i64 = pgQuery(self.handle, sql)
        if h == 0 as i64 { return nil }
        return PgResult { handle: h }
    }
```

- [ ] **Step 5: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R StdlibModuleTest.PostgresResultMethods --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/IR/IRGenCall.cpp stdlib/postgres/postgres.liva tests/unit/StdlibModuleTest.cpp
git commit -m "postgres: PgResult + PgConn.query"
```

### Task B6: `queryParams` via `PQexecParams` ([String] bridge)

Passes a `[String]` array as `char** values, int n`. A `[String]` is a DynArray whose `.data` is a contiguous buffer of `char*`. We pass `.data` (as the values array) and `.length`.

**Files:**
- Modify: `stdlib/runtime/runtime.cpp`, `stdlib/runtime/runtime.h`
- Modify: `src/IR/IRGenCall.cpp`, `src/Sema/TypeChecker.cpp` (name already declared in B2; add return-type case)
- Modify: `stdlib/postgres/postgres.liva`
- Test: `tests/unit/StdlibModuleTest.cpp`

- [ ] **Step 1: Verify the `[String]` memory layout**

Run: `grep -n "DynArray\|getDynArrayStructTy\|element.*ptr\|i8\*\* \|char \*\*" src/IR/IRGen.cpp src/IR/IRGenExpr.cpp | head`
Confirm a `[String]`'s backing buffer holds consecutive string pointers (the same `.data` field used by `bytesToStr`, but element size = pointer). If confirmed, `.data` reinterpreted as `char**` + `.length` is the values array. If the layout differs (e.g. strings stored inline rather than as pointers), fall back to the documented alternative in Step 4b. Record the finding as a comment in the runtime function.

- [ ] **Step 2: Add the runtime function**

In `stdlib/runtime/runtime.cpp`, after `liva_pg_fname`:

```cpp
// Parameterized query via PQexecParams. `values` is an array of `nparams`
// C-string pointers (all text format; server coerces by column type). NULL
// entries bind SQL NULL. Returns an opaque PGresult* (0 on failure); only
// TUPLES_OK is returned (use liva_pg_exec for non-row commands).
int64_t liva_pg_query_params(int64_t handle, const char *sql,
                             const char *const *values, int64_t nparams) {
    if (!handle || !sql) return 0;
    auto &api = pg_api();
    if (!api.loaded || !api.execParams) return 0;
    void *res = api.execParams((void *)(uintptr_t)handle, sql, (int)nparams,
                               nullptr, values, nullptr, nullptr, 0);
    if (!res) return 0;
    int st = api.resultStatus(res);
    if (st != LIVA_PGRES_TUPLES_OK && st != LIVA_PGRES_COMMAND_OK) {
        api.clear(res);
        return 0;
    }
    return (int64_t)(uintptr_t)res;
}
```

In `stdlib/runtime/runtime.h`, after `liva_pg_fname`:

```cpp
/// Parameterized query. `values` holds `nparams` text C-strings (NULL = SQL
/// NULL). Returns opaque PGresult* i64 (0 on failure). Caller frees via clear.
int64_t liva_pg_query_params(int64_t handle, const char *sql,
                             const char *const *values, int64_t nparams);
```

- [ ] **Step 3: Add the IRGen lowering ([String] → char**)**

In `src/IR/IRGenCall.cpp`, in the PG section after `pgColumnName`:

```cpp
    // pgQueryParams(handle, sql, params: [String]) -> i64 (result handle)
    if (funcName == "pgQueryParams" && node->getArgs().size() >= 3) {
        auto *h = visit(node->getArgs()[0].get());
        auto *sql = visit(node->getArgs()[1].get());
        auto *arr = visit(node->getArgs()[2].get());
        if (!h || !sql || !arr) return nullptr;
        auto *daTy = getDynArrayStructTy();
        auto *funcCur = builder_->GetInsertBlock()->getParent();
        auto *src = createEntryBlockAlloca(funcCur, "pgp.src", daTy);
        builder_->CreateStore(arr, src);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        // .data is the contiguous buffer of char* (string pointers).
        auto *data = builder_->CreateLoad(ptrTy,
            builder_->CreateStructGEP(daTy, src, 0));
        auto *len = builder_->CreateLoad(builder_->getInt64Ty(),
            builder_->CreateStructGEP(daTy, src, 1));
        auto *fn = getOrPanic("liva_pg_query_params");
        return builder_->CreateCall(fn, {h, sql, data, len}, "pg.queryparams");
    }
```

- [ ] **Step 4: TypeChecker return-type case**

`pgQueryParams` is already in the B2 name-declaration list and the B2 return-type switch (mapped to `makeI64Type()` alongside `pgQuery`). No new TypeChecker edit needed. Verify by re-reading the switch; if missing, add:

```cpp
        } else if (ident->getName() == "pgQuery" ||
                   ident->getName() == "pgQueryParams") {
            node->setResolvedType(makeI64Type());
```

- [ ] **Step 4b: Fallback if Step 1 found a non-pointer `[String]` layout**

If `[String]` does not store consecutive `char*`, instead serialize params in Liva by joining with a NUL-free delimiter and splitting in C. Replace Step 2's signature with `liva_pg_query_params(int64_t handle, const char *sql, const char *joined, int64_t nparams, const char *delim)` and the IRGen with a string-passing call. (Only do this if Step 1 proves the pointer layout wrong — the pointer layout is expected.)

- [ ] **Step 5: Add `queryParams` to the wrapper**

In `stdlib/postgres/postgres.liva`, `impl PgConn`, after `query`:

```liva
    // Parameterized query. Use '?' placeholders in `sql` (this method does NOT
    // rewrite them — pass '$1','$2',... directly, or use db::db for '?' style).
    // All params are sent as text; the server coerces by column type.
    pub func queryParams(ref self, sql: String, params: [String]) -> PgResult? {
        let h: i64 = pgQueryParams(self.handle, sql, params)
        if h == 0 as i64 { return nil }
        return PgResult { handle: h }
    }
```

- [ ] **Step 6: Add a type-check test**

Add to `tests/unit/StdlibModuleTest.cpp`:

```cpp
TEST_F(StdlibModuleTest, PostgresQueryParams) {
    auto r = check(
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"host=localhost\") {\n"
        "        var conn = c\n"
        "        let args: [String] = [\"1\", \"abc\"]\n"
        "        if let res = conn.queryParams(\"SELECT $1, $2\", args) {\n"
        "            var rs = res\n"
        "            rs.clear()\n"
        "        }\n"
        "        conn.close()\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "queryParams should type-check";
}
```

- [ ] **Step 7: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R StdlibModuleTest.PostgresQueryParams --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h src/IR/IRGenCall.cpp src/Sema/TypeChecker.cpp stdlib/postgres/postgres.liva tests/unit/StdlibModuleTest.cpp
git commit -m "postgres: add queryParams via PQexecParams ([String] bridge)"
```

### Task B7 (optional, opt-in): real PostgreSQL round-trip test

Gated on `LIVA_PG_TEST_CONN`; skipped when unset so CI stays green. Uses real libpq (present on this machine).

- [ ] **Step 1: Add the gated test to RuntimeExecTest.cpp**

```cpp
TEST(RuntimeExecTest, PgRealRoundTrip) {
    const char *conn = std::getenv("LIVA_PG_TEST_CONN");
    if (!conn) GTEST_SKIP() << "LIVA_PG_TEST_CONN not set — skipping real PG test";
    std::string src =
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"" + std::string(conn) + "\") {\n"
        "        var conn = c\n"
        "        conn.exec(\"DROP TABLE IF EXISTS liva_pg_test\")\n"
        "        conn.exec(\"CREATE TABLE liva_pg_test(id INT, name TEXT)\")\n"
        "        conn.exec(\"INSERT INTO liva_pg_test VALUES (1, 'Ada')\")\n"
        "        if let res = conn.query(\"SELECT name FROM liva_pg_test WHERE id = 1\") {\n"
        "            var rs = res\n"
        "            println(rs.getText(0, 0))\n"
        "            rs.clear()\n"
        "        }\n"
        "        conn.exec(\"DROP TABLE liva_pg_test\")\n"
        "        conn.close()\n"
        "    } else { println(\"noconn\") }\n"
        "}\n";
    auto r = compileAndRun(src, "pg_real");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "Ada\n");
}
```

- [ ] **Step 2: Build, then run with a live connection (manual)**

Run: `build_clang.bat` then (PowerShell)
`$env:LIVA_PG_TEST_CONN = "host=localhost dbname=postgres user=postgres password=YOURPW"; ctest --test-dir build-clang -R RuntimeExecTest.PgRealRoundTrip --output-on-failure`
Expected: PASS (stdout `Ada\n`) with a live server; SKIPPED without `LIVA_PG_TEST_CONN`.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/RuntimeExecTest.cpp
git commit -m "postgres: opt-in real round-trip test (LIVA_PG_TEST_CONN)"
```

---

## Phase C — Unified DB layer (`db::db`, pure Liva)

### Task C1: `Row` struct

**Files:**
- Create: `stdlib/db/db.liva`
- Test: `tests/unit/StdlibModuleTest.cpp`

- [ ] **Step 1: Write the failing type-check test**

Add to `tests/unit/StdlibModuleTest.cpp`:

```cpp
TEST_F(StdlibModuleTest, ImportDbRow) {
    auto r = check(
        "import db::db\n"
        "func main() {\n"
        "    let names: [String] = [\"id\", \"name\"]\n"
        "    let vals: [String] = [\"7\", \"Ada\"]\n"
        "    let nulls: [bool] = [false, false]\n"
        "    let row = Row { names: names, vals: vals, nulls: nulls }\n"
        "    let t = row.getText(1)\n"
        "    let i = row.getInt(0)\n"
        "    let nul = row.isNull(0)\n"
        "    let bn = row.byName(\"name\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "db::db Row should type-check";
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R StdlibModuleTest.ImportDbRow --output-on-failure`
Expected: FAIL (module not found).

- [ ] **Step 3: Create `stdlib/db/db.liva` with `Row`**

Uses `convert::convert`'s `toInt64(s) -> i64?` (verified to exist) with `??` nil-coalescing for `getInt`.

```liva
// db::db — driver-agnostic database layer over sqlite and postgres.
// Import with: import db::db
//
// Provides a `Database` protocol (dynamic dispatch via `dyn Database`), a
// materialized `Row`, and adapters SqliteDatabase / PgDatabase. Callers always
// write '?' placeholders; the postgres adapter rewrites them to '$1','$2',...

import convert::convert

// =============================================================
// Row — one materialized result row. Column indices are 0-based.
// =============================================================

pub struct Row {
    var names: [String]
    var vals:  [String]
    var nulls: [bool]
}

impl Row {
    pub func getText(ref self, col: i32) -> String {
        return self.vals[col]
    }

    pub func getInt(ref self, col: i32) -> i64 {
        return toInt64(self.vals[col]) ?? (0 as i64)
    }

    pub func isNull(ref self, col: i32) -> bool {
        return self.nulls[col]
    }

    pub func byName(ref self, name: String) -> String? {
        for i in 0..self.names.length {
            if self.names[i] == name {
                return self.vals[i]
            }
        }
        return nil
    }
}
```

> Verified: `convert::convert` exposes `toInt64(s) -> i64?`; `??` nil-coalescing is supported. `getInt` indexing uses `i32` against arrays — if the compiler requires `i64` indices, the verification build will surface it; cast with `col as i64` where needed.

- [ ] **Step 4: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R StdlibModuleTest.ImportDbRow --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add stdlib/db/db.liva tests/unit/StdlibModuleTest.cpp
git commit -m "db: add Row materialized result type"
```

### Task C2: `Database` protocol + `SqliteDatabase` adapter

**Files:**
- Modify: `stdlib/db/db.liva`
- Test: `tests/unit/RuntimeExecTest.cpp` (real, via SQLite)

- [ ] **Step 1: Write the failing behavioral test**

```cpp
TEST(RuntimeExecTest, DbLayerSqliteAdapter) {
    auto r = compileAndRun(
        "import db::db\n"
        "func main() {\n"
        "    if let d = SqliteDatabase.openMemory() {\n"
        "        var db = d\n"
        "        db.exec(\"CREATE TABLE u(id INTEGER, name TEXT)\")\n"
        "        db.exec(\"INSERT INTO u VALUES (1, 'Ada')\")\n"
        "        db.exec(\"INSERT INTO u VALUES (2, 'Lin')\")\n"
        "        let rows = db.query(\"SELECT id, name FROM u WHERE id > ?\", [\"0\"])\n"
        "        println(rows.length)\n"
        "        for row in rows { println(row.getText(1)) }\n"
        "        if let r0 = rows[0].byName(\"name\") { println(r0) }\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        "db_sqlite_adapter");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "2\nAda\nLin\nAda\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.DbLayerSqliteAdapter --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Add the protocol + SqliteDatabase to `db.liva`**

Append to `stdlib/db/db.liva` (imports at top of file: add `import sqlite::sqlite`):

```liva
import sqlite::sqlite

// =============================================================
// Database — driver-agnostic protocol. Always use '?' placeholders;
// adapters translate to the driver's native style.
// =============================================================

pub protocol Database {
    func exec(ref self, sql: String) -> bool
    func query(ref self, sql: String, params: [String]) -> [Row]
    func lastInsertId(ref self) -> i64
    func errorMessage(ref self) -> String
    func close(mut self)
}

// =============================================================
// SqliteDatabase — Database backed by sqlite::SqliteDB.
// =============================================================

pub struct SqliteDatabase {
    var db: SqliteDB
}

impl SqliteDatabase {
    pub func open(path: String) -> SqliteDatabase? {
        if let d = SqliteDB.open(path) {
            return SqliteDatabase { db: d }
        }
        return nil
    }

    pub func openMemory() -> SqliteDatabase? {
        return SqliteDatabase.open(":memory:")
    }
}

impl SqliteDatabase: Database {
    func exec(ref self, sql: String) -> bool {
        return self.db.exec(sql)
    }

    func query(ref self, sql: String, params: [String]) -> [Row] {
        var rows: [Row] = []
        // SQLite uses '?' natively — no rewrite needed.
        if let s = self.db.prepare(sql) {
            var stmt = s
            var i: i32 = 0
            for p in params {
                i = i + 1
                stmt.bindText(i, p)
            }
            let cols = stmt.columnCount()
            while stmt.step() {
                var names: [String] = []
                var vals: [String] = []
                var nulls: [bool] = []
                var c: i32 = 0
                while c < cols {
                    names.push(stmt.columnName(c))
                    vals.push(stmt.columnText(c))
                    nulls.push(stmt.columnIsNull(c))
                    c = c + 1
                }
                rows.push(Row { names: names, vals: vals, nulls: nulls })
            }
            stmt.finalize()
        }
        return rows
    }

    func lastInsertId(ref self) -> i64 {
        return self.db.lastInsertId()
    }

    func errorMessage(ref self) -> String {
        return self.db.errorMessage()
    }

    func close(mut self) {
        self.db.close()
    }
}
```

> Verification sub-steps for this step (Liva idioms that must match the codebase — check against existing stdlib before/after writing):
> 1. Array append: confirm the method is `arr.push(x)`. Run `grep -rn "\.push(" stdlib examples | head`. If arrays use a different append (e.g. `arr.append(x)` or `arr += [x]`), use that form consistently.
> 2. Empty typed array literal `var rows: [Row] = []` — confirm this is valid; `grep -rn ": \[.*\] = \[\]" stdlib examples | head`. If not, initialize differently (e.g. construct then push).
> 3. `for p in params` over a `[String]` parameter and `while c < cols` with `i32` — confirm loop forms compile (the build in Step 4 is the gate).
> 4. Mutating `self.db` through `ref self`/`mut self`: `bindText`/`step` take `ref self`/`mut self`; ensure calling them on `self.db` is allowed. If the compiler rejects mutation through `ref self`, change the adapter method receivers to `mut self` (the protocol allows `ref self`; adjust the protocol signature to `mut self` for `query` if needed and keep adapters consistent).

- [ ] **Step 4: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.DbLayerSqliteAdapter --output-on-failure`
Expected: PASS — stdout `2\nAda\nLin\nAda\n`. If it fails to compile, resolve the idioms flagged in Step 3's verification sub-steps, then re-run.

- [ ] **Step 5: Commit**

```bash
git add stdlib/db/db.liva tests/unit/RuntimeExecTest.cpp
git commit -m "db: add Database protocol + SqliteDatabase adapter"
```

### Task C3: `PgDatabase` adapter

**Files:**
- Modify: `stdlib/db/db.liva`
- Test: `tests/unit/StdlibModuleTest.cpp` (type-check) + `tests/unit/RuntimeExecTest.cpp` (gated real test, optional)

- [ ] **Step 1: Write the failing type-check test**

Add to `tests/unit/StdlibModuleTest.cpp`:

```cpp
TEST_F(StdlibModuleTest, DbPgAdapterTypeCheck) {
    auto r = check(
        "import db::db\n"
        "func main() {\n"
        "    if let d = PgDatabase.open(\"host=localhost\") {\n"
        "        var db: dyn Database = d\n"
        "        db.exec(\"CREATE TABLE t(id INT)\")\n"
        "        let rows = db.query(\"SELECT id FROM t WHERE id > ?\", [\"0\"])\n"
        "        let n = rows.length\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "PgDatabase should satisfy dyn Database";
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R StdlibModuleTest.DbPgAdapterTypeCheck --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Add `PgDatabase` to `db.liva`**

Add `import postgres::postgres` at the top, then append:

```liva
import postgres::postgres

// =============================================================
// PgDatabase — Database backed by postgres::PgConn. Rewrites '?' to '$n'.
// PostgreSQL has no implicit last-insert id; lastInsertId() returns 0 — use
// "INSERT ... RETURNING id" with query() instead.
// =============================================================

pub struct PgDatabase {
    var conn: PgConn
}

impl PgDatabase {
    pub func open(connString: String) -> PgDatabase? {
        if let c = PgConn.open(connString) {
            return PgDatabase { conn: c }
        }
        return nil
    }
}

impl PgDatabase: Database {
    func exec(ref self, sql: String) -> bool {
        return self.conn.exec(normalizeParams(sql))
    }

    func query(ref self, sql: String, params: [String]) -> [Row] {
        var rows: [Row] = []
        let pgSql = normalizeParams(sql)
        if let res = self.conn.queryParams(pgSql, params) {
            var rs = res
            let nrows = rs.rowCount()
            let ncols = rs.colCount()
            var names: [String] = []
            var c: i32 = 0
            while c < ncols {
                names.push(rs.columnName(c))
                c = c + 1
            }
            var r: i32 = 0
            while r < nrows {
                var vals: [String] = []
                var nulls: [bool] = []
                var cc: i32 = 0
                while cc < ncols {
                    vals.push(rs.getText(r, cc))
                    nulls.push(rs.isNull(r, cc))
                    cc = cc + 1
                }
                rows.push(Row { names: names, vals: vals, nulls: nulls })
                r = r + 1
            }
            rs.clear()
        }
        return rows
    }

    func lastInsertId(ref self) -> i64 {
        return 0 as i64
    }

    func errorMessage(ref self) -> String {
        return self.conn.errorMessage()
    }

    func close(mut self) {
        self.conn.close()
    }
}
```

> `normalizeParams` is the `pub func` defined in `postgres.liva` (Task B2). Each `Row` shares the same `names` array reference — that is fine (read-only). If the compiler disallows sharing, build `names` per row.

- [ ] **Step 4: Rebuild and run**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R StdlibModuleTest.DbPgAdapterTypeCheck --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add stdlib/db/db.liva tests/unit/StdlibModuleTest.cpp
git commit -m "db: add PgDatabase adapter with ?->$n normalization"
```

### Task C4: example program

**Files:**
- Create: `examples/db_unified_demo.liva`

- [ ] **Step 1: Write the example**

```liva
// Unified DB layer demo: the same code path works over SQLite and PostgreSQL.
// Run against SQLite (always available via winsqlite3 / libsqlite3).
import db::db

func dump(db: dyn Database) {
    let rows = db.query("SELECT id, name FROM users WHERE id > ?", ["0"])
    println("rows: \(rows.length)")
    for row in rows {
        println("\(row.getInt(0)): \(row.getText(1))")
    }
}

func main() {
    if let d = SqliteDatabase.openMemory() {
        var db = d
        db.exec("CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT)")
        db.exec("INSERT INTO users(name) VALUES ('Ada')")
        db.exec("INSERT INTO users(name) VALUES ('Linus')")
        dump(db)
        db.close()
    }
    // For PostgreSQL, swap the first line for:
    //   if let d = PgDatabase.open("host=localhost dbname=mydb user=me") {
    // The rest of the code is identical.
}
```

- [ ] **Step 2: Build + run the example via a behavioral test**

Add to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST(RuntimeExecTest, DbUnifiedDemo) {
    auto r = compileAndRun(
        "import db::db\n"
        "func dump(db: dyn Database) {\n"
        "    let rows = db.query(\"SELECT id, name FROM users WHERE id > ?\", [\"0\"])\n"
        "    println(\"rows: \\(rows.length)\")\n"
        "    for row in rows { println(\"\\(row.getInt(0)): \\(row.getText(1))\") }\n"
        "}\n"
        "func main() {\n"
        "    if let d = SqliteDatabase.openMemory() {\n"
        "        var db = d\n"
        "        db.exec(\"CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT)\")\n"
        "        db.exec(\"INSERT INTO users(name) VALUES ('Ada')\")\n"
        "        db.exec(\"INSERT INTO users(name) VALUES ('Linus')\")\n"
        "        dump(db)\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        "db_unified_demo");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "rows: 2\n1: Ada\n2: Linus\n");
}
```

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.DbUnifiedDemo --output-on-failure`
Expected: PASS — stdout `rows: 2\n1: Ada\n2: Linus\n`.

- [ ] **Step 3: Commit**

```bash
git add examples/db_unified_demo.liva tests/unit/RuntimeExecTest.cpp
git commit -m "db: add unified demo example + end-to-end test"
```

---

## Phase D — Docs, packaging, final regression

### Task D1: CMake install entries (optional parity)

The existing install list only covers `core` and `io`; tests resolve stdlib from `LIVA_PROJECT_ROOT/stdlib` directly, so this is parity-only, not test-critical.

**Files:** `CMakeLists.txt`

- [ ] **Step 1: Add install entries**

In `CMakeLists.txt`, after the `stdlib/io/` install line (~line 317):

```cmake
install(DIRECTORY stdlib/sqlite/ DESTINATION lib/liva/stdlib/sqlite FILES_MATCHING PATTERN "*.liva")
install(DIRECTORY stdlib/postgres/ DESTINATION lib/liva/stdlib/postgres FILES_MATCHING PATTERN "*.liva")
install(DIRECTORY stdlib/db/ DESTINATION lib/liva/stdlib/db FILES_MATCHING PATTERN "*.liva")
```

- [ ] **Step 2: Configure to verify CMake parses**

Run: `build_clang.bat` (re-runs CMake configure + build)
Expected: configure + build succeed.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: install sqlite/postgres/db stdlib modules"
```

### Task D2: API reference docs

**Files:** `docs/tr/API-REFERENCE.md`, `docs/en/API-REFERENCE.md`

- [ ] **Step 1: Locate the SQLite section in each doc**

Run: `grep -n "sqlite\|SqliteDB" docs/en/API-REFERENCE.md docs/tr/API-REFERENCE.md`
Note the insertion points (end of the SQLite section).

- [ ] **Step 2: Add to `docs/en/API-REFERENCE.md`**

Under the SQLite section, document the new `Stmt`/`SqliteDB` methods, then add `postgres::postgres` and `db::db` sections:

```markdown
#### New SQLite methods

- `Stmt.columnName(col) -> String` — result column name.
- `Stmt.columnType(col) -> i32` — 1=INTEGER, 2=FLOAT, 3=TEXT, 4=BLOB, 5=NULL.
- `Stmt.columnIsNull(col) -> bool` — true when the cell is NULL.
- `Stmt.bindByName(name, val) -> bool` — bind text to `:name`/`@name`/`$name`.
- `Stmt.bindBlob(idx, [u8]) -> bool`, `Stmt.columnBlob(col) -> [u8]` — binary data.
- `SqliteDB.begin()/commit()/rollback() -> bool` — transaction control.

### postgres::postgres

libpq-backed client, resolved dynamically at runtime. If libpq or a server is
absent, `PgConn.open` returns `nil`, `exec` returns `false`, queries return `nil`
(fail-closed). On Windows the loader probes `C:\Program Files\PostgreSQL\<ver>\bin\libpq.dll`
(newest first) and `$LIVA_LIBPQ_PATH`.

- `PgConn.open(connString) -> PgConn?` — `"host=... dbname=... user=..."`.
- `PgConn.exec(sql) -> bool` — no-result command.
- `PgConn.query(sql) -> PgResult?` / `queryParams(sql, [String]) -> PgResult?`.
- `PgConn.errorMessage() -> String`, `close()`.
- `PgResult.rowCount()/colCount() -> i32`, `getText(r,c)/getInt(r,c)`,
  `isNull(r,c) -> bool`, `columnName(c) -> String`, `clear()`.

### db::db

Driver-agnostic layer. Write `?` placeholders everywhere; the PostgreSQL
adapter rewrites them to `$1,$2,...` (single-quoted literals are skipped;
SQL comments and dollar-quoting are NOT — use the driver's `queryParams`
directly for those edge cases).

- `protocol Database { exec; query(sql, [String]) -> [Row]; lastInsertId;
  errorMessage; close }`
- `SqliteDatabase.open(path)? / openMemory()?` — `impl Database`.
- `PgDatabase.open(connString)?` — `impl Database` (`lastInsertId` returns 0;
  use `RETURNING`).
- `Row.getText(col)/getInt(col)/isNull(col)/byName(name) -> String?`.
- Use `dyn Database` for code that works across both drivers.
```

- [ ] **Step 3: Add the Turkish translation to `docs/tr/API-REFERENCE.md`**

Mirror the same structure in Turkish (translate the prose; keep code/identifiers as-is):

```markdown
#### Yeni SQLite metotları

- `Stmt.columnName(col) -> String` — sonuç kolon adı.
- `Stmt.columnType(col) -> i32` — 1=INTEGER, 2=FLOAT, 3=TEXT, 4=BLOB, 5=NULL.
- `Stmt.columnIsNull(col) -> bool` — hücre NULL ise true.
- `Stmt.bindByName(name, val) -> bool` — `:name`/`@name`/`$name` parametresine text bağlar.
- `Stmt.bindBlob(idx, [u8]) -> bool`, `Stmt.columnBlob(col) -> [u8]` — ikili veri.
- `SqliteDB.begin()/commit()/rollback() -> bool` — transaction kontrolü.

### postgres::postgres

libpq tabanlı istemci, çalışma anında dinamik çözülür. libpq veya sunucu yoksa
`PgConn.open` `nil`, `exec` `false`, sorgular `nil` döndürür (fail-closed).
Windows'ta yükleyici `C:\Program Files\PostgreSQL\<sürüm>\bin\libpq.dll`'i
(en yeni önce) ve `$LIVA_LIBPQ_PATH`'i yoklar.

- `PgConn.open(connString) -> PgConn?` — `"host=... dbname=... user=..."`.
- `PgConn.exec(sql) -> bool` — satır döndürmeyen komut.
- `PgConn.query(sql) -> PgResult?` / `queryParams(sql, [String]) -> PgResult?`.
- `PgConn.errorMessage() -> String`, `close()`.
- `PgResult.rowCount()/colCount() -> i32`, `getText(r,c)/getInt(r,c)`,
  `isNull(r,c) -> bool`, `columnName(c) -> String`, `clear()`.

### db::db

Sürücü-bağımsız katman. Her yerde `?` yer tutucusu yazın; PostgreSQL adapteri
bunları `$1,$2,...`'e çevirir (tek tırnaklı literal'ler atlanır; SQL yorumları
ve dollar-quoting atlanmaz — bu kenar durumlar için sürücünün `queryParams`'ını
doğrudan kullanın).

- `protocol Database { exec; query(sql, [String]) -> [Row]; lastInsertId;
  errorMessage; close }`
- `SqliteDatabase.open(path)? / openMemory()?` — `impl Database`.
- `PgDatabase.open(connString)?` — `impl Database` (`lastInsertId` 0 döndürür;
  `RETURNING` kullanın).
- `Row.getText(col)/getInt(col)/isNull(col)/byName(name) -> String?`.
- İki sürücüde de çalışan kod için `dyn Database` kullanın.
```

- [ ] **Step 4: Commit**

```bash
git add docs/en/API-REFERENCE.md docs/tr/API-REFERENCE.md
git commit -m "docs: document postgres, db layer, and new sqlite methods"
```

### Task D3: Full regression

- [ ] **Step 1: Run the entire suite**

Run: `ctest --test-dir build-clang --output-on-failure`
Expected: all tests pass — the prior 2064 plus the new SQLite/postgres/db tests. The gated `PgRealRoundTrip` is SKIPPED (no `LIVA_PG_TEST_CONN`).

- [ ] **Step 2: If any prior test regressed, fix before proceeding**

Investigate via `superpowers:systematic-debugging`. Most likely culprits: a TypeChecker name-list edit shadowing an existing name, or an IRGen block placed where an earlier `if` already returned. Re-run until green.

- [ ] **Step 3: Update memory test count**

Update `MEMORY.md` "2064 tests passing" to the new total, and note postgres/db modules under Recent Session.

- [ ] **Step 4: Final commit**

```bash
git add C:/Users/Kadir/.claude/projects/F--Cpp-Projects-liva-lang/memory/MEMORY.md
git commit -m "chore: bump test count + note db modules"
```

---

## Self-review notes

- **Spec coverage:** PostgreSQL module (B1–B6 + opt-in B7); unified layer with `?`→`$n` normalize (B2 native + C2/C3 adapters); SQLite transactions (A5), BLOB (A4), NULL/type (A2), named params (A3); `dyn Database` (C2/C3); fail-closed (B1/B3); install (D1); docs TR+EN (D2). All spec sections map to tasks.
- **BLOB `[u8]` bridge** (the highest-risk item) is verified against the exact `bytesToStr`/`gzipDecode` precedent and exercised by a real round-trip test in A4 — done early as the spec required.
- **`[String]` bridge** (B6) carries an explicit layout-verification step + documented fallback, since it reinterprets DynArray `.data` as `char**`.
- **Liva-idiom risks** (`arr.push`, empty typed-array literals, mutation through `ref self`, i32 vs i64 indices) are called out as verification sub-steps in C2; the build is the gate and fallbacks are specified.
- **Naming consistency:** runtime `liva_pg_*` / `liva_sqlite_*`; builtins `pg*` / `sqlite*`; wrappers `PgConn`/`PgResult`/`SqliteDatabase`/`PgDatabase`/`Row`. Every builtin added to BOTH the TypeChecker name list and the ModuleLoader module list (the two registration points that, if missed, cause "unknown identifier").
