# JSON Module — Parse-Tree Redesign (JsonDataObjects-style) — Design

- **Date:** 2026-06-04
- **Status:** Approved
- **Scope:** Replace the string-based `json::json` module with a memory-resident parse-tree (DOM) backend modeled on Delphi's *JsonDataObjects* — parse once, navigate fast, typed accessors, distinct `JsonObject`/`JsonArray`/`JsonValue` wrappers, path read + auto-vivifying write, `obj["key"]`/`arr[i]` subscript. Includes a scoped compiler change to support **struct subscript**. **Breaking change** — the old string-based API and its bindings are removed.

## 1. Goal & context

Today `stdlib/json/json.liva` exposes a single `JsonObject { data: String }` that holds the raw JSON text and **re-parses on every operation** via a hand-rolled top-level-only string scanner in `runtime.cpp` (`json_find_key`, `json_skip_value`, `json_extract_string`, …). There is no tree, no `JsonArray` type, no nested/path access, and no typed-optional gets. The native `liva_json_keys` exists but is never exposed.

The new design parses JSON **once** into a runtime-owned DOM and lets Liva navigate it through thin handle-holding wrappers. This is faster (no re-parse per access) and far more ergonomic (typed accessors, indexer syntax, path access, fluent building), matching the *JsonDataObjects* model the user requested.

## 2. Decisions (locked)

1. **Architecture:** runtime-owned parse tree (DOM), handle-based wrappers. (Not string-based.)
2. **Backward compatibility:** **fully replace** the old string-based `JsonObject`; reuse the `JsonObject` name for the new tree type. Old `liva_json_*` natives **and** their IRGen/TypeChecker/ModuleLoader bindings are removed.
3. **Memory model:** **owns-flag** — each wrapper carries `owns: bool`; the owner (parse/build result) frees the whole document arena in `Drop`; navigation returns non-owning views (`owns=false`, `Drop` is a no-op). **No refcounting** (Liva does not Drop temporaries, so per-wrapper refcount would leak on chained navigation). Move semantics + `movedVars` guarantee a single owner and prevent double-free.
4. **Wrapper kind:** **structs** (clean move semantics; wrapper fields are all primitive → non-Copy Named structs → single-owner). Plus a **scoped compiler change** to support struct `subscript` so `obj["key"]`/`arr[i]` work.
5. **v1 capability scope:** base skeleton + path read + auto-vivifying write + `kind()`/`is*` + optional typed gets (`try*`).

## 3. Runtime DOM (native, in `stdlib/runtime/runtime.cpp` + `.h`)

A compact recursive-descent parser + serializer building an arena-allocated node tree.

### 3.1 Node model

```
enum JsonKind { Null=0, Bool=1, Int=2, Double=3, String=4, Array=5, Object=6 }

struct JsonNode {
    JsonKind kind;
    union { bool b; int64_t i; double d; };
    std::string str;                                  // String
    std::vector<JsonNode*> arr;                       // Array
    std::vector<std::pair<std::string, JsonNode*>> obj; // Object (ordered)
};

struct JsonDoc {
    JsonNode* root;
    std::vector<JsonNode*> arena;  // every node, freed together
};
```

- Numbers: a JSON number with `.`/`e`/`E` → `Double`; otherwise `Int` (i64). (`asFloat` on an `Int` returns `(f64)i`; `asInt` on a `Double` truncates — degrade-to-default style.)
- Strings: parsed with the existing escape handling (`json_extract_string` logic reused).
- `JsonDoc` owns all nodes via `arena`; `jsonFreeDoc` frees the whole arena.

### 3.2 Handle encoding

- Wrapper carries `docHandle: i64` (= `JsonDoc*` cast) and `nodeHandle: i64` (= `JsonNode*` cast).
- The **owner** wrapper's `docHandle` identifies the arena to free; the `nodeHandle` is its root.
- Navigation mints a wrapper with the **same `docHandle`**, a child `nodeHandle`, and `owns=false`.

### 3.3 Native intrinsics (each wired through the 6-layer binding pattern)

