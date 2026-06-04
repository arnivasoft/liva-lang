# JSON Parse-Tree Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the string-based `json::json` module with a runtime-owned parse-tree (DOM) backend offering typed accessors, `obj["k"]`/`arr[i]` subscript, path read + auto-vivifying write, and fluent building — modeled on Delphi's JsonDataObjects.

**Architecture:** A compact recursive-descent parser in `runtime.cpp` builds an arena-allocated node tree (`JsonDoc`). Thin Liva structs (`JsonValue`/`JsonObject`/`JsonArray`) carry `{docHandle, nodeHandle, owns}` handles; the owner frees the whole arena in `Drop`, navigation returns non-owning views. A scoped compiler change adds struct `subscript` so `obj["k"]`/`arr[i]` work. Breaking change: the old string-based API and its bindings are removed.

**Tech Stack:** C++20 runtime (`stdlib/runtime/runtime.cpp`), LLVM IRGen (`src/IR/*`), Sema (`src/Sema/*`), Liva stdlib (`stdlib/json/json.liva`), GoogleTest (`tests/unit/*`), CMake/Ninja Clang build.

---

## Build & test commands (reference)

- **Build:** `build_clang.bat` (Ninja, output `build-clang/`). Run from repo root in PowerShell.
- **Run a single test:** `ctest --test-dir build-clang -R "<TestName>" --output-on-failure`
- **Full suite (MUST be serial — `-j` triggers pre-existing parallel races):** `ctest --test-dir build-clang --output-on-failure`
- After editing `stdlib/*.liva`, **rebuild** (the build copies stdlib into `build-clang/`); never `git stash` without rebuild when checking behavior.

## The 6-layer native-binding pattern (apply to EVERY new `liva_json_*`)

1. **`stdlib/runtime/runtime.cpp`** (+ declaration in `stdlib/runtime/runtime.h`) — the C function.
2. **`src/IR/IRGen.cpp` `createRuntimeDecls()`** — `module_->getOrInsertFunction("liva_json_…", <FnTy>)`. **MANDATORY** — omission → `getOrPanic` access-violation crash at codegen.
3. **`src/IR/IRGenCall.cpp`** — intrinsic lowering: `if (funcName == "json…") { … }`.
4. **`src/Sema/TypeChecker.cpp`** — add the builtin name to the loop at line ~129, and a return-type case in the switch at line ~2522.
5. **`src/Sema/ModuleLoader.cpp`** — add the name to the `std::json` list at line ~94.
6. **`stdlib/json/json.liva`** — the wrapper method calling the lowered builtin name.

Builtin Liva-facing names use camelCase (`jsonParse`); runtime C names use snake_case (`liva_json_parse`).

## IRGen gotchas (carry forward from prior DB work)

- `??` nil-coalescing **fails** in IRGen inside non-optional-returning functions → use `if let`.
- Array indexing needs an i64 index → `arr[i as i64]`.
- A `String` (Named alias) parameter breaks string-method dispatch (`.trim()`); use lowercase `string` primitive where a method body calls string methods on a parameter.
- Array literals in expression position build a DynArray; the `[String]` return bridge is shown in Task 4.

## File structure

- `stdlib/runtime/runtime.cpp` / `.h` — DOM node model, parser, serializer, all `liva_json_*` intrinsics (old ones removed in Task 10).
- `src/IR/IRGen.cpp` — `createRuntimeDecls()` entries.
- `src/IR/IRGenCall.cpp` — intrinsic lowering.
- `src/IR/IRGenExpr.cpp` — struct subscript branch (Task 9).
- `src/Sema/TypeChecker.cpp` — builtin names + return types; subscript return-type resolution (Task 9).
- `src/Sema/ModuleLoader.cpp` — `std::json` builtin surface.
- `stdlib/json/json.liva` — `Json`, `JsonValue`, `JsonObject`, `JsonArray`, `JsonKind`.
- `tests/unit/RuntimeExecTest.cpp` — behavioral round-trip tests.
- `tests/unit/StdlibModuleTest.cpp` — type-check tests (old JSON tests replaced in Task 1).
- `docs/{tr,en}/API-REFERENCE.md` — JSON section (Task 11).

---

## Task 1: Runtime DOM core + parse/free/kind/as*/serialize + JsonValue/Json wrapper

Foundation: validates the whole pipeline (DOM, handle model, Drop, all 6 binding layers, serialization round-trip) on the smallest surface before scaling.

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (add DOM + natives), `stdlib/runtime/runtime.h` (declare natives)
- Modify: `src/IR/IRGen.cpp` (`createRuntimeDecls`, ~line 988+)
- Modify: `src/IR/IRGenCall.cpp` (lowering, near existing json block ~line 4045)
- Modify: `src/Sema/TypeChecker.cpp` (~line 129 list, ~line 2522 switch)
- Modify: `src/Sema/ModuleLoader.cpp` (~line 94)
- Replace: `stdlib/json/json.liva` (whole file)
- Modify: `tests/unit/StdlibModuleTest.cpp` (replace old JSON tests at lines 53–94, 985–994)
- Test: `tests/unit/RuntimeExecTest.cpp` (new behavioral test)

- [ ] **Step 1: Add the DOM model + parser + serializer to `runtime.cpp`**

Add near the existing JSON helpers (after `json_extract_string`, ~line 4139). This is self-contained; it does NOT touch the old `liva_json_*` functions (removed in Task 10).

```cpp
// ===== JSON DOM (parse-tree) =====
namespace livajson {

enum Kind { K_Null=0, K_Bool=1, K_Int=2, K_Double=3, K_String=4, K_Array=5, K_Object=6 };

struct Node {
    int kind = K_Null;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    std::string str;
    std::vector<Node*> arr;
    std::vector<std::pair<std::string, Node*>> obj;
};

struct Doc {
    Node* root = nullptr;
    std::vector<Node*> arena;
    Node* make(int kind) { Node* n = new Node(); n->kind = kind; arena.push_back(n); return n; }
    ~Doc() { for (Node* n : arena) delete n; }
};

// --- Parser (strict JSON) ---
struct Parser {
    const char* p;
    Doc* doc;
    bool ok = true;
    void ws() { while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++; }
    Node* parseValue() {
        ws();
        switch (*p) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { Node* n = doc->make(K_String); n->str = parseString(); return n; }
            case 't': if (!strncmp(p,"true",4)) { p+=4; Node* n=doc->make(K_Bool); n->b=true; return n; } ok=false; return nullptr;
            case 'f': if (!strncmp(p,"false",5)) { p+=5; Node* n=doc->make(K_Bool); n->b=false; return n; } ok=false; return nullptr;
            case 'n': if (!strncmp(p,"null",4)) { p+=4; return doc->make(K_Null); } ok=false; return nullptr;
            default: return parseNumber();
        }
    }
    std::string parseString() {
        std::string out;
        if (*p != '"') { ok=false; return out; }
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') {
                p++;
                switch (*p) {
                    case '"': out+='"'; break;  case '\\': out+='\\'; break; case '/': out+='/'; break;
                    case 'n': out+='\n'; break; case 'r': out+='\r'; break; case 't': out+='\t'; break;
                    case 'b': out+='\b'; break; case 'f': out+='\f'; break;
                    case 'u': { // \uXXXX -> UTF-8
                        unsigned cp = 0; for (int k=0;k<4 && p[1];k++){ p++; char c=*p; cp<<=4;
                            if(c>='0'&&c<='9')cp|=c-'0'; else if(c>='a'&&c<='f')cp|=c-'a'+10; else if(c>='A'&&c<='F')cp|=c-'A'+10; }
                        if (cp<0x80) out+=(char)cp;
                        else if (cp<0x800){ out+=(char)(0xC0|(cp>>6)); out+=(char)(0x80|(cp&0x3F)); }
                        else { out+=(char)(0xE0|(cp>>12)); out+=(char)(0x80|((cp>>6)&0x3F)); out+=(char)(0x80|(cp&0x3F)); }
                        break;
                    }
                    default: out+=*p; break;
                }
                p++;
            } else { out += *p++; }
        }
        if (*p == '"') p++; else ok=false;
        return out;
    }
    Node* parseNumber() {
        const char* start = p;
        if (*p=='-') p++;
        while (*p>='0'&&*p<='9') p++;
        bool isDouble = false;
        if (*p=='.') { isDouble=true; p++; while(*p>='0'&&*p<='9')p++; }
        if (*p=='e'||*p=='E') { isDouble=true; p++; if(*p=='+'||*p=='-')p++; while(*p>='0'&&*p<='9')p++; }
        if (p==start) { ok=false; return nullptr; }
        std::string num(start, p);
        if (isDouble) { Node* n=doc->make(K_Double); n->d=strtod(num.c_str(),nullptr); return n; }
        Node* n=doc->make(K_Int); n->i=strtoll(num.c_str(),nullptr,10); return n;
    }
    Node* parseArray() {
        Node* n = doc->make(K_Array); p++; ws();
        if (*p==']') { p++; return n; }
        while (ok) {
            Node* v = parseValue(); if(!ok) break; n->arr.push_back(v); ws();
            if (*p==',') { p++; continue; }
            if (*p==']') { p++; break; }
            ok=false;
        }
        return n;
    }
    Node* parseObject() {
        Node* n = doc->make(K_Object); p++; ws();
        if (*p=='}') { p++; return n; }
        while (ok) {
            ws(); if (*p!='"'){ ok=false; break; }
            std::string key = parseString(); ws();
            if (*p!=':'){ ok=false; break; } p++;
            Node* v = parseValue(); if(!ok) break;
            n->obj.push_back({key, v}); ws();
            if (*p==',') { p++; continue; }
            if (*p=='}') { p++; break; }
            ok=false;
        }
        return n;
    }
};

// --- Serializer ---
static void escape(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) switch (c) {
        case '"': out+="\\\""; break; case '\\': out+="\\\\"; break;
        case '\b': out+="\\b"; break; case '\f': out+="\\f"; break;
        case '\n': out+="\\n"; break; case '\r': out+="\\r"; break; case '\t': out+="\\t"; break;
        default: out += c;
    }
    out += '"';
}
static void numToStr(std::string& out, Node* n) {
    char buf[64];
    if (n->kind==K_Int) snprintf(buf,sizeof(buf),"%lld",(long long)n->i);
    else snprintf(buf,sizeof(buf),"%.17g",n->d);
    out += buf;
}
static void serialize(std::string& out, Node* n, int indent, int depth) {
    auto nl = [&](int d){ if(indent>0){ out+='\n'; for(int k=0;k<d*indent;k++) out+=' '; } };
    if (!n) { out += "null"; return; }
    switch (n->kind) {
        case K_Null: out += "null"; break;
        case K_Bool: out += n->b ? "true" : "false"; break;
        case K_Int: case K_Double: numToStr(out, n); break;
        case K_String: escape(out, n->str); break;
        case K_Array:
            if (n->arr.empty()) { out += "[]"; break; }
            out += '['; for (size_t k=0;k<n->arr.size();k++){ if(k) out+=','; nl(depth+1); serialize(out,n->arr[k],indent,depth+1);} nl(depth); out += ']';
            break;
        case K_Object:
            if (n->obj.empty()) { out += "{}"; break; }
            out += '{'; for (size_t k=0;k<n->obj.size();k++){ if(k) out+=','; nl(depth+1); escape(out,n->obj[k].first); out+=indent>0?": ":":"; serialize(out,n->obj[k].second,indent,depth+1);} nl(depth); out += '}';
            break;
    }
}

inline Doc* asDoc(int64_t h) { return reinterpret_cast<Doc*>(h); }
inline Node* asNode(int64_t h) { return reinterpret_cast<Node*>(h); }

} // namespace livajson
```

