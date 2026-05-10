# P1-8 Hash Family (Alt-Spec 3): Runtime B-tree — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Liva'ya ordered key/value map ekle — C++ runtime'da `std::map` tabanlı backend + 14 type-specialized `extern "C"` entry point; stdlib'de `BTreeMapI64I64` ve `BTreeMapStrI64` concrete wrapper'lar.

**Architecture:** Runtime'da iki ayrı handle tipi (`std::map<int64_t, int64_t>*` ve `std::map<std::string, int64_t>*`), `int64_t` handle olarak Liva'ya geçer. Stdlib `extern "C"` block ile FFI bindings, struct'lar handle'ı `var handle: i64` field'ında tutar.

**Tech Stack:** C++20 std::map, Liva FFI (`extern "C"`), CMake, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-05-11-btree-runtime-design.md` (commit `fcce0dd`).

---

## Task 1: Runtime B-tree (i64-key family)

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (B-tree section)
- Modify: `stdlib/runtime/runtime.h` (forward declarations)

- [ ] **Step 1: `stdlib/runtime/runtime.cpp`'a `#include <map>` ve `#include <string>` ekle**

Eğer dosyanın başında zaten yoksa. (Muhtemelen `<map>` yok; `<string>` olabilir.)

- [ ] **Step 2: i64-key family fonksiyonlarını ekle**

Mevcut `liva_hash_*` wrapper'larından sonra (yaklaşık satır 1150 civarı):

```cpp
// === BTreeMap runtime (P1-8 alt-spec 3) ===
// Backed by std::map for ordered O(log n) semantics. Two type-specialized
// families: i64 keys and string keys, both with i64 values.

extern "C" int64_t liva_btree_i64_new() {
    return reinterpret_cast<int64_t>(new std::map<int64_t, int64_t>());
}

extern "C" void liva_btree_i64_free(int64_t handle) {
    delete reinterpret_cast<std::map<int64_t, int64_t>*>(handle);
}

extern "C" void liva_btree_i64_insert(int64_t handle, int64_t key, int64_t value) {
    auto *m = reinterpret_cast<std::map<int64_t, int64_t>*>(handle);
    (*m)[key] = value;
}

extern "C" int64_t liva_btree_i64_get(int64_t handle, int64_t key) {
    auto *m = reinterpret_cast<std::map<int64_t, int64_t>*>(handle);
    auto it = m->find(key);
    if (it == m->end()) return INT64_MIN;
    return it->second;
}

extern "C" int8_t liva_btree_i64_contains(int64_t handle, int64_t key) {
    auto *m = reinterpret_cast<std::map<int64_t, int64_t>*>(handle);
    return m->find(key) != m->end() ? 1 : 0;
}

extern "C" int8_t liva_btree_i64_remove(int64_t handle, int64_t key) {
    auto *m = reinterpret_cast<std::map<int64_t, int64_t>*>(handle);
    return m->erase(key) > 0 ? 1 : 0;
}

extern "C" int64_t liva_btree_i64_size(int64_t handle) {
    auto *m = reinterpret_cast<std::map<int64_t, int64_t>*>(handle);
    return static_cast<int64_t>(m->size());
}
```

- [ ] **Step 3: string-key family fonksiyonlarını ekle**

Hemen i64 family'nin altına:

```cpp
extern "C" int64_t liva_btree_str_new() {
    return reinterpret_cast<int64_t>(new std::map<std::string, int64_t>());
}

extern "C" void liva_btree_str_free(int64_t handle) {
    delete reinterpret_cast<std::map<std::string, int64_t>*>(handle);
}

extern "C" void liva_btree_str_insert(int64_t handle, const char *key, int64_t value) {
    auto *m = reinterpret_cast<std::map<std::string, int64_t>*>(handle);
    (*m)[std::string(key ? key : "")] = value;
}

extern "C" int64_t liva_btree_str_get(int64_t handle, const char *key) {
    auto *m = reinterpret_cast<std::map<std::string, int64_t>*>(handle);
    auto it = m->find(std::string(key ? key : ""));
    if (it == m->end()) return INT64_MIN;
    return it->second;
}

extern "C" int8_t liva_btree_str_contains(int64_t handle, const char *key) {
    auto *m = reinterpret_cast<std::map<std::string, int64_t>*>(handle);
    return m->find(std::string(key ? key : "")) != m->end() ? 1 : 0;
}

extern "C" int8_t liva_btree_str_remove(int64_t handle, const char *key) {
    auto *m = reinterpret_cast<std::map<std::string, int64_t>*>(handle);
    return m->erase(std::string(key ? key : "")) > 0 ? 1 : 0;
}

extern "C" int64_t liva_btree_str_size(int64_t handle) {
    auto *m = reinterpret_cast<std::map<std::string, int64_t>*>(handle);
    return static_cast<int64_t>(m->size());
}
```