Construction / lifetime:
- `liva_json_parse(const char* s) -> int64_t` — parse to a new `JsonDoc`; returns `docHandle` (root node = `doc->root`), or `0` on invalid JSON.
- `liva_json_new_object() -> int64_t` — new doc whose root is an empty Object.
- `liva_json_new_array() -> int64_t` — new doc whose root is an empty Array.
- `liva_json_free_doc(int64_t doc)` — free the whole arena. Idempotent (null-safe).
- `liva_json_root(int64_t doc) -> int64_t` — root node handle for a doc.

Navigation / typing (operate on `nodeHandle`):
- `liva_json_obj_get(int64_t node, const char* key) -> int64_t` — child node or `0`.
- `liva_json_arr_at(int64_t node, int64_t i) -> int64_t` — element node or `0`.
- `liva_json_node_kind(int64_t node) -> int32_t` — `JsonKind` (or `Null` for `0`).
- `liva_json_node_as_int(int64_t node) -> int64_t`
- `liva_json_node_as_float(int64_t node) -> double`
- `liva_json_node_as_bool(int64_t node) -> int8_t`
- `liva_json_node_as_string(int64_t node) -> char*` (caller frees; `""` for non-string)

Object mutation (operate on `docHandle`+`nodeHandle`; doc needed to arena-allocate new child nodes):
- `liva_json_obj_set_string/int/float/bool(int64_t doc, int64_t node, const char* key, …)`
- `liva_json_obj_set_null(int64_t doc, int64_t node, const char* key)`
- `liva_json_obj_set_object(int64_t doc, int64_t node, const char* key) -> int64_t` — set key to a new empty Object, return its node.
- `liva_json_obj_set_array(int64_t doc, int64_t node, const char* key) -> int64_t`
- `liva_json_obj_remove(int64_t node, const char* key)`
- `liva_json_obj_has(int64_t node, const char* key) -> int8_t`
- `liva_json_obj_count(int64_t node) -> int32_t`
- `liva_json_obj_keys(int64_t node, int64_t* count) -> char**` — exposed via the `[String]` DynArray bridge.

Array mutation:
- `liva_json_arr_count(int64_t node) -> int32_t`
- `liva_json_arr_add_string/int/float/bool(int64_t doc, int64_t node, …)`
- `liva_json_arr_add_null(int64_t doc, int64_t node)`
- `liva_json_arr_add_object(int64_t doc, int64_t node) -> int64_t`
- `liva_json_arr_add_array(int64_t doc, int64_t node) -> int64_t`

Path:
- `liva_json_path_get(int64_t node, const char* path) -> int64_t` — dotted/indexed path (`"a.b.0.c"`), `0` if missing/mismatched.
- `liva_json_path_set_string/int/float/bool(int64_t doc, int64_t node, const char* path, …)` — auto-vivify intermediate Objects (numeric segment → Array index where applicable). Path grammar: segments split on `.`; a segment that is all digits indexes an Array, otherwise a key into an Object.

Serialization:
- `liva_json_to_string(int64_t node) -> char*` — compact.
- `liva_json_to_string_pretty(int64_t node, int32_t indent) -> char*`.

> **try-gets and `is*` need NO new natives** — they are pure Liva over `node_kind` + `node_as_*`.

### 3.4 6-layer binding reminder (mandatory per intrinsic)

For every `liva_json_*` above: (1) runtime `.cpp`/`.h`; (2) **`IRGen.cpp createRuntimeDecls()`** `getOrInsertFunction` — omission → `getOrPanic` crash; (3) `IRGenCall.cpp` intrinsic lowering; (4) `TypeChecker.cpp` builtin name list + return-type case; (5) `ModuleLoader.cpp createBuiltinModule` (`std::json`); (6) `stdlib/json/json.liva` wrapper. Removed old `liva_json_*` ops must be deleted from layers 2–6 too.

## 4. Liva wrapper API (`stdlib/json/json.liva`)