- [ ] **Step 2: Add the core natives to `runtime.cpp`** (after the namespace block)

```cpp
extern "C" {

int64_t liva_json_parse(const char* s) {
    using namespace livajson;
    if (!s) return 0;
    Doc* doc = new Doc();
    Parser ps{ s, doc, true };
    Node* root = ps.parseValue();
    ps.ws();
    if (!ps.ok || *ps.p != '\0' || !root) { delete doc; return 0; }
    doc->root = root;
    return reinterpret_cast<int64_t>(doc);
}

void liva_json_free_doc(int64_t docH) {
    using namespace livajson;
    if (docH) delete asDoc(docH);
}

int64_t liva_json_root(int64_t docH) {
    using namespace livajson;
    if (!docH) return 0;
    return reinterpret_cast<int64_t>(asDoc(docH)->root);
}

int32_t liva_json_node_kind(int64_t nodeH) {
    using namespace livajson;
    if (!nodeH) return K_Null;
    return asNode(nodeH)->kind;
}

int64_t liva_json_node_as_int(int64_t nodeH) {
    using namespace livajson;
    if (!nodeH) return 0;
    Node* n = asNode(nodeH);
    if (n->kind==K_Int) return n->i;
    if (n->kind==K_Double) return (int64_t)n->d;
    if (n->kind==K_Bool) return n->b ? 1 : 0;
    if (n->kind==K_String) return strtoll(n->str.c_str(),nullptr,10);
    return 0;
}

double liva_json_node_as_float(int64_t nodeH) {
    using namespace livajson;
    if (!nodeH) return 0.0;
    Node* n = asNode(nodeH);
    if (n->kind==K_Double) return n->d;
    if (n->kind==K_Int) return (double)n->i;
    if (n->kind==K_String) return strtod(n->str.c_str(),nullptr);
    return 0.0;
}

int8_t liva_json_node_as_bool(int64_t nodeH) {
    using namespace livajson;
    if (!nodeH) return 0;
    Node* n = asNode(nodeH);
    if (n->kind==K_Bool) return n->b ? 1 : 0;
    if (n->kind==K_Int) return n->i != 0;
    if (n->kind==K_String) return (n->str=="true"||n->str=="1") ? 1 : 0;
    return 0;
}

char* liva_json_node_as_string(int64_t nodeH) {
    using namespace livajson;
    if (!nodeH) return strdup_safe("");
    Node* n = asNode(nodeH);
    if (n->kind==K_String) return strdup_safe(n->str.c_str());
    // Non-string: serialize compactly (numbers/bools render naturally)
    std::string out; serialize(out, n, 0, 0);
    return strdup_safe(out.c_str());
}

char* liva_json_to_string(int64_t nodeH) {
    using namespace livajson;
    std::string out; serialize(out, asNode(nodeH), 0, 0);
    return strdup_safe(out.c_str());
}

char* liva_json_to_string_pretty(int64_t nodeH, int32_t indent) {
    using namespace livajson;
    if (indent<0) indent=2; if (indent>16) indent=16;
    std::string out; serialize(out, asNode(nodeH), indent, 0);
    return strdup_safe(out.c_str());
}

} // extern "C"
```

- [ ] **Step 3: Declare the natives in `runtime.h`** (in the `extern "C"` block with other `liva_json_*` decls)

```cpp
int64_t liva_json_parse(const char* s);
void    liva_json_free_doc(int64_t docH);
int64_t liva_json_root(int64_t docH);
int32_t liva_json_node_kind(int64_t nodeH);
int64_t liva_json_node_as_int(int64_t nodeH);
double  liva_json_node_as_float(int64_t nodeH);
int8_t  liva_json_node_as_bool(int64_t nodeH);
char*   liva_json_node_as_string(int64_t nodeH);
char*   liva_json_to_string(int64_t nodeH);
char*   liva_json_to_string_pretty(int64_t nodeH, int32_t indent);
```

- [ ] **Step 4: Register the natives in `createRuntimeDecls()` (`src/IR/IRGen.cpp`, after the existing json block ~line 1000)**

```cpp
auto *ptrTy = llvm::PointerType::getUnqual(*context_);
auto *i64Ty = builder_->getInt64Ty();
auto *i32Ty = builder_->getInt32Ty();
auto *i8Ty  = builder_->getInt8Ty();
auto *f64Ty = llvm::Type::getDoubleTy(*context_);
auto *voidTy = llvm::Type::getVoidTy(*context_);
// parse: (ptr) -> i64
module_->getOrInsertFunction("liva_json_parse", llvm::FunctionType::get(i64Ty, {ptrTy}, false));
// free_doc: (i64) -> void
module_->getOrInsertFunction("liva_json_free_doc", llvm::FunctionType::get(voidTy, {i64Ty}, false));
// root: (i64) -> i64
module_->getOrInsertFunction("liva_json_root", llvm::FunctionType::get(i64Ty, {i64Ty}, false));
// node_kind: (i64) -> i32
module_->getOrInsertFunction("liva_json_node_kind", llvm::FunctionType::get(i32Ty, {i64Ty}, false));
// node_as_int: (i64) -> i64
module_->getOrInsertFunction("liva_json_node_as_int", llvm::FunctionType::get(i64Ty, {i64Ty}, false));
// node_as_float: (i64) -> f64
module_->getOrInsertFunction("liva_json_node_as_float", llvm::FunctionType::get(f64Ty, {i64Ty}, false));
// node_as_bool: (i64) -> i8
module_->getOrInsertFunction("liva_json_node_as_bool", llvm::FunctionType::get(i8Ty, {i64Ty}, false));
// node_as_string: (i64) -> ptr
module_->getOrInsertFunction("liva_json_node_as_string", llvm::FunctionType::get(ptrTy, {i64Ty}, false));
// to_string: (i64) -> ptr
module_->getOrInsertFunction("liva_json_to_string", llvm::FunctionType::get(ptrTy, {i64Ty}, false));
// to_string_pretty: (i64, i32) -> ptr
module_->getOrInsertFunction("liva_json_to_string_pretty", llvm::FunctionType::get(ptrTy, {i64Ty, i32Ty}, false));
```

> Note: if `ptrTy`/`i64Ty` etc. are already declared earlier in `createRuntimeDecls`, reuse them instead of re-declaring (avoid shadowing compile errors). Inspect the surrounding lines first.

- [ ] **Step 5: Add lowering in `src/IR/IRGenCall.cpp`** (place beside the existing json block, ~line 4045; the old `jsonGet`… blocks stay until Task 10)

```cpp
if (funcName == "jsonParse" && node->getArgs().size() >= 1) {
    auto *sArg = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_parse"), {sArg}, "json.parse");
}
if (funcName == "jsonFreeDoc" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_free_doc"), {h});
}
if (funcName == "jsonRoot" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_root"), {h}, "json.root");
}
if (funcName == "jsonNodeKind" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_node_kind"), {h}, "json.kind");
}
if (funcName == "jsonNodeAsInt" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_node_as_int"), {h}, "json.asint");
}
if (funcName == "jsonNodeAsFloat" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_node_as_float"), {h}, "json.asfloat");
}
if (funcName == "jsonNodeAsBool" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    auto *r = builder_->CreateCall(getOrPanic("liva_json_node_as_bool"), {h}, "json.asbool");
    return builder_->CreateTrunc(r, builder_->getInt1Ty(), "json.asbool.bool");
}
if (funcName == "jsonNodeAsString" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_node_as_string"), {h}, "json.asstr");
}
if (funcName == "jsonToString" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_to_string"), {h}, "json.tostr");
}
if (funcName == "jsonToStringPretty" && node->getArgs().size() >= 2) {
    auto *h = visit(node->getArgs()[0].get());
    auto *ind = visit(node->getArgs()[1].get());
    return builder_->CreateCall(getOrPanic("liva_json_to_string_pretty"), {h, ind}, "json.tostrp");
}
```

- [ ] **Step 6: Add builtin names + return types in `src/Sema/TypeChecker.cpp`**

At the name loop (~line 129), add to the brace-init list:
```cpp
"jsonParse", "jsonFreeDoc", "jsonRoot", "jsonNodeKind",
"jsonNodeAsInt", "jsonNodeAsFloat", "jsonNodeAsBool", "jsonNodeAsString",
"jsonToString", "jsonToStringPretty",
```
At the return-type switch (~line 2522), add cases:
```cpp
} else if (ident->getName() == "jsonParse" || ident->getName() == "jsonRoot" ||
           ident->getName() == "jsonNodeAsInt") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::I64));
} else if (ident->getName() == "jsonNodeKind") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::I32));
} else if (ident->getName() == "jsonNodeAsFloat") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::F64));
} else if (ident->getName() == "jsonNodeAsBool") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::Bool));
} else if (ident->getName() == "jsonNodeAsString" || ident->getName() == "jsonToString" ||
           ident->getName() == "jsonToStringPretty") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::String));
} else if (ident->getName() == "jsonFreeDoc") {
    ident->setResolvedType(makeVoidType());
```
> Match the exact `makePrimitiveType`/`makeVoidType` helpers and `TypeRepr::Kind` enum names already used in this switch (inspect neighbors). `jsonFreeDoc` returns void.

- [ ] **Step 7: Add names to `std::json` in `src/Sema/ModuleLoader.cpp`** (~line 94 list)

```cpp
"jsonParse", "jsonFreeDoc", "jsonRoot", "jsonNodeKind",
"jsonNodeAsInt", "jsonNodeAsFloat", "jsonNodeAsBool", "jsonNodeAsString",
"jsonToString", "jsonToStringPretty",
```
(Keep the old `jsonGet`… names for now; removed in Task 10.)