- [ ] **Step 4: `stdlib/runtime/runtime.h`'a forward declarations ekle**

Mevcut `liva_map_*` veya `liva_hash_*` forward declarations'ın altına (extern "C" bloku içindeyse zaten extern "C" almasına gerek yok; aynı bloğa eklenmesi yeterli):

```cpp
// BTreeMap runtime (P1-8 alt-spec 3)
int64_t liva_btree_i64_new();
void    liva_btree_i64_free(int64_t handle);
void    liva_btree_i64_insert(int64_t handle, int64_t key, int64_t value);
int64_t liva_btree_i64_get(int64_t handle, int64_t key);
int8_t  liva_btree_i64_contains(int64_t handle, int64_t key);
int8_t  liva_btree_i64_remove(int64_t handle, int64_t key);
int64_t liva_btree_i64_size(int64_t handle);

int64_t liva_btree_str_new();
void    liva_btree_str_free(int64_t handle);
void    liva_btree_str_insert(int64_t handle, const char *key, int64_t value);
int64_t liva_btree_str_get(int64_t handle, const char *key);
int8_t  liva_btree_str_contains(int64_t handle, const char *key);
int8_t  liva_btree_str_remove(int64_t handle, const char *key);
int64_t liva_btree_str_size(int64_t handle);
```

`runtime.h`'da `extern "C" { ... }` bloku içindeyse separate `extern "C"` kelimesine gerek yok.

- [ ] **Step 5: Build**

```
cmake --build build-clang --parallel
```

Expected: BUILD SUCCESS. (No callers — dead code.)

- [ ] **Step 6: Sanity test**

```
ctest --test-dir build-clang -j 1 2>&1 | tail -5
```

Expected: 2274/2274 still pass (no regression).

- [ ] **Step 7: Commit**

```bash
git add stdlib/runtime/runtime.cpp stdlib/runtime/runtime.h
git commit -m "runtime: add BTreeMap i64/string families (P1-8 alt-spec 3)"
```

---

## Task 2: Stdlib `collections::btree` Module

**Files:**
- Create: `stdlib/collections/btree.liva`

- [ ] **Step 1: `stdlib/collections/btree.liva` oluştur**

İçerik:

```liva
// collections::btree — Ordered key/value maps backed by runtime balanced BST.
//
// Concrete types in this iteration (P1-8 alt-spec 3):
//   - BTreeMapI64I64: i64 keys, i64 values
//   - BTreeMapStrI64: string keys, i64 values
//
// Generic BTreeMap<K, V>, BTreeSet, and in-order iteration are future-work.

extern "C" {
    func liva_btree_i64_new() -> i64
    func liva_btree_i64_free(handle: i64)
    func liva_btree_i64_insert(handle: i64, key: i64, value: i64)
    func liva_btree_i64_get(handle: i64, key: i64) -> i64
    func liva_btree_i64_contains(handle: i64, key: i64) -> i8
    func liva_btree_i64_remove(handle: i64, key: i64) -> i8
    func liva_btree_i64_size(handle: i64) -> i64

    func liva_btree_str_new() -> i64
    func liva_btree_str_free(handle: i64)
    func liva_btree_str_insert(handle: i64, key: string, value: i64)
    func liva_btree_str_get(handle: i64, key: string) -> i64
    func liva_btree_str_contains(handle: i64, key: string) -> i8
    func liva_btree_str_remove(handle: i64, key: string) -> i8
    func liva_btree_str_size(handle: i64) -> i64
}

// ===========================================================================
// BTreeMapI64I64 — ordered i64 → i64 map
// ===========================================================================

pub struct BTreeMapI64I64 {
    var handle: i64
}

impl BTreeMapI64I64 {
    pub static func new() -> BTreeMapI64I64 {
        return BTreeMapI64I64 { handle: liva_btree_i64_new() }
    }

    pub func insert(ref mut self, key: i64, value: i64) {
        liva_btree_i64_insert(self.handle, key, value)
    }

    pub func get(ref self, key: i64) -> i64? {
        if liva_btree_i64_contains(self.handle, key) == 0 { return nil }
        return liva_btree_i64_get(self.handle, key)
    }

    pub func contains(ref self, key: i64) -> bool {
        return liva_btree_i64_contains(self.handle, key) != 0
    }

    pub func remove(ref mut self, key: i64) -> bool {
        return liva_btree_i64_remove(self.handle, key) != 0
    }

    pub func size(ref self) -> i64 {
        return liva_btree_i64_size(self.handle)
    }

    pub func isEmpty(ref self) -> bool {
        return self.size() == 0
    }

    pub func free(ref mut self) {
        if self.handle != 0 {
            liva_btree_i64_free(self.handle)
            self.handle = 0
        }
    }
}

// ===========================================================================
// BTreeMapStrI64 — ordered string → i64 map
// ===========================================================================

pub struct BTreeMapStrI64 {
    var handle: i64
}

impl BTreeMapStrI64 {
    pub static func new() -> BTreeMapStrI64 {
        return BTreeMapStrI64 { handle: liva_btree_str_new() }
    }

    pub func insert(ref mut self, key: string, value: i64) {
        liva_btree_str_insert(self.handle, key, value)
    }

    pub func get(ref self, key: string) -> i64? {
        if liva_btree_str_contains(self.handle, key) == 0 { return nil }
        return liva_btree_str_get(self.handle, key)
    }

    pub func contains(ref self, key: string) -> bool {
        return liva_btree_str_contains(self.handle, key) != 0
    }

    pub func remove(ref mut self, key: string) -> bool {
        return liva_btree_str_remove(self.handle, key) != 0
    }

    pub func size(ref self) -> i64 {
        return liva_btree_str_size(self.handle)
    }

    pub func isEmpty(ref self) -> bool {
        return self.size() == 0
    }

    pub func free(ref mut self) {
        if self.handle != 0 {
            liva_btree_str_free(self.handle)
            self.handle = 0
        }
    }
}
```