Three structs + a `Json` entry struct + a `JsonKind` enum. All wrappers: `var docHandle: i64`, `var nodeHandle: i64`, `var owns: bool`, and an `impl … : Drop` with `func drop(mut self) { if self.owns { jsonFreeDoc(self.docHandle) } }`.

```liva
pub enum JsonKind { Null, Bool, Int, Double, Str, Arr, Obj }

pub struct JsonValue { var docHandle: i64  var nodeHandle: i64  var owns: bool }
pub struct JsonObject { var docHandle: i64  var nodeHandle: i64  var owns: bool }
pub struct JsonArray  { var docHandle: i64  var nodeHandle: i64  var owns: bool }
pub struct Json {}
```

### 4.1 `Json` (entry, owner-producing)
- `parse(s: String) -> JsonValue` — `owns=true`; `nodeHandle = jsonRoot(doc)`.
- `object() -> JsonObject` — `owns=true`, empty object doc.
- `array() -> JsonArray` — `owns=true`, empty array doc.

### 4.2 `JsonValue` (universal node; always a view)
- `kind() -> JsonKind`, `isNull/isBool/isInt/isDouble/isString/isArray/isObject -> bool`.
- `asString() -> String`, `asInt() -> i64`, `asFloat() -> f64`, `asBool() -> bool` (degrade-to-default).
- `object() -> JsonObject`, `array() -> JsonArray` (views; carry `owns=false`).
- `toString() -> String`, `toStringPretty(indent: i32) -> String`.

### 4.3 `JsonObject`
- `subscript(key: String) -> JsonValue` → `obj["k"]` (view).
- Typed (degrade-to-default): `getString(key) -> String`, `getInt(key) -> i64`, `getFloat(key) -> f64`, `getBool(key) -> bool`, `getObject(key) -> JsonObject`, `getArray(key) -> JsonArray`.
- Optional (safe): `tryString(key) -> String?`, `tryInt(key) -> i64?`, `tryFloat(key) -> f64?`, `tryBool(key) -> bool?` — return `nil` if missing **or** wrong kind. Pure-Liva over `kind()`.
- Mutation: `setString/setInt/setFloat/setBool(key, v)`, `setNull(key)`, `setObject(key) -> JsonObject` (view of new child), `setArray(key) -> JsonArray`. (`mut self`.)
- `remove(key)`, `has(key) -> bool`, `keys() -> [String]`, `count() -> i32`.
- `path(p: String) -> JsonValue` (view), `setPath(p, v)` family (auto-viv).
- `toString()/toStringPretty(indent)`.

### 4.4 `JsonArray`
- `subscript(i: i64) -> JsonValue` → `arr[i]` (view).
- `count() -> i32`, `length() -> i32` (alias).
- Typed: `getString(i)/getInt(i)/getFloat(i)/getBool(i)`, `getObject(i) -> JsonObject`, `getArray(i) -> JsonArray`, `at(i) -> JsonValue`.
- Mutation: `addString/addInt/addFloat/addBool(v)`, `addNull()`, `addObject() -> JsonObject`, `addArray() -> JsonArray`. (`mut self`.)
- `toString()/toStringPretty(indent)`.

### 4.5 Ownership rule (documented contract)

The value returned by `Json.parse`/`Json.object`/`Json.array` is the **sole owner** of the document and must be **bound to a `let`/`var`**. Everything reached from it (`obj["k"]`, `arr[i]`, `.object()`, `.path(...)`, typed getters returning `JsonObject`/`JsonArray`) is a **borrow valid only while the owner is alive**. Do **not** chain off a parse temporary (`Json.parse(s).object()`) — the temporary owner is never Dropped (Liva does not Drop temporaries) → arena leak. Bind first:

```liva
let doc = Json.parse(text)      // owner
let user = doc.object()         // view
let name = user.getString("name")
```

## 5. Compiler change: struct subscript (scoped, full-suite-gated)

Subscript codegen is currently **class-only** (`IRGenExpr.cpp visitIndexExpr` → `varClassTypes` → `ClassName_subscript`), and `TypeChecker::visitIndexExpr` resolves String/array/Named-element results but **not** subscript return types at all. Two scoped additions, mirroring the existing class path:

1. **`TypeChecker::visitIndexExpr`** — when the base resolves to a **struct (or class)** type that declares a `subscript` method, set the node's resolved type to that method's declared **return type** (clone). This is required so chained calls (`obj["k"].asInt()`, `arr[i].getString("x")`) type-check and codegen. Mirror the Named-return guard discipline used for array elements (set a resolved type only for the subscript-return case; do not perturb existing String/array handling).
2. **`IRGen::visitIndexExpr`** — add a struct branch mirroring the class branch (lines ~1088–1112): look up `vars_.varStructTypes` for the base identifier, resolve `StructName_subscript`, load `self` (struct alloca passed by pointer, as struct methods receive `self`), visit the index, and `CreateCall`. Handle a `String` index argument (passed as `ptr`) and a Named-struct/Named return materialized to a temp as needed (reuse the chained-call materialization added for typed DB accessors).

**Out of scope:** `subscript_set` (`obj["k"] = v`) — writes use `setString`/`addString`/etc. (YAGNI). Generic struct subscript — not needed.

**Risk & gate:** struct subscript with a `String` key and a Named return type is **untested at runtime** (existing subscript tests are class-only, sema/parse-only, `i32`/`i32`). The implementation must add a behavioral runtime test for `obj["k"].asInt()` and `arr[i].getString(...)` and verify the **full serial `ctest`** stays green (no `-j`). If the change perturbs unrelated index handling (cf. the `[u8]` sign-extension regression in the typed-DB task), narrow the new resolution to the subscript case only.

## 6. Testing

- **`RuntimeExecTest`** (behavioral, `compileAndRun`, real round-trip):
  - parse → `obj.getString/getInt/getFloat/getBool`; `obj["k"].asInt()` subscript; `arr[i]`/`arr.getString(i)`; nested `obj.getObject("a").getInt("b")`.
  - `path("a.b.0.c")` read; `setPath` auto-viv then read back.
  - build from scratch (`Json.object()`/`Json.array()` + `set*`/`add*`) → `toString()` round-trip parses back equal.
  - `keys()` returns expected `[String]`; `has`/`remove`/`count`.
  - `try*` returns `nil` on missing/wrong-kind, value otherwise.
  - Drop: owner goes out of scope without crash; building/large docs don't crash (arena freed once).
- **`StdlibModuleTest`** (type-check, `check(src,true,"stdlib")`): full new surface type-checks.
- Full **serial** suite green.

## 7. Files changed

- `stdlib/runtime/runtime.cpp` + `.h` — new DOM parser/serializer + all `liva_json_*` intrinsics; **remove** old string-based `liva_json_*`.
- `src/IR/IRGen.cpp` — `createRuntimeDecls()` entries (add new, remove old).
- `src/IR/IRGenCall.cpp` — lowering for new intrinsics (remove old); chained-call/materialization reuse.
- `src/IR/IRGenExpr.cpp` — struct subscript branch in `visitIndexExpr`.
- `src/Sema/TypeChecker.cpp` — new builtin names + return types; struct/class subscript return-type resolution in `visitIndexExpr`.
- `src/Sema/ModuleLoader.cpp` — rebuild `std::json` builtin module surface.
- `stdlib/json/json.liva` — full rewrite (3 wrappers + `Json` + `JsonKind` + `Drop`).
- `tests/unit/RuntimeExecTest.cpp`, `tests/unit/StdlibModuleTest.cpp` — new tests.
- `docs/{tr,en}/API-REFERENCE.md` — rewrite JSON section.

## 8. Out of scope (YAGNI)

- `subscript_set` (`obj["k"] = v`).
- JSON5/comments/trailing-comma tolerance (strict JSON only).
- Streaming/SAX parsing; number-as-bignum; preserving raw number lexemes beyond Int/Double.
- struct↔JSON (de)serialization / reflection.
- Refcounting / shared multi-owner documents (single-owner move model only).