- [ ] **Step 8: Replace `stdlib/json/json.liva` with the new foundation**

```liva
// json::json — Parse-tree JSON (JsonDataObjects-style)
// Import with: import json::json
//
// Ownership: the value returned by Json.parse/object/array is the SOLE owner of
// the document; bind it to a let/var. Everything reached from it (obj["k"],
// arr[i], .object(), typed getters returning JsonObject/JsonArray) is a borrow
// valid only while the owner is alive. Do NOT chain off a parse temporary
// (e.g. Json.parse(s).object()) — the temporary owner is never dropped -> leak.

import std::json

pub enum JsonKind { Null, Bool, Int, Double, Str, Arr, Obj }

pub struct JsonValue {
    var docHandle: i64
    var nodeHandle: i64
    var owns: bool
}

impl JsonValue: Drop {
    func drop(mut self) {
        if self.owns { jsonFreeDoc(self.docHandle) }
    }
}

impl JsonValue {
    pub func kind(ref self) -> JsonKind {
        let k = jsonNodeKind(self.nodeHandle)
        if k == 1 { return JsonKind.Bool }
        if k == 2 { return JsonKind.Int }
        if k == 3 { return JsonKind.Double }
        if k == 4 { return JsonKind.Str }
        if k == 5 { return JsonKind.Arr }
        if k == 6 { return JsonKind.Obj }
        return JsonKind.Null
    }
    pub func isNull(ref self) -> bool { return jsonNodeKind(self.nodeHandle) == 0 }
    pub func isBool(ref self) -> bool { return jsonNodeKind(self.nodeHandle) == 1 }
    pub func isInt(ref self) -> bool { return jsonNodeKind(self.nodeHandle) == 2 }
    pub func isDouble(ref self) -> bool { return jsonNodeKind(self.nodeHandle) == 3 }
    pub func isString(ref self) -> bool { return jsonNodeKind(self.nodeHandle) == 4 }
    pub func isArray(ref self) -> bool { return jsonNodeKind(self.nodeHandle) == 5 }
    pub func isObject(ref self) -> bool { return jsonNodeKind(self.nodeHandle) == 6 }

    pub func asString(ref self) -> String { return jsonNodeAsString(self.nodeHandle) }
    pub func asInt(ref self) -> i64 { return jsonNodeAsInt(self.nodeHandle) }
    pub func asFloat(ref self) -> f64 { return jsonNodeAsFloat(self.nodeHandle) }
    pub func asBool(ref self) -> bool { return jsonNodeAsBool(self.nodeHandle) }

    pub func toString(ref self) -> String { return jsonToString(self.nodeHandle) }
    pub func toStringPretty(ref self, indent: i32) -> String {
        return jsonToStringPretty(self.nodeHandle, indent)
    }
}

pub struct Json {}

impl Json {
    pub func parse(s: String) -> JsonValue {
        let doc = jsonParse(s)
        return JsonValue { docHandle: doc, nodeHandle: jsonRoot(doc), owns: true }
    }
}
```

> `JsonObject`/`JsonArray` and `Json.object()/array()` are added in Tasks 2/3/6. `JsonValue.object()/array()` come in Task 2 (they need `JsonObject`/`JsonArray` to exist).

- [ ] **Step 9: Replace the old JSON tests in `tests/unit/StdlibModuleTest.cpp`**

Replace the three old tests at lines 53–94 and the one at 985–994 with new-API type-check tests:
```cpp
TEST_F(StdlibModuleTest, ImportJsonModule) {
    auto r = check(
        "import json::json\n"
        "func main() {\n"
        "    let doc = Json.parse(\"{\\\"a\\\":1}\")\n"
        "    let k = doc.kind()\n"
        "}\n", true, "stdlib");
    EXPECT_TRUE(r.passed) << "import json::json should resolve Json/JsonValue";
}

TEST_F(StdlibModuleTest, JsonValueAccessors) {
    auto r = check(
        "import json::json\n"
        "func main() {\n"
        "    let doc = Json.parse(\"42\")\n"
        "    let i: i64 = doc.asInt()\n"
        "    let f: f64 = doc.asFloat()\n"
        "    let s: String = doc.toString()\n"
        "    let b: bool = doc.isInt()\n"
        "}\n", true, "stdlib");
    EXPECT_TRUE(r.passed) << "JsonValue accessors should type-check";
}
```
> Use the exact `check(...)` signature already used in this file (3rd arg is the stdlib mode string — copy a neighboring call). Delete the obsolete `JsonObjectMethods`, `JsonObjectNumericMethods`, `JsonStringifyPretty` tests.

- [ ] **Step 10: Write the failing behavioral test in `tests/unit/RuntimeExecTest.cpp`**

Add near other module tests (copy the `compileAndRun` harness pattern from a neighboring test):
```cpp
TEST_F(RuntimeExecTest, JsonParseScalarAndSerialize) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"n\":7,\"name\":\"liva\",\"ok\":true}")
    println(doc.toString())
    let pretty = doc.toStringPretty(2)
    println(pretty)
    if doc.isObject() { println("isobj") }
}
)LIVA";
    auto out = compileAndRun("json_scalar.liva", src);
    EXPECT_NE(out.find("\"n\":7"), std::string::npos) << out;
    EXPECT_NE(out.find("\"name\":\"liva\""), std::string::npos) << out;
    EXPECT_NE(out.find("isobj"), std::string::npos) << out;
}
```
> Copy the exact `compileAndRun` helper name/signature from an existing RuntimeExecTest case.

- [ ] **Step 11: Build, then run the new test to verify it fails (before) / passes (after)**

Run: `build_clang.bat`
Run: `ctest --test-dir build-clang -R "JsonParseScalarAndSerialize|JsonValueAccessors|ImportJsonModule" --output-on-failure`
Expected: PASS for all three.

- [ ] **Step 12: Run the FULL serial suite**

Run: `ctest --test-dir build-clang --output-on-failure`
Expected: all green (the typed-DB baseline was 2334; this replaces ~3 JSON type-check tests with 2 new ones + adds 1 runtime test).

- [ ] **Step 13: Commit**

```bash
git add -A
git commit -m "json: parse-tree DOM core + JsonValue/Json wrapper (parse/kind/as*/serialize)"
```

---

## Task 2: Object read navigation — JsonObject getters + JsonValue.object()/array()

**Files:** `runtime.cpp`/`.h`, `IRGen.cpp`, `IRGenCall.cpp`, `TypeChecker.cpp`, `ModuleLoader.cpp`, `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Add natives in `runtime.cpp`**

```cpp
extern "C" {
int64_t liva_json_obj_get(int64_t nodeH, const char* key) {
    using namespace livajson;
    if (!nodeH || !key) return 0;
    Node* n = asNode(nodeH);
    if (n->kind != K_Object) return 0;
    for (auto& kv : n->obj) if (kv.first == key) return reinterpret_cast<int64_t>(kv.second);
    return 0;
}
int8_t liva_json_obj_has(int64_t nodeH, const char* key) {
    return liva_json_obj_get(nodeH, key) != 0 ? 1 : 0;
}
int32_t liva_json_obj_count(int64_t nodeH) {
    using namespace livajson;
    if (!nodeH) return 0;
    Node* n = asNode(nodeH);
    return n->kind==K_Object ? (int32_t)n->obj.size() : 0;
}
} // extern "C"
```

- [ ] **Step 2: Declare in `runtime.h`**
```cpp
int64_t liva_json_obj_get(int64_t nodeH, const char* key);
int8_t  liva_json_obj_has(int64_t nodeH, const char* key);
int32_t liva_json_obj_count(int64_t nodeH);
```

- [ ] **Step 3: `createRuntimeDecls()` entries (`IRGen.cpp`)**
```cpp
module_->getOrInsertFunction("liva_json_obj_get", llvm::FunctionType::get(i64Ty, {i64Ty, ptrTy}, false));
module_->getOrInsertFunction("liva_json_obj_has", llvm::FunctionType::get(i8Ty, {i64Ty, ptrTy}, false));
module_->getOrInsertFunction("liva_json_obj_count", llvm::FunctionType::get(i32Ty, {i64Ty}, false));
```

- [ ] **Step 4: Lowering (`IRGenCall.cpp`)**
```cpp
if (funcName == "jsonObjGet" && node->getArgs().size() >= 2) {
    auto *h = visit(node->getArgs()[0].get());
    auto *key = visit(node->getArgs()[1].get());
    return builder_->CreateCall(getOrPanic("liva_json_obj_get"), {h, key}, "json.objget");
}
if (funcName == "jsonObjHas" && node->getArgs().size() >= 2) {
    auto *h = visit(node->getArgs()[0].get());
    auto *key = visit(node->getArgs()[1].get());
    auto *r = builder_->CreateCall(getOrPanic("liva_json_obj_has"), {h, key}, "json.objhas");
    return builder_->CreateTrunc(r, builder_->getInt1Ty(), "json.objhas.bool");
}
if (funcName == "jsonObjCount" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_obj_count"), {h}, "json.objcount");
}
```

- [ ] **Step 5: TypeChecker names + return types**
Add to name loop: `"jsonObjGet", "jsonObjHas", "jsonObjCount",`
Add cases:
```cpp
} else if (ident->getName() == "jsonObjGet") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::I64));
} else if (ident->getName() == "jsonObjHas") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::Bool));
} else if (ident->getName() == "jsonObjCount") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::I32));
```

- [ ] **Step 6: ModuleLoader names** — add `"jsonObjGet", "jsonObjHas", "jsonObjCount",`

- [ ] **Step 7: Add `JsonObject` + `JsonValue.object()/array()` to `json.liva`**

Add the `JsonObject` struct (with Drop) and a forward placeholder for `JsonArray` (full impl in Task 3 — but `JsonObject.getArray` returns `JsonArray`, so define `JsonArray` struct + Drop here too; its read methods land in Task 3).

```liva
pub struct JsonObject {
    var docHandle: i64
    var nodeHandle: i64
    var owns: bool
}
impl JsonObject: Drop {
    func drop(mut self) { if self.owns { jsonFreeDoc(self.docHandle) } }
}

pub struct JsonArray {
    var docHandle: i64
    var nodeHandle: i64
    var owns: bool
}
impl JsonArray: Drop {
    func drop(mut self) { if self.owns { jsonFreeDoc(self.docHandle) } }
}

impl JsonObject {
    pub func has(ref self, key: String) -> bool { return jsonObjHas(self.nodeHandle, key) }
    pub func count(ref self) -> i32 { return jsonObjCount(self.nodeHandle) }