- [ ] **Step 2: Build (modülün sema'dan geçtiğini doğrula)**

`cmake --build build-clang --parallel`

Expected: BUILD SUCCESS. (Liva derleyici stdlib dosyalarını parse etmez build sırasında; sadece test/example derlemeleri sırasında loader yükler.)

- [ ] **Step 3: Smoke check — basit import + sema**

Geçici unit test ile ya da `livac` ile elle test et:

```liva
import collections::btree
func main() {
    var m = BTreeMapI64I64.new()
    m.insert(1, 100)
    let v = m.get(1)
    m.free()
}
```

Liva derleyicisi bu programı kabul etmeli (sema hatası olmamalı). Test execution bir sonraki task'ta.

- [ ] **Step 4: Commit**

```bash
git add stdlib/collections/btree.liva
git commit -m "stdlib: add collections::btree (BTreeMapI64I64/StrI64) — P1-8 alt-spec 3"
```

---

## Task 3: Sema + Runtime Testleri

**Files:**
- Modify: `tests/unit/StdlibModuleTest.cpp` (sema testleri)
- Modify: `tests/unit/RuntimeExecTest.cpp` (runtime testleri)

- [ ] **Step 1: `tests/unit/StdlibModuleTest.cpp`'a 2 sema testi ekle**

Mevcut `CollectionsGenericStack` veya `CollectionsGenericQueue` test'lerinin yanına:

```cpp
TEST_F(StdlibModuleTest, BTreeMapI64I64Type) {
    auto r = check(
        "import collections::btree\n"
        "func main() {\n"
        "    var m = BTreeMapI64I64.new()\n"
        "    m.insert(1, 100)\n"
        "    let v = m.get(1)\n"
        "    let n = m.size()\n"
        "    let e = m.isEmpty()\n"
        "    m.remove(1)\n"
        "    m.free()\n"
        "}\n");
    EXPECT_TRUE(r.passed);
}

TEST_F(StdlibModuleTest, BTreeMapStrI64Type) {
    auto r = check(
        "import collections::btree\n"
        "func main() {\n"
        "    var m = BTreeMapStrI64.new()\n"
        "    m.insert(\"key\", 42)\n"
        "    let v = m.get(\"key\")\n"
        "    m.free()\n"
        "}\n");
    EXPECT_TRUE(r.passed);
}
```

- [ ] **Step 2: `tests/unit/RuntimeExecTest.cpp`'a 4 runtime testi ekle**

`RuntimeExecTest` fixture'ının helper API'sini (compileAndRun veya benzer) kullanarak:

```cpp
TEST_F(RuntimeExecTest, BTreeMapI64OrderedInsertGet) {
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapI64I64.new()
            m.insert(5, 50)
            m.insert(2, 20)
            m.insert(8, 80)
            print(m.get(5).unwrap())
            print(m.get(2).unwrap())
            print(m.get(8).unwrap())
            print(m.size())
            m.free()
        }
    )--", "BTreeMapI64OrderedInsertGet");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("50"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("20"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("80"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos);
}

TEST_F(RuntimeExecTest, BTreeMapI64ContainsAndRemove) {
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapI64I64.new()
            m.insert(10, 100)
            m.insert(20, 200)
            if m.contains(10) { print("has10") }
            m.remove(10)
            if m.contains(10) { print("still10") } else { print("no10") }
            print(m.size())
            m.free()
        }
    )--", "BTreeMapI64ContainsAndRemove");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("has10"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("no10"), std::string::npos);
}

TEST_F(RuntimeExecTest, BTreeMapStrLookup) {
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapStrI64.new()
            m.insert("apple", 1)
            m.insert("banana", 2)
            m.insert("cherry", 3)
            print(m.get("apple").unwrap())
            print(m.get("banana").unwrap())
            print(m.get("cherry").unwrap())
            if m.get("missing") == nil { print("absent") }
            m.free()
        }
    )--", "BTreeMapStrLookup");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("1"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("2"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("absent"), std::string::npos);
}

TEST_F(RuntimeExecTest, BTreeMapI64SizeAndIsEmpty) {
    auto r = compileAndRun(R"--(
        import collections::btree
        func main() {
            var m = BTreeMapI64I64.new()
            if m.isEmpty() { print("empty") }
            m.insert(1, 1)
            m.insert(2, 2)
            m.insert(3, 3)
            print(m.size())
            m.free()
        }
    )--", "BTreeMapI64SizeAndIsEmpty");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("empty"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("3"), std::string::npos);
}
```

- [ ] **Step 3: Build + yeni testleri çalıştır**

```
cmake --build build-clang --parallel
ctest --test-dir build-clang -R "BTreeMap|btree" -j 1 --output-on-failure
```

Expected: All 6 new tests PASS (2 sema + 4 runtime).

Eğer FFI'da problem çıkarsa (örn. `extern "C" { func ... }` blok syntax'ı parse edilmiyor):
- Tek tek `extern "C" func liva_btree_i64_new() -> i64` formatına geç.
- examples/ffi_demo.liva'da ikisi de görünüyor; ikisi de çalışmalı.