    pub func get(ref self, key: String) -> JsonValue {
        return JsonValue { docHandle: self.docHandle, nodeHandle: jsonObjGet(self.nodeHandle, key), owns: false }
    }
    pub func getString(ref self, key: String) -> String {
        return jsonNodeAsString(jsonObjGet(self.nodeHandle, key))
    }
    pub func getInt(ref self, key: String) -> i64 {
        return jsonNodeAsInt(jsonObjGet(self.nodeHandle, key))
    }
    pub func getFloat(ref self, key: String) -> f64 {
        return jsonNodeAsFloat(jsonObjGet(self.nodeHandle, key))
    }
    pub func getBool(ref self, key: String) -> bool {
        return jsonNodeAsBool(jsonObjGet(self.nodeHandle, key))
    }
    pub func getObject(ref self, key: String) -> JsonObject {
        return JsonObject { docHandle: self.docHandle, nodeHandle: jsonObjGet(self.nodeHandle, key), owns: false }
    }
    pub func getArray(ref self, key: String) -> JsonArray {
        return JsonArray { docHandle: self.docHandle, nodeHandle: jsonObjGet(self.nodeHandle, key), owns: false }
    }
    pub func toString(ref self) -> String { return jsonToString(self.nodeHandle) }
    pub func toStringPretty(ref self, indent: i32) -> String { return jsonToStringPretty(self.nodeHandle, indent) }
}
```
Add to `impl JsonValue`:
```liva
    pub func object(ref self) -> JsonObject {
        return JsonObject { docHandle: self.docHandle, nodeHandle: self.nodeHandle, owns: false }
    }
    pub func array(ref self) -> JsonArray {
        return JsonArray { docHandle: self.docHandle, nodeHandle: self.nodeHandle, owns: false }
    }
```

- [ ] **Step 8: Behavioral test (`RuntimeExecTest.cpp`)**
```cpp
TEST_F(RuntimeExecTest, JsonObjectTypedRead) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"name\":\"liva\",\"age\":3,\"pi\":3.5,\"ok\":true,\"addr\":{\"city\":\"izmir\"}}")
    let o = doc.object()
    println(o.getString("name"))
    println(o.getInt("age"))
    println(o.getFloat("pi"))
    if o.getBool("ok") { println("ok-true") }
    if o.has("age") { println("has-age") }
    println(o.count())
    let addr = o.getObject("addr")
    println(addr.getString("city"))
}
)LIVA";
    auto out = compileAndRun("json_objread.liva", src);
    EXPECT_NE(out.find("liva"), std::string::npos) << out;
    EXPECT_NE(out.find("ok-true"), std::string::npos) << out;
    EXPECT_NE(out.find("izmir"), std::string::npos) << out;
}
```

- [ ] **Step 9: Build + run test**
Run: `build_clang.bat && ctest --test-dir build-clang -R "JsonObjectTypedRead" --output-on-failure`
Expected: PASS.

- [ ] **Step 10: Full serial suite green**
Run: `ctest --test-dir build-clang --output-on-failure`

- [ ] **Step 11: Commit**
```bash
git add -A && git commit -m "json: object navigation + JsonObject typed getters + JsonValue.object/array"
```

---

## Task 3: Array read — JsonArray getters + at()

**Files:** `runtime.cpp`/`.h`, `IRGen.cpp`, `IRGenCall.cpp`, `TypeChecker.cpp`, `ModuleLoader.cpp`, `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Natives (`runtime.cpp`)**
```cpp
extern "C" {
int32_t liva_json_arr_count(int64_t nodeH) {
    using namespace livajson;
    if (!nodeH) return 0;
    Node* n = asNode(nodeH);
    return n->kind==K_Array ? (int32_t)n->arr.size() : 0;
}
int64_t liva_json_arr_at(int64_t nodeH, int64_t idx) {
    using namespace livajson;
    if (!nodeH) return 0;
    Node* n = asNode(nodeH);
    if (n->kind != K_Array || idx < 0 || (size_t)idx >= n->arr.size()) return 0;
    return reinterpret_cast<int64_t>(n->arr[idx]);
}
} // extern "C"
```

- [ ] **Step 2: `runtime.h`**
```cpp
int32_t liva_json_arr_count(int64_t nodeH);
int64_t liva_json_arr_at(int64_t nodeH, int64_t idx);
```

- [ ] **Step 3: `createRuntimeDecls()`**
```cpp
module_->getOrInsertFunction("liva_json_arr_count", llvm::FunctionType::get(i32Ty, {i64Ty}, false));
module_->getOrInsertFunction("liva_json_arr_at", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false));
```

- [ ] **Step 4: Lowering (`IRGenCall.cpp`)**
```cpp
if (funcName == "jsonArrCount" && node->getArgs().size() >= 1) {
    auto *h = visit(node->getArgs()[0].get());
    return builder_->CreateCall(getOrPanic("liva_json_arr_count"), {h}, "json.arrcount");
}
if (funcName == "jsonArrAt" && node->getArgs().size() >= 2) {
    auto *h = visit(node->getArgs()[0].get());
    auto *idx = visit(node->getArgs()[1].get());
    return builder_->CreateCall(getOrPanic("liva_json_arr_at"), {h, idx}, "json.arrat");
}
```

- [ ] **Step 5: TypeChecker** — names `"jsonArrCount", "jsonArrAt",`; cases:
```cpp
} else if (ident->getName() == "jsonArrAt") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::I64));
} else if (ident->getName() == "jsonArrCount") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::I32));
```

- [ ] **Step 6: ModuleLoader** — add `"jsonArrCount", "jsonArrAt",`

- [ ] **Step 7: `JsonArray` read methods (`json.liva`)** — add to `impl JsonArray`:
```liva
    pub func count(ref self) -> i32 { return jsonArrCount(self.nodeHandle) }
    pub func length(ref self) -> i32 { return jsonArrCount(self.nodeHandle) }
    pub func at(ref self, i: i64) -> JsonValue {
        return JsonValue { docHandle: self.docHandle, nodeHandle: jsonArrAt(self.nodeHandle, i), owns: false }
    }
    pub func getString(ref self, i: i64) -> String { return jsonNodeAsString(jsonArrAt(self.nodeHandle, i)) }
    pub func getInt(ref self, i: i64) -> i64 { return jsonNodeAsInt(jsonArrAt(self.nodeHandle, i)) }
    pub func getFloat(ref self, i: i64) -> f64 { return jsonNodeAsFloat(jsonArrAt(self.nodeHandle, i)) }
    pub func getBool(ref self, i: i64) -> bool { return jsonNodeAsBool(jsonArrAt(self.nodeHandle, i)) }
    pub func getObject(ref self, i: i64) -> JsonObject {
        return JsonObject { docHandle: self.docHandle, nodeHandle: jsonArrAt(self.nodeHandle, i), owns: false }
    }
    pub func getArray(ref self, i: i64) -> JsonArray {
        return JsonArray { docHandle: self.docHandle, nodeHandle: jsonArrAt(self.nodeHandle, i), owns: false }
    }
    pub func toString(ref self) -> String { return jsonToString(self.nodeHandle) }
    pub func toStringPretty(ref self, indent: i32) -> String { return jsonToStringPretty(self.nodeHandle, indent) }
```

- [ ] **Step 8: Behavioral test (`RuntimeExecTest.cpp`)**
```cpp
TEST_F(RuntimeExecTest, JsonArrayRead) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"nums\":[10,20,30],\"objs\":[{\"x\":1},{\"x\":2}]}")
    let o = doc.object()
    let nums = o.getArray("nums")
    println(nums.count())
    println(nums.getInt(0 as i64))
    println(nums.getInt(2 as i64))
    let objs = o.getArray("objs")
    println(objs.getObject(1 as i64).getInt("x"))
}
)LIVA";
    auto out = compileAndRun("json_arrread.liva", src);
    EXPECT_NE(out.find("10"), std::string::npos) << out;
    EXPECT_NE(out.find("30"), std::string::npos) << out;
    EXPECT_NE(out.find("2"), std::string::npos) << out;
}
```

- [ ] **Step 9: Build + run** — `build_clang.bat && ctest --test-dir build-clang -R "JsonArrayRead" --output-on-failure` → PASS
- [ ] **Step 10: Full serial suite green** — `ctest --test-dir build-clang --output-on-failure`
- [ ] **Step 11: Commit** — `git add -A && git commit -m "json: array navigation + JsonArray typed getters"`

---

## Task 4: keys() — `[String]` via DynArray bridge