- [ ] **Step 4: Full regression**

```
ctest --test-dir build-clang -j 1 2>&1 | tail -5
```

Expected: 2280/2280 (2274 baseline + 6 new) PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/StdlibModuleTest.cpp tests/unit/RuntimeExecTest.cpp
git commit -m "test: BTreeMap sema + runtime tests (6 tests, P1-8 alt-spec 3)"
```

---

## Task 4: Status Update + Final Regression

**Files:**
- Modify: `status.md`

- [ ] **Step 1: status.md güncelle**

A. Line 4: `**Test Durumu:** 2274/2274` → `2280/2280`
B. Line 13: `| **Toplam Test** | **2274/2274 geçiyor**` → `2280/2280`
C. P1-8 alt-spec 2 satırının altına yeni satır:

```markdown
- **P1-8 (alt-spec 3): BTreeMap (ordered map)** — Runtime `std::map` backed B-tree; `BTreeMapI64I64` ve `BTreeMapStrI64` concrete stdlib types. `import collections::btree` ile kullanılır; insert/get/contains/remove/size/isEmpty/free metotları. Generic BTreeMap<K,V>, BTreeSet, ve in-order iteration future-work (alt-spec 4).
```

- [ ] **Step 2: Final full regression**

```
ctest --test-dir build-clang -j 1 2>&1 | tail -5
```

Expected: 2280/2280 PASS.

- [ ] **Step 3: Commit**

```bash
git add status.md
git commit -m "status: P1-8 alt-spec 3 complete (BTreeMap, 2280 tests)"
```

---

## Doğrulama Checklist

- [ ] Runtime'da 14 yeni `liva_btree_*` fonksiyonu.
- [ ] `runtime.h` forward declarations.
- [ ] `stdlib/collections/btree.liva` modülü.
- [ ] 2 sema testi (`BTreeMapI64I64Type`, `BTreeMapStrI64Type`).
- [ ] 4 runtime testi (`BTreeMapI64OrderedInsertGet`, `BTreeMapI64ContainsAndRemove`, `BTreeMapStrLookup`, `BTreeMapI64SizeAndIsEmpty`).
- [ ] Full ctest 2280/2280 PASS.
- [ ] `status.md` güncellendi.

## Notlar

- `std::map<int64_t, int64_t>` ve `std::map<std::string, int64_t>` C++ stdlib'i — derleyici desteği yeterli, ek build flag gerekmez.
- `reinterpret_cast<int64_t>` pointer→int64 conversion: 64-bit hedeflerde güvenli; 32-bit hedef yoksa sorun yok.
- Eğer `INT64_MIN` makrosu için `<climits>` veya `<cstdint>` gerekiyorsa, eklenmesi unutulmamalı (mevcut runtime'da büyük ihtimalle zaten var).
- Liva FFI'ın `extern "C" { ... }` block syntax'ı kabul ettiği doğrulandı (`examples/ffi_demo.liva:5-8`).