**Files:** `runtime.cpp`/`.h`, `IRGen.cpp`, `IRGenCall.cpp`, `TypeChecker.cpp`, `ModuleLoader.cpp`, `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Native (`runtime.cpp`)** — mirrors the existing `liva_json_keys` but over the DOM:
```cpp
extern "C" {
char** liva_json_obj_keys(int64_t nodeH, int64_t* count) {
    using namespace livajson;
    if (count) *count = 0;
    if (!nodeH || !count) return nullptr;
    Node* n = asNode(nodeH);
    if (n->kind != K_Object || n->obj.empty()) return nullptr;
    *count = (int64_t)n->obj.size();
    char** result = (char**)malloc(n->obj.size() * sizeof(char*));
    if (!result) { *count = 0; return nullptr; }
    for (size_t i = 0; i < n->obj.size(); i++)
        result[i] = strdup_safe(n->obj[i].first.c_str());
    return result;
}
} // extern "C"
```

- [ ] **Step 2: `runtime.h`** — `char** liva_json_obj_keys(int64_t nodeH, int64_t* count);`

- [ ] **Step 3: `createRuntimeDecls()`**
```cpp
module_->getOrInsertFunction("liva_json_obj_keys", llvm::FunctionType::get(ptrTy, {i64Ty, ptrTy}, false));
```

- [ ] **Step 4: Lowering (`IRGenCall.cpp`)** — DynArray bridge (mirror the existing `jsonKeys` block at IRGenCall.cpp ~4101):
```cpp
if (funcName == "jsonObjKeys" && !node->getArgs().empty()) {
    auto *nodeArg = visit(node->getArgs()[0].get());
    if (!nodeArg) return nullptr;
    auto *fn = getOrPanic("liva_json_obj_keys");
    auto *i64Ty = builder_->getInt64Ty();
    auto *curFunc = builder_->GetInsertBlock()->getParent();
    auto *countAlloca = createEntryBlockAlloca(curFunc, "jsonkeys.count", i64Ty);
    builder_->CreateStore(builder_->getInt64(0), countAlloca);
    auto *rawArr = builder_->CreateCall(fn, {nodeArg, countAlloca}, "jsonkeys.raw");
    auto *count = builder_->CreateLoad(i64Ty, countAlloca, "jsonkeys.count");
    auto *daTy = getDynArrayStructTy();
    auto *daAlloca = createEntryBlockAlloca(curFunc, "jsonkeys.da", daTy);
    builder_->CreateStore(rawArr, builder_->CreateStructGEP(daTy, daAlloca, 0));
    builder_->CreateStore(count, builder_->CreateStructGEP(daTy, daAlloca, 1));
    builder_->CreateStore(count, builder_->CreateStructGEP(daTy, daAlloca, 2));
    return builder_->CreateLoad(daTy, daAlloca, "jsonkeys.da.val");
}
```

- [ ] **Step 5: TypeChecker** — add name `"jsonObjKeys",`; return type `[String]`:
```cpp
} else if (ident->getName() == "jsonObjKeys") {
    ident->setResolvedType(makeArrayType(makePrimitiveType(TypeRepr::Kind::String)));
```
> Use the exact array-type constructor used by the old `jsonKeys` case at TypeChecker.cpp ~line 2531 (copy it verbatim).

- [ ] **Step 6: ModuleLoader** — add `"jsonObjKeys",`

- [ ] **Step 7: `json.liva`** — add to `impl JsonObject`:
```liva
    pub func keys(ref self) -> [String] { return jsonObjKeys(self.nodeHandle) }
```

- [ ] **Step 8: Behavioral test**
```cpp
TEST_F(RuntimeExecTest, JsonObjectKeys) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"a\":1,\"b\":2,\"c\":3}")
    let o = doc.object()
    let ks = o.keys()
    for k in ks { println(k) }
}
)LIVA";
    auto out = compileAndRun("json_keys.liva", src);
    EXPECT_NE(out.find("a"), std::string::npos) << out;
    EXPECT_NE(out.find("b"), std::string::npos) << out;
    EXPECT_NE(out.find("c"), std::string::npos) << out;
}
```
> Confirm the `for k in ks` iteration form over `[String]` is the one used elsewhere (copy from an existing DynArray iteration test if syntax differs).

- [ ] **Step 9: Build + run** → PASS
- [ ] **Step 10: Full serial suite green**
- [ ] **Step 11: Commit** — `git add -A && git commit -m "json: JsonObject.keys() -> [String] via DynArray bridge"`

---

## Task 5: Optional typed gets (`try*`) — pure Liva

No new natives. Implemented over `kind()` + `as*`.

**Files:** `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Add to `impl JsonObject` (`json.liva`)**
```liva
    pub func tryString(ref self, key: String) -> String? {
        let h = jsonObjGet(self.nodeHandle, key)
        if jsonNodeKind(h) == 4 { return jsonNodeAsString(h) }
        return nil
    }
    pub func tryInt(ref self, key: String) -> i64? {
        let h = jsonObjGet(self.nodeHandle, key)
        if jsonNodeKind(h) == 2 { return jsonNodeAsInt(h) }
        return nil
    }
    pub func tryFloat(ref self, key: String) -> f64? {
        let h = jsonObjGet(self.nodeHandle, key)
        let k = jsonNodeKind(h)
        if k == 3 { return jsonNodeAsFloat(h) }
        if k == 2 { return jsonNodeAsFloat(h) }
        return nil
    }
    pub func tryBool(ref self, key: String) -> bool? {
        let h = jsonObjGet(self.nodeHandle, key)
        if jsonNodeKind(h) == 1 { return jsonNodeAsBool(h) }
        return nil
    }
```
> `tryFloat` accepts Int (kind 2) as well as Double (kind 3) — JSON `3` is a valid float. Do NOT use `??`; the `if`-return form avoids the IRGen `??` bug.

- [ ] **Step 2: Behavioral test**
```cpp
TEST_F(RuntimeExecTest, JsonTryGetters) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"name\":\"liva\",\"age\":3}")
    let o = doc.object()
    if let n = o.tryString("name") { println(n) }
    if let a = o.tryInt("age") { println(a) }
    if let m = o.tryString("missing") { println(m) } else { println("none") }
    if let w = o.tryInt("name") { println(w) } else { println("wrongkind") }
}
)LIVA";
    auto out = compileAndRun("json_try.liva", src);
    EXPECT_NE(out.find("liva"), std::string::npos) << out;
    EXPECT_NE(out.find("none"), std::string::npos) << out;
    EXPECT_NE(out.find("wrongkind"), std::string::npos) << out;
}
```

- [ ] **Step 3: Build + run** → PASS  (no C++ change, so `build_clang.bat` still required to copy stdlib)
- [ ] **Step 4: Full serial suite green**
- [ ] **Step 5: Commit** — `git add -A && git commit -m "json: optional typed getters (tryString/tryInt/tryFloat/tryBool)"`

---

## Task 6: Building / mutation — new docs + set*/add* + setObject/addObject views

**Files:** `runtime.cpp`/`.h`, `IRGen.cpp`, `IRGenCall.cpp`, `TypeChecker.cpp`, `ModuleLoader.cpp`, `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Natives (`runtime.cpp`)**
```cpp
extern "C" {
int64_t liva_json_new_object() {
    using namespace livajson;
    Doc* doc = new Doc(); doc->root = doc->make(K_Object);
    return reinterpret_cast<int64_t>(doc);
}
int64_t liva_json_new_array() {
    using namespace livajson;
    Doc* doc = new Doc(); doc->root = doc->make(K_Array);
    return reinterpret_cast<int64_t>(doc);
}

// Object set helpers: replace existing key or append. Returns the value node.
static livajson::Node* objSetNode(int64_t docH, int64_t nodeH, const char* key, livajson::Node* v) {
    using namespace livajson;
    if (!docH || !nodeH || !key) return nullptr;
    Node* n = asNode(nodeH);
    if (n->kind != K_Object) return nullptr;
    for (auto& kv : n->obj) if (kv.first == key) { kv.second = v; return v; }
    n->obj.push_back({key, v});
    return v;
}
void liva_json_obj_set_string(int64_t d, int64_t node, const char* key, const char* val) {
    using namespace livajson; Node* v = asDoc(d)->make(K_String); v->str = val ? val : ""; objSetNode(d,node,key,v);
}
void liva_json_obj_set_int(int64_t d, int64_t node, const char* key, int64_t val) {
    using namespace livajson; Node* v = asDoc(d)->make(K_Int); v->i = val; objSetNode(d,node,key,v);
}
void liva_json_obj_set_float(int64_t d, int64_t node, const char* key, double val) {
    using namespace livajson; Node* v = asDoc(d)->make(K_Double); v->d = val; objSetNode(d,node,key,v);
}
void liva_json_obj_set_bool(int64_t d, int64_t node, const char* key, int8_t val) {
    using namespace livajson; Node* v = asDoc(d)->make(K_Bool); v->b = val!=0; objSetNode(d,node,key,v);
}
void liva_json_obj_set_null(int64_t d, int64_t node, const char* key) {
    using namespace livajson; Node* v = asDoc(d)->make(K_Null); objSetNode(d,node,key,v);
}
int64_t liva_json_obj_set_object(int64_t d, int64_t node, const char* key) {
    using namespace livajson; Node* v = asDoc(d)->make(K_Object); objSetNode(d,node,key,v); return reinterpret_cast<int64_t>(v);
}
int64_t liva_json_obj_set_array(int64_t d, int64_t node, const char* key) {
    using namespace livajson; Node* v = asDoc(d)->make(K_Array); objSetNode(d,node,key,v); return reinterpret_cast<int64_t>(v);
}
void liva_json_obj_remove(int64_t node, const char* key) {
    using namespace livajson;
    if (!node || !key) return;
    Node* n = asNode(node);
    if (n->kind != K_Object) return;
    for (auto it = n->obj.begin(); it != n->obj.end(); ++it)
        if (it->first == key) { n->obj.erase(it); return; }
}

// Array add helpers
void liva_json_arr_add_string(int64_t d, int64_t node, const char* val) {
    using namespace livajson; Node* n=asNode(node); if(!n||n->kind!=K_Array) return; Node* v=asDoc(d)->make(K_String); v->str=val?val:""; n->arr.push_back(v);
}
void liva_json_arr_add_int(int64_t d, int64_t node, int64_t val) {
    using namespace livajson; Node* n=asNode(node); if(!n||n->kind!=K_Array) return; Node* v=asDoc(d)->make(K_Int); v->i=val; n->arr.push_back(v);
}
void liva_json_arr_add_float(int64_t d, int64_t node, double val) {
    using namespace livajson; Node* n=asNode(node); if(!n||n->kind!=K_Array) return; Node* v=asDoc(d)->make(K_Double); v->d=val; n->arr.push_back(v);
}
void liva_json_arr_add_bool(int64_t d, int64_t node, int8_t val) {
    using namespace livajson; Node* n=asNode(node); if(!n||n->kind!=K_Array) return; Node* v=asDoc(d)->make(K_Bool); v->b=val!=0; n->arr.push_back(v);
}
void liva_json_arr_add_null(int64_t d, int64_t node) {
    using namespace livajson; Node* n=asNode(node); if(!n||n->kind!=K_Array) return; n->arr.push_back(asDoc(d)->make(K_Null));
}
int64_t liva_json_arr_add_object(int64_t d, int64_t node) {
    using namespace livajson; Node* n=asNode(node); if(!n||n->kind!=K_Array) return 0; Node* v=asDoc(d)->make(K_Object); n->arr.push_back(v); return reinterpret_cast<int64_t>(v);
}
int64_t liva_json_arr_add_array(int64_t d, int64_t node) {
    using namespace livajson; Node* n=asNode(node); if(!n||n->kind!=K_Array) return 0; Node* v=asDoc(d)->make(K_Array); n->arr.push_back(v); return reinterpret_cast<int64_t>(v);
}
} // extern "C"
```

- [ ] **Step 2: `runtime.h`** — declare all 17 functions above.

- [ ] **Step 3: `createRuntimeDecls()`** — register all (types: `new_object/new_array` → `()->i64`; `obj_set_string` → `(i64,i64,ptr,ptr)->void`; `obj_set_int` → `(i64,i64,ptr,i64)->void`; `obj_set_float` → `(i64,i64,ptr,f64)->void`; `obj_set_bool` → `(i64,i64,ptr,i8)->void`; `obj_set_null` → `(i64,i64,ptr)->void`; `obj_set_object/array` → `(i64,i64,ptr)->i64`; `obj_remove` → `(i64,ptr)->void`; `arr_add_string` → `(i64,i64,ptr)->void`; `arr_add_int` → `(i64,i64,i64)->void`; `arr_add_float` → `(i64,i64,f64)->void`; `arr_add_bool` → `(i64,i64,i8)->void`; `arr_add_null` → `(i64,i64)->void`; `arr_add_object/array` → `(i64,i64)->i64`).

```cpp
module_->getOrInsertFunction("liva_json_new_object", llvm::FunctionType::get(i64Ty, {}, false));
module_->getOrInsertFunction("liva_json_new_array", llvm::FunctionType::get(i64Ty, {}, false));
module_->getOrInsertFunction("liva_json_obj_set_string", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, ptrTy}, false));
module_->getOrInsertFunction("liva_json_obj_set_int", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, i64Ty}, false));
module_->getOrInsertFunction("liva_json_obj_set_float", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, f64Ty}, false));
module_->getOrInsertFunction("liva_json_obj_set_bool", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, i8Ty}, false));
module_->getOrInsertFunction("liva_json_obj_set_null", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy}, false));
module_->getOrInsertFunction("liva_json_obj_set_object", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty, ptrTy}, false));
module_->getOrInsertFunction("liva_json_obj_set_array", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty, ptrTy}, false));
module_->getOrInsertFunction("liva_json_obj_remove", llvm::FunctionType::get(voidTy, {i64Ty, ptrTy}, false));
module_->getOrInsertFunction("liva_json_arr_add_string", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy}, false));
module_->getOrInsertFunction("liva_json_arr_add_int", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, i64Ty}, false));
module_->getOrInsertFunction("liva_json_arr_add_float", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, f64Ty}, false));
module_->getOrInsertFunction("liva_json_arr_add_bool", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, i8Ty}, false));
module_->getOrInsertFunction("liva_json_arr_add_null", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty}, false));
module_->getOrInsertFunction("liva_json_arr_add_object", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false));
module_->getOrInsertFunction("liva_json_arr_add_array", llvm::FunctionType::get(i64Ty, {i64Ty, i64Ty}, false));
```

- [ ] **Step 4: Lowering (`IRGenCall.cpp`)** — one block per builtin. Bool args: the Liva `bool` arrives as i1; zero-extend to i8 before the call. Pattern (showing the four representative shapes; replicate for every name):
```cpp
if (funcName == "jsonNewObject" && node->getArgs().empty())
    return builder_->CreateCall(getOrPanic("liva_json_new_object"), {}, "json.newobj");
if (funcName == "jsonNewArray" && node->getArgs().empty())
    return builder_->CreateCall(getOrPanic("liva_json_new_array"), {}, "json.newarr");

if (funcName == "jsonObjSetString" && node->getArgs().size() >= 4) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    auto *k = visit(node->getArgs()[2].get());
    auto *v = visit(node->getArgs()[3].get());
    return builder_->CreateCall(getOrPanic("liva_json_obj_set_string"), {d, n, k, v});
}
if (funcName == "jsonObjSetInt" && node->getArgs().size() >= 4) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    auto *k = visit(node->getArgs()[2].get());
    auto *v = visit(node->getArgs()[3].get());
    return builder_->CreateCall(getOrPanic("liva_json_obj_set_int"), {d, n, k, v});
}
if (funcName == "jsonObjSetFloat" && node->getArgs().size() >= 4) { /* same shape -> liva_json_obj_set_float */ }
if (funcName == "jsonObjSetBool" && node->getArgs().size() >= 4) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    auto *k = visit(node->getArgs()[2].get());
    auto *v = visit(node->getArgs()[3].get());
    v = builder_->CreateZExt(v, builder_->getInt8Ty(), "json.bool.zext");
    return builder_->CreateCall(getOrPanic("liva_json_obj_set_bool"), {d, n, k, v});
}
if (funcName == "jsonObjSetNull" && node->getArgs().size() >= 3) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    auto *k = visit(node->getArgs()[2].get());
    return builder_->CreateCall(getOrPanic("liva_json_obj_set_null"), {d, n, k});
}
if (funcName == "jsonObjSetObject" && node->getArgs().size() >= 3) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    auto *k = visit(node->getArgs()[2].get());
    return builder_->CreateCall(getOrPanic("liva_json_obj_set_object"), {d, n, k}, "json.setobj");
}
if (funcName == "jsonObjSetArray" && node->getArgs().size() >= 3) { /* same -> liva_json_obj_set_array, "json.setarr" */ }
if (funcName == "jsonObjRemove" && node->getArgs().size() >= 2) {
    auto *n = visit(node->getArgs()[0].get());
    auto *k = visit(node->getArgs()[1].get());
    return builder_->CreateCall(getOrPanic("liva_json_obj_remove"), {n, k});
}
if (funcName == "jsonArrAddString" && node->getArgs().size() >= 2) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    auto *v = visit(node->getArgs()[2].get());  // note: 3 args (doc,node,val)
    return builder_->CreateCall(getOrPanic("liva_json_arr_add_string"), {d, n, v});
}
// jsonArrAddInt/Float -> same 3-arg shape. jsonArrAddBool -> ZExt i1->i8 like above.
if (funcName == "jsonArrAddNull" && node->getArgs().size() >= 2) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    return builder_->CreateCall(getOrPanic("liva_json_arr_add_null"), {d, n});
}
if (funcName == "jsonArrAddObject" && node->getArgs().size() >= 2) {
    auto *d = visit(node->getArgs()[0].get());
    auto *n = visit(node->getArgs()[1].get());
    return builder_->CreateCall(getOrPanic("liva_json_arr_add_object"), {d, n}, "json.addobj");
}
// jsonArrAddArray -> liva_json_arr_add_array, "json.addarr"
```
> Implement EVERY name: `jsonNewObject, jsonNewArray, jsonObjSetString, jsonObjSetInt, jsonObjSetFloat, jsonObjSetBool, jsonObjSetNull, jsonObjSetObject, jsonObjSetArray, jsonObjRemove, jsonArrAddString, jsonArrAddInt, jsonArrAddFloat, jsonArrAddBool, jsonArrAddNull, jsonArrAddObject, jsonArrAddArray`. The `/* same shape */` comments above must be expanded to full blocks.

- [ ] **Step 5: TypeChecker** — add all 17 names to the loop. Return types: `jsonNewObject/jsonNewArray/jsonObjSetObject/jsonObjSetArray/jsonArrAddObject/jsonArrAddArray` → `I64`; all `set_*`/`add_*` (non-container)/`remove`/`set_null`/`add_null` → void.
```cpp
} else if (ident->getName() == "jsonNewObject" || ident->getName() == "jsonNewArray" ||
           ident->getName() == "jsonObjSetObject" || ident->getName() == "jsonObjSetArray" ||
           ident->getName() == "jsonArrAddObject" || ident->getName() == "jsonArrAddArray") {
    ident->setResolvedType(makePrimitiveType(TypeRepr::Kind::I64));
} else if (ident->getName() == "jsonObjSetString" || ident->getName() == "jsonObjSetInt" ||
           ident->getName() == "jsonObjSetFloat" || ident->getName() == "jsonObjSetBool" ||
           ident->getName() == "jsonObjSetNull" || ident->getName() == "jsonObjRemove" ||
           ident->getName() == "jsonArrAddString" || ident->getName() == "jsonArrAddInt" ||
           ident->getName() == "jsonArrAddFloat" || ident->getName() == "jsonArrAddBool" ||
           ident->getName() == "jsonArrAddNull") {
    ident->setResolvedType(makeVoidType());
```

- [ ] **Step 6: ModuleLoader** — add all 17 names.

- [ ] **Step 7: `json.liva`** — `Json.object()/array()` + mutation methods.
Add to `impl Json`:
```liva
    pub func object() -> JsonObject {
        let doc = jsonNewObject()
        return JsonObject { docHandle: doc, nodeHandle: jsonRoot(doc), owns: true }
    }
    pub func array() -> JsonArray {
        let doc = jsonNewArray()
        return JsonArray { docHandle: doc, nodeHandle: jsonRoot(doc), owns: true }
    }
```
Add to `impl JsonObject`:
```liva
    pub func setString(mut self, key: String, val: String) { jsonObjSetString(self.docHandle, self.nodeHandle, key, val) }
    pub func setInt(mut self, key: String, val: i64) { jsonObjSetInt(self.docHandle, self.nodeHandle, key, val) }
    pub func setFloat(mut self, key: String, val: f64) { jsonObjSetFloat(self.docHandle, self.nodeHandle, key, val) }
    pub func setBool(mut self, key: String, val: bool) { jsonObjSetBool(self.docHandle, self.nodeHandle, key, val) }
    pub func setNull(mut self, key: String) { jsonObjSetNull(self.docHandle, self.nodeHandle, key) }
    pub func setObject(mut self, key: String) -> JsonObject {
        return JsonObject { docHandle: self.docHandle, nodeHandle: jsonObjSetObject(self.docHandle, self.nodeHandle, key), owns: false }
    }
    pub func setArray(mut self, key: String) -> JsonArray {
        return JsonArray { docHandle: self.docHandle, nodeHandle: jsonObjSetArray(self.docHandle, self.nodeHandle, key), owns: false }
    }
    pub func remove(mut self, key: String) { jsonObjRemove(self.nodeHandle, key) }
```
Add to `impl JsonArray`:
```liva
    pub func addString(mut self, val: String) { jsonArrAddString(self.docHandle, self.nodeHandle, val) }
    pub func addInt(mut self, val: i64) { jsonArrAddInt(self.docHandle, self.nodeHandle, val) }
    pub func addFloat(mut self, val: f64) { jsonArrAddFloat(self.docHandle, self.nodeHandle, val) }
    pub func addBool(mut self, val: bool) { jsonArrAddBool(self.docHandle, self.nodeHandle, val) }
    pub func addNull(mut self) { jsonArrAddNull(self.docHandle, self.nodeHandle) }
    pub func addObject(mut self) -> JsonObject {
        return JsonObject { docHandle: self.docHandle, nodeHandle: jsonArrAddObject(self.docHandle, self.nodeHandle), owns: false }
    }
    pub func addArray(mut self) -> JsonArray {
        return JsonArray { docHandle: self.docHandle, nodeHandle: jsonArrAddArray(self.docHandle, self.nodeHandle), owns: false }
    }
```

- [ ] **Step 8: Behavioral test (build + round-trip)**
```cpp
TEST_F(RuntimeExecTest, JsonBuildRoundTrip) {
    std::string src = R"LIVA(
import json::json
func main() {
    var o = Json.object()
    o.setString("name", "liva")
    o.setInt("age", 3)
    o.setBool("ok", true)
    var arr = o.setArray("tags")
    arr.addString("a")
    arr.addString("b")
    var child = o.setObject("addr")
    child.setString("city", "izmir")
    let s = o.toString()
    println(s)
    let reparsed = Json.parse(s)
    println(reparsed.object().getString("name"))
    println(reparsed.object().getObject("addr").getString("city"))
}
)LIVA";
    auto out = compileAndRun("json_build.liva", src);
    EXPECT_NE(out.find("\"name\":\"liva\""), std::string::npos) << out;
    EXPECT_NE(out.find("izmir"), std::string::npos) << out;
}
```

- [ ] **Step 9: Build + run** → PASS
- [ ] **Step 10: Full serial suite green**
- [ ] **Step 11: Commit** — `git add -A && git commit -m "json: building/mutation (Json.object/array, set*/add*, nested views)"`

---

## Task 7: Path read — `path("a.b.0.c")`

**Files:** `runtime.cpp`/`.h`, `IRGen.cpp`, `IRGenCall.cpp`, `TypeChecker.cpp`, `ModuleLoader.cpp`, `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Native (`runtime.cpp`)**
```cpp
extern "C" {
int64_t liva_json_path_get(int64_t nodeH, const char* path) {
    using namespace livajson;
    if (!nodeH || !path) return 0;
    Node* cur = asNode(nodeH);
    const char* p = path;
    std::string seg;
    while (cur) {
        seg.clear();
        while (*p && *p != '.') seg += *p++;
        if (*p == '.') p++;
        if (seg.empty()) break;
        bool allDigits = !seg.empty();
        for (char c : seg) if (c<'0'||c>'9') { allDigits=false; break; }
        if (cur->kind == K_Object) {
            Node* next = nullptr;
            for (auto& kv : cur->obj) if (kv.first == seg) { next = kv.second; break; }
            cur = next;
        } else if (cur->kind == K_Array && allDigits) {
            long idx = strtol(seg.c_str(), nullptr, 10);
            cur = (idx >= 0 && (size_t)idx < cur->arr.size()) ? cur->arr[idx] : nullptr;
        } else {
            cur = nullptr;
        }
        if (*p == '\0') break;  // consumed last segment when no trailing '.'
    }
    // Edge: when the path had a trailing segment with no following '.', the loop
    // body already advanced `cur`; ensure we return what we resolved.
    return reinterpret_cast<int64_t>(cur);
}
} // extern "C"
```
> Path grammar: segments split on `.`; an all-digit segment indexes an Array, otherwise it is an Object key. Returns `0` on any miss/type-mismatch.

- [ ] **Step 2: `runtime.h`** — `int64_t liva_json_path_get(int64_t nodeH, const char* path);`
- [ ] **Step 3: `createRuntimeDecls()`** — `module_->getOrInsertFunction("liva_json_path_get", llvm::FunctionType::get(i64Ty, {i64Ty, ptrTy}, false));`
- [ ] **Step 4: Lowering (`IRGenCall.cpp`)**
```cpp
if (funcName == "jsonPathGet" && node->getArgs().size() >= 2) {
    auto *h = visit(node->getArgs()[0].get());
    auto *path = visit(node->getArgs()[1].get());
    return builder_->CreateCall(getOrPanic("liva_json_path_get"), {h, path}, "json.pathget");
}
```
- [ ] **Step 5: TypeChecker** — name `"jsonPathGet",`; case → `I64`.
- [ ] **Step 6: ModuleLoader** — add `"jsonPathGet",`
- [ ] **Step 7: `json.liva`** — add to `impl JsonObject`:
```liva
    pub func path(ref self, p: String) -> JsonValue {
        return JsonValue { docHandle: self.docHandle, nodeHandle: jsonPathGet(self.nodeHandle, p), owns: false }
    }
```
- [ ] **Step 8: Behavioral test**
```cpp
TEST_F(RuntimeExecTest, JsonPathRead) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"a\":{\"b\":[{\"c\":42}]}}")
    let o = doc.object()
    let v = o.path("a.b.0.c")
    println(v.asInt())
    let missing = o.path("a.x.y")
    if missing.isNull() { println("missing-null") }
}
)LIVA";
    auto out = compileAndRun("json_path.liva", src);
    EXPECT_NE(out.find("42"), std::string::npos) << out;
    EXPECT_NE(out.find("missing-null"), std::string::npos) << out;
}
```
- [ ] **Step 9: Build + run** → PASS
- [ ] **Step 10: Full serial suite green**
- [ ] **Step 11: Commit** — `git add -A && git commit -m "json: path read (dotted/indexed)"`

---

## Task 8: Auto-vivifying path write — `setPath*`

**Files:** `runtime.cpp`/`.h`, `IRGen.cpp`, `IRGenCall.cpp`, `TypeChecker.cpp`, `ModuleLoader.cpp`, `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Natives (`runtime.cpp`)** — walk/create Object segments, set the leaf. (Array auto-viv via numeric segments is out of scope — only Object intermediates are created; a numeric segment on a non-array leaf is treated as an Object key.)
```cpp
extern "C" {
// Returns the parent object node for the final segment, creating intermediate
// Objects as needed; writes the final segment name into *leafKey.
static livajson::Node* pathParent(int64_t docH, int64_t nodeH, const char* path, std::string& leafKey) {
    using namespace livajson;
    Node* cur = asNode(nodeH);
    Doc* doc = asDoc(docH);
    if (!cur || cur->kind != K_Object) return nullptr;
    const char* p = path;
    std::string seg;
    while (true) {
        seg.clear();
        while (*p && *p != '.') seg += *p++;
        bool last = (*p == '\0');
        if (*p == '.') p++;
        if (seg.empty()) return nullptr;
        if (last) { leafKey = seg; return cur; }
        // descend/create an Object child
        Node* next = nullptr;
        for (auto& kv : cur->obj) if (kv.first == seg) { next = kv.second; break; }
        if (!next || next->kind != K_Object) {
            next = doc->make(K_Object);
            bool replaced = false;
            for (auto& kv : cur->obj) if (kv.first == seg) { kv.second = next; replaced = true; break; }
            if (!replaced) cur->obj.push_back({seg, next});
        }
        cur = next;
    }
}
void liva_json_path_set_string(int64_t d, int64_t node, const char* path, const char* val) {
    using namespace livajson; std::string key; Node* par = pathParent(d,node,path,key); if(!par) return;
    Node* v = asDoc(d)->make(K_String); v->str = val?val:"";
    for (auto& kv : par->obj) if (kv.first==key){ kv.second=v; return; } par->obj.push_back({key,v});
}
void liva_json_path_set_int(int64_t d, int64_t node, const char* path, int64_t val) {
    using namespace livajson; std::string key; Node* par = pathParent(d,node,path,key); if(!par) return;
    Node* v = asDoc(d)->make(K_Int); v->i = val;
    for (auto& kv : par->obj) if (kv.first==key){ kv.second=v; return; } par->obj.push_back({key,v});
}
void liva_json_path_set_float(int64_t d, int64_t node, const char* path, double val) {
    using namespace livajson; std::string key; Node* par = pathParent(d,node,path,key); if(!par) return;
    Node* v = asDoc(d)->make(K_Double); v->d = val;
    for (auto& kv : par->obj) if (kv.first==key){ kv.second=v; return; } par->obj.push_back({key,v});
}
void liva_json_path_set_bool(int64_t d, int64_t node, const char* path, int8_t val) {
    using namespace livajson; std::string key; Node* par = pathParent(d,node,path,key); if(!par) return;
    Node* v = asDoc(d)->make(K_Bool); v->b = val!=0;
    for (auto& kv : par->obj) if (kv.first==key){ kv.second=v; return; } par->obj.push_back({key,v});
}
} // extern "C"
```
- [ ] **Step 2: `runtime.h`** — declare the 4 `liva_json_path_set_*`.
- [ ] **Step 3: `createRuntimeDecls()`**
```cpp
module_->getOrInsertFunction("liva_json_path_set_string", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, ptrTy}, false));
module_->getOrInsertFunction("liva_json_path_set_int", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, i64Ty}, false));
module_->getOrInsertFunction("liva_json_path_set_float", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, f64Ty}, false));
module_->getOrInsertFunction("liva_json_path_set_bool", llvm::FunctionType::get(voidTy, {i64Ty, i64Ty, ptrTy, i8Ty}, false));
```
- [ ] **Step 4: Lowering (`IRGenCall.cpp`)** — 4 blocks; bool ZExts i1→i8:
```cpp
if (funcName == "jsonPathSetString" && node->getArgs().size() >= 4) {
    auto *d=visit(node->getArgs()[0].get()); auto *n=visit(node->getArgs()[1].get());
    auto *pth=visit(node->getArgs()[2].get()); auto *v=visit(node->getArgs()[3].get());
    return builder_->CreateCall(getOrPanic("liva_json_path_set_string"), {d, n, pth, v});
}
if (funcName == "jsonPathSetInt" && node->getArgs().size() >= 4) { /* -> liva_json_path_set_int */ }
if (funcName == "jsonPathSetFloat" && node->getArgs().size() >= 4) { /* -> liva_json_path_set_float */ }
if (funcName == "jsonPathSetBool" && node->getArgs().size() >= 4) {
    auto *d=visit(node->getArgs()[0].get()); auto *n=visit(node->getArgs()[1].get());
    auto *pth=visit(node->getArgs()[2].get()); auto *v=visit(node->getArgs()[3].get());
    v = builder_->CreateZExt(v, builder_->getInt8Ty(), "json.pbool.zext");
    return builder_->CreateCall(getOrPanic("liva_json_path_set_bool"), {d, n, pth, v});
}
```
> Expand the `/* */` blocks fully.
- [ ] **Step 5: TypeChecker** — names `"jsonPathSetString","jsonPathSetInt","jsonPathSetFloat","jsonPathSetBool",`; all → void.
- [ ] **Step 6: ModuleLoader** — add the 4 names.
- [ ] **Step 7: `json.liva`** — add to `impl JsonObject`:
```liva
    pub func setPathString(mut self, p: String, val: String) { jsonPathSetString(self.docHandle, self.nodeHandle, p, val) }
    pub func setPathInt(mut self, p: String, val: i64) { jsonPathSetInt(self.docHandle, self.nodeHandle, p, val) }
    pub func setPathFloat(mut self, p: String, val: f64) { jsonPathSetFloat(self.docHandle, self.nodeHandle, p, val) }
    pub func setPathBool(mut self, p: String, val: bool) { jsonPathSetBool(self.docHandle, self.nodeHandle, p, val) }
```
- [ ] **Step 8: Behavioral test**
```cpp
TEST_F(RuntimeExecTest, JsonSetPathAutoViv) {
    std::string src = R"LIVA(
import json::json
func main() {
    var o = Json.object()
    o.setPathString("a.b.c", "deep")
    o.setPathInt("a.b.n", 9)
    let v = o.path("a.b.c")
    println(v.asString())
    println(o.path("a.b.n").asInt())
    println(o.toString())
}
)LIVA";
    auto out = compileAndRun("json_setpath.liva", src);
    EXPECT_NE(out.find("deep"), std::string::npos) << out;
    EXPECT_NE(out.find("9"), std::string::npos) << out;
}
```
- [ ] **Step 9: Build + run** → PASS
- [ ] **Step 10: Full serial suite green**
- [ ] **Step 11: Commit** — `git add -A && git commit -m "json: auto-vivifying path write (setPath*)"`

---

## Task 9: Compiler change — struct subscript + obj["k"]/arr[i]

**Files:** `src/Sema/TypeChecker.cpp`, `src/IR/IRGenExpr.cpp`, `stdlib/json/json.liva`, `tests/unit/RuntimeExecTest.cpp`, `tests/unit/SemaTest.cpp`

Subscript codegen is currently class-only (`IRGenExpr.cpp visitIndexExpr` ~line 1088). `TypeChecker::visitIndexExpr` (line 3304) does not resolve subscript return types at all. Add struct support, mirroring the class path.

- [ ] **Step 1: Failing Sema test (`tests/unit/SemaTest.cpp`)**
```cpp
TEST_F(SemaTest, StructDecl_SubscriptReturnTypeResolves) {
    auto result = check(R"--(
        struct Bag {
            var n: i32
            subscript(key: string) -> i32 {
                return self.n
            }
        }
        func use(b: Bag) -> i32 {
            return b["x"]
        }
    )--");
    EXPECT_FALSE(result.diag.hasErrors()) << "struct subscript should type-check and resolve return type";
}
```
Run: `ctest --test-dir build-clang -R "StructDecl_SubscriptReturnTypeResolves" --output-on-failure`
Expected: currently FAIL (return type unresolved / type error on `b["x"]`).

- [ ] **Step 2: Resolve struct/class subscript return type in `TypeChecker::visitIndexExpr`**

At the end of `visitIndexExpr` (after the array-element block, before the closing brace at line ~3344), add:
```cpp
    // Struct/class subscript: base[idx] -> the `subscript` method's return type.
    if (!node->getResolvedType() &&
        node->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(const_cast<Expr *>(node->getBase()));
        auto *sym = scopes_.lookup(ident->getName());
        if (sym && sym->type && sym->type->getKind() == TypeRepr::Kind::Named) {
            auto *named = static_cast<const NamedTypeRepr *>(sym->type);
            auto *typeSym = scopes_.lookup(named->getName());
            const FuncDecl *sub = nullptr;
            if (typeSym && typeSym->structDecl) {
                for (auto &m : typeSym->structDecl->getMethods())
                    if (m->getName() == "subscript") { sub = m.get(); break; }
            }
            // (Optionally also handle typeSym->classDecl the same way.)
            if (sub && sub->getReturnType())
                node->setResolvedType(cloneTypeRepr(sub->getReturnType()));
        }
    }
```
> Inspect the actual accessors: how struct methods are stored on the struct decl/symbol (e.g. `getMethods()` vs an impl table), how `FuncDecl` exposes its return type (`getReturnType()`), and the `Symbol` field for a struct decl (`structDecl`). Adapt names to match this codebase. Keep the guard `!node->getResolvedType()` so existing String/array handling is untouched.

- [ ] **Step 3: Add struct branch to `IRGen::visitIndexExpr` (`IRGenExpr.cpp`, after the class branch ~line 1112)**
```cpp
    // Struct subscript: obj[i] → StructName_subscript(selfPtr, i)
    if (node->getBase()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *baseIdent = static_cast<const IdentifierExpr *>(node->getBase());
        auto svIt = vars_.varStructTypes.find(baseIdent->getName());
        if (svIt != vars_.varStructTypes.end()) {
            std::string subName = svIt->second + "_subscript";
            auto *subFn = module_->getFunction(subName);
            if (subFn) {
                auto nvIt = vars_.namedValues.find(baseIdent->getName());
                if (nvIt != vars_.namedValues.end()) {
                    auto *selfPtr = nvIt->second;  // struct alloca; methods receive self by pointer
                    auto *idxVal = visit(const_cast<Expr *>(node->getIndex()));
                    if (idxVal)
                        return builder_->CreateCall(subFn, {selfPtr, idxVal}, "struct.sub.call");
                }
            }
        }
    }
```
> Verify how struct methods receive `self` (by pointer to the alloca, as struct method calls elsewhere do). If `self` is loaded/passed differently, match that convention. Place this branch BEFORE the `non-identifier`/`err_irgen_subscript_failed` fallthrough so it takes effect.

- [ ] **Step 4: Add `subscript` to `JsonObject` and `JsonArray` (`json.liva`)**
```liva
// in impl JsonObject
    pub func subscript(ref self, key: String) -> JsonValue {
        return JsonValue { docHandle: self.docHandle, nodeHandle: jsonObjGet(self.nodeHandle, key), owns: false }
    }
// in impl JsonArray
    pub func subscript(ref self, i: i64) -> JsonValue {
        return JsonValue { docHandle: self.docHandle, nodeHandle: jsonArrAt(self.nodeHandle, i), owns: false }
    }
```

- [ ] **Step 5: Behavioral test (`RuntimeExecTest.cpp`)** — the high-risk String-key + Named-return runtime path:
```cpp
TEST_F(RuntimeExecTest, JsonSubscriptIndexer) {
    std::string src = R"LIVA(
import json::json
func main() {
    let doc = Json.parse("{\"name\":\"liva\",\"nums\":[5,6,7]}")
    let o = doc.object()
    println(o["name"].asString())
    let arr = o.getArray("nums")
    println(arr[1 as i64].asInt())
}
)LIVA";
    auto out = compileAndRun("json_subscript.liva", src);
    EXPECT_NE(out.find("liva"), std::string::npos) << out;
    EXPECT_NE(out.find("6"), std::string::npos) << out;
}
```

- [ ] **Step 6: Build + run new tests**
Run: `build_clang.bat && ctest --test-dir build-clang -R "StructDecl_SubscriptReturnTypeResolves|JsonSubscriptIndexer" --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Full serial suite green — CRITICAL gate**
Run: `ctest --test-dir build-clang --output-on-failure`
Expected: all green. If any unrelated index/array test regresses (cf. the `[u8]` sign-extension regression in the typed-DB task), narrow the TypeChecker resolution strictly to the subscript-method case and re-run. Verify by rebuild, never `git stash` without rebuild.

- [ ] **Step 8: Commit** — `git add -A && git commit -m "compiler+json: struct subscript support; obj[\"k\"]/arr[i] indexer"`

---

## Task 10: Remove the old string-based JSON API + bindings

Now that the new API is complete, delete the dead old surface.

**Files:** `runtime.cpp`/`.h`, `IRGen.cpp`, `IRGenCall.cpp`, `TypeChecker.cpp`, `ModuleLoader.cpp`

- [ ] **Step 1: Confirm nothing references the old names**
Run: `grep -rn "jsonGet\b\|jsonGetInt\|jsonGetFloat\|jsonGetBool\|jsonIsValid\|jsonKeys\|jsonCreate\|jsonSet\|jsonSetInt\|jsonSetFloat\|jsonSetBool\|jsonRemove\|jsonGetArray\|jsonGetObject\|jsonCount\|jsonStringifyPretty" stdlib tests`
Expected: no matches (the new `json.liva` uses none of them; old tests were replaced in Task 1).

- [ ] **Step 2: Remove old runtime functions** — delete `liva_json_get, liva_json_get_int, liva_json_get_float, liva_json_get_bool, liva_json_is_valid, liva_json_keys, liva_json_create, liva_json_set, liva_json_set_int, liva_json_set_float, liva_json_set_bool, liva_json_remove, liva_json_get_array, liva_json_get_object, liva_json_count, liva_json_stringify_pretty` from `runtime.cpp` and their decls from `runtime.h`. (Keep the shared helpers `json_skip_ws`/`json_skip_value`/`json_extract_string` ONLY if still referenced; otherwise remove.)

- [ ] **Step 3: Remove old `createRuntimeDecls` entries** (`IRGen.cpp` ~lines 988–1000+ for the old names).

- [ ] **Step 4: Remove old lowering blocks** (`IRGenCall.cpp` ~lines 4045–4220: the `jsonGet`…`jsonStringifyPretty` blocks).

- [ ] **Step 5: Remove old TypeChecker names + cases** (`TypeChecker.cpp` line ~129 list entries and the switch cases ~2522–2543 for old names).

- [ ] **Step 6: Remove old ModuleLoader names** (`ModuleLoader.cpp` ~lines 95–100 old entries; keep only the new `json*` names).

- [ ] **Step 7: Build**
Run: `build_clang.bat`
Expected: clean build (no references to removed symbols).

- [ ] **Step 8: Full serial suite green**
Run: `ctest --test-dir build-clang --output-on-failure`

- [ ] **Step 9: Commit** — `git add -A && git commit -m "json: remove dead string-based API + bindings (breaking)"`

---

## Task 11: Documentation

**Files:** `docs/en/API-REFERENCE.md`, `docs/tr/API-REFERENCE.md`

- [ ] **Step 1: Rewrite the JSON section in `docs/en/API-REFERENCE.md`**
Replace the old `json::json` documentation with the new API: the ownership contract (bind the parse result; views are borrows; don't chain off a parse temporary), `Json.parse/object/array`, `JsonValue` (kind/is*/as*/object/array/toString/toStringPretty), `JsonObject` (subscript `["k"]`, get*/try*/set*/setObject/setArray/has/remove/keys/count/path/setPath*), `JsonArray` (subscript `[i]`, count/length/get*/at/add*/addObject/addArray). Include a worked example:
```liva
import json::json
func main() {
    let doc = Json.parse("{\"user\":{\"name\":\"liva\",\"age\":3},\"tags\":[\"a\",\"b\"]}")
    let root = doc.object()
    println(root.path("user.name").asString())   // liva
    println(root.getObject("user").getInt("age")) // 3
    println(root.getArray("tags").getString(0 as i64)) // a

    var out = Json.object()
    out.setString("name", "yeni")
    var tags = out.setArray("tags")
    tags.addString("x")
    println(out.toString())
}
```

- [ ] **Step 2: Mirror the rewrite in `docs/tr/API-REFERENCE.md`** (Turkish prose, same API and example, comments in Turkish).

- [ ] **Step 3: Commit** — `git add -A && git commit -m "docs: rewrite JSON API reference for parse-tree redesign (tr+en)"`

---

## Final verification

- [ ] Run the FULL serial suite one last time: `ctest --test-dir build-clang --output-on-failure` — all green.
- [ ] Confirm no `liva_json_get`/old-name references remain anywhere: `grep -rn "liva_json_get\b\|liva_json_create\|liva_json_stringify_pretty" src stdlib`.
- [ ] Update `MEMORY.md` with a one-line pointer to the new JSON module design if appropriate.
