# P1-8 Hash Family (Alt-Spec 3): Runtime B-tree + Concrete Stdlib Wrappers

**Status:** Draft → Approved
**Date:** 2026-05-11
**Predecessor:** Alt-Spec 2 (`docs/superpowers/specs/2026-05-10-hashable-builtin-design.md`, commit `f66ff75`)
**Successor:** Alt-Spec 4 (Generic `BTreeMap<K,V>` — future-work)

---

## Goal

Liva'ya ordered key/value map veri yapısı eklemek. C++ runtime'da gerçek B-tree implementasyonu (CLRS-style, degree t=8) inşa edilir; stdlib'de iki **concrete** wrapper expose edilir:
- `BTreeMapI64I64` (i64 anahtar, i64 değer)
- `BTreeMapStrI64` (string anahtar, i64 değer)

Bu alt-spec'in çıktıları:

1. Runtime'da `liva_btree_*` C++ B-tree implementasyonu (insert/get/contains/remove/size/free).
2. Runtime entry point'leri: `extern "C"` API'lar `key_kind`+sizes ile generic-dispatch (gelecek generic Liva wrapper için altyapı).
3. Stdlib: `collections::btree` modülü — `BTreeMapI64I64` ve `BTreeMapStrI64` struct'ları, `var handle: i64` field'ı opaque runtime pointer'ı temsil eder.
4. Yeni testler: 2 sema + 4 runtime testi.

Bu alt-spec'in **dışında** kalan:

- **Generic `BTreeMap<K, V>`** — Liva-side `sizeof<T>` / `keyKind<T>` intrinsics veya IRGen monomorphization (`varMapTypes` benzeri) gerektirir. Alt-spec 4.
- **`BTreeSet<T>`** — BTreeMap alt yapısı kullanarak alt-spec 4'te eklenir (`BTreeSetI64`, `BTreeSetStr`).
- **In-order iteration** (`for k in btree`) — runtime'da iterator state machine + IRGen for-in dispatch gerekir. Alt-spec 4 veya 5.
- **Range queries** (`btree.range(lo, hi)`) — future-work.
- **Comparable protocol** — i64 ve string için doğrudan comparator kullanılır; protokol declaration gerekmez bu evrede.

## Arka Plan

Mevcut `Map<K,V>` built-in hash-table; ordering garantisi yok. Liva'nın `Stack<T>`/`Queue<T>` stdlib pattern'i pure-Liva struct + DynArray; tree veri yapısı için yetersiz. Mevcut sqlite/http/websocket modülleri opaque pointer'ları `i64` handle olarak stdlib struct'a koyuyor — bu alt-spec aynı pattern'i kullanır.

## Tasarım

### 1. Runtime Ordered Map Implementation

C++'da ordered key/value map. **Implementasyon seçimi**: en pragmatik yaklaşım `std::map<K, V>` (C++ standart kütüphane, balanced BST — tipik olarak red-black tree, O(log n) ops, well-tested). Alternatif: hand-rolled B-tree (CLRS Bölüm 18, degree t=8). Spec her ikisini de kabul eder — implementer'ın çağrısı; semantik aynı.

**Type-specialized entry points** (generic typed-pointer FFI'dan kaçınmak için):

- **i64-key family** (i64 anahtar, i64 değer):
  ```cpp
  extern "C" int64_t liva_btree_i64_new();
  extern "C" void    liva_btree_i64_free(int64_t handle);
  extern "C" void    liva_btree_i64_insert(int64_t handle, int64_t key, int64_t value);
  // Returns INT64_MIN sentinel if key absent.
  extern "C" int64_t liva_btree_i64_get(int64_t handle, int64_t key);
  extern "C" int8_t  liva_btree_i64_contains(int64_t handle, int64_t key);
  extern "C" int8_t  liva_btree_i64_remove(int64_t handle, int64_t key);
  extern "C" int64_t liva_btree_i64_size(int64_t handle);
  ```

- **string-key family** (string anahtar, i64 değer):
  ```cpp
  extern "C" int64_t liva_btree_str_new();
  extern "C" void    liva_btree_str_free(int64_t handle);
  extern "C" void    liva_btree_str_insert(int64_t handle, const char *key, int64_t value);
  extern "C" int64_t liva_btree_str_get(int64_t handle, const char *key);
  extern "C" int8_t  liva_btree_str_contains(int64_t handle, const char *key);
  extern "C" int8_t  liva_btree_str_remove(int64_t handle, const char *key);
  extern "C" int64_t liva_btree_str_size(int64_t handle);
  ```

- **Sentinel `INT64_MIN`**: `get` API'sı için "anahtar yok" işaretçisi. Stdlib wrapper bunu `nil` Optional'a dönüştürür. `INT64_MIN` Liva i64 değer olarak nadir olduğu için iyi bir sentinel; çakışma durumunda kullanıcı `contains` ile teyit eder.

- **Memory model:**
  - `_new` heap'te `std::map` veya custom B-tree instance allocate eder; handle iade eder
  - `_free` instance'ı `delete` eder
  - Liva-side ownership: stdlib struct'ın manuel `free()` method'u (destructor desteği kapsam dışı)

- **Yaklaşık LOC:** ~150 LOC (std::map kullanırsa) veya ~400 LOC (hand-rolled B-tree).

### 2. Stdlib: `collections::btree`

Yeni dosya: `stdlib/collections/btree.liva`

```liva
// collections::btree — Ordered key/value maps backed by runtime balanced BST.
//
// Current concrete types (P1-8 alt-spec 3):
//   - BTreeMapI64I64: i64 keys, i64 values
//   - BTreeMapStrI64: string keys, i64 values
//
// Generic BTreeMap<K, V> and BTreeSet are future-work (alt-spec 4).

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

// Sentinel returned by `_get` runtime fn when key is absent. Liva i64
// minimum value; chosen because actual user data rarely hits this.
const BTREE_GET_MISSING: i64 = -9223372036854775808

// ---------------------------------------------------------------------------
// BTreeMapI64I64 — i64 keyed, i64 valued ordered map.
// ---------------------------------------------------------------------------

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
        // Two-call protocol: contains-then-get; avoids sentinel ambiguity.
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

// ---------------------------------------------------------------------------
// BTreeMapStrI64 — string keyed, i64 valued ordered map.
// ---------------------------------------------------------------------------

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

**Notlar:**
- `extern "C" { ... }` blok syntax'ı Liva FFI'da mevcut (örnek: `examples/ffi_demo.liva`).
- Tip-spesifik runtime entry'leri generic pointer FFI gerektirmiyor — i64 ve string Liva primitif tipleri olarak doğrudan geçiyor.
- `free()` manuel; Liva'nın `deinit` desteği varsa onu da ekle.
- `BTREE_GET_MISSING` const şu an kullanılmıyor (`contains+get` two-call protocol tercih edildi); ileride lazım olabilir.

### 3. Diagnostics

Yeni diagnostic eklenmez.

### 4. Test Stratejisi

#### 4.1 Sema testleri

`tests/unit/StdlibModuleTest.cpp` (mevcut `CollectionsGenericStack` benzeri):

**Test A — BTreeMapI64I64Type:** `var m = BTreeMapI64I64.new(); m.insert(1, 100)` parse + sema.

**Test B — BTreeMapStrI64Type:** `var m = BTreeMapStrI64.new(); m.insert("k", 42)` parse + sema.

#### 4.2 Runtime testleri

`tests/unit/RuntimeExecTest.cpp` veya yeni `tests/unit/BTreeTest.cpp`:

**Test C — BTreeMapI64OrderedInsertGet:**
```liva
import collections::btree
func main() {
    var m = BTreeMapI64I64.new()
    m.insert(5, 50)
    m.insert(2, 20)
    m.insert(8, 80)
    print(m.get(5).unwrap())  // 50
    print(m.get(2).unwrap())  // 20
    print(m.get(8).unwrap())  // 80
    print(m.size())            // 3
    m.free()
}
```

**Test D — BTreeMapI64Remove:** insert/remove/contains akışı.

**Test E — BTreeMapStrLookup:** string key insert + get.

**Test F — BTreeMapStrManyKeys:** 100+ string anahtar ekle, hepsini geri al — B-tree split/merge stress.

## Dosya Manifest

| Eylem  | Dosya                                            | Boyut         |
| ------ | ------------------------------------------------ | ------------- |
| Modify | `stdlib/runtime/runtime.cpp`                     | +400-500 LOC (B-tree impl + 7 extern "C" fn) |
| Modify | `stdlib/runtime/runtime.h`                       | +7 fn forward decl |
| Create | `stdlib/collections/btree.liva`                  | ~150 LOC (2 struct + 18 method) |
| Modify | `tests/unit/StdlibModuleTest.cpp`                | +2 sema test |
| Create | `tests/unit/BTreeTest.cpp` (veya RuntimeExecTest)| +4 runtime test |
| Modify | `tests/CMakeLists.txt` (yeni test exe ise)       | +1 hedef |
| Modify | `status.md`                                      | Test sayısı + yeni özellik satırı |

## Doğrulama

1. Tüm 2274+ mevcut test geçer (`ctest --test-dir build-clang -j 1`).
2. Yeni 6 test (2 sema + 4 runtime) geçer.
3. B-tree stress testi: 100+ key insert/lookup deterministik.

## Riskler & Hafifletmeler

| Risk                                            | Hafifletme |
| ----------------------------------------------- | ---------- |
| B-tree implementasyon hataları (split/merge)    | CLRS pseudocode'a sadık kalın; basit kapsamlı testlerle stress (100+ keys). |
| Liva FFI pointer cast syntax karmaşık           | Mevcut sqlite/http wrapper'ları örnek olarak alın; aynı pattern. |
| Memory leak (handle free unutulur)              | Test'lerde explicit `m.free()` çağrısı; future: deinit. |
| Generic K/V eksikliği kullanıcı için kısıtlayıcı | Spec açıkça future-work olarak işaretler; concrete tipler %80 use case'i karşılar. |
| `&i8` cast Liva'da desteklenmiyor olabilir       | Sqlite wrapper'ın ne kullandığını incele; gerekirse `i64` cast üzerinden pointer geçirimi. |

## Tahmini Süre

~3-4 gün (subagent-driven, 5 task):
1. Runtime B-tree C++ implementasyonu (~1.5 gün)
2. Runtime `extern "C"` API + header forward decls (~0.3 gün)
3. Stdlib `collections::btree` modülü (~0.5 gün)
4. Sema + runtime testleri (~0.5 gün)
5. Final regression + status update (~0.2 gün)

## Sonraki Adımlar

Alt-spec 3 onaylandıktan sonra:
- `writing-plans` → `docs/superpowers/plans/2026-05-11-btree-runtime.md`
- `subagent-driven-development` ile uygulama

İleride (alt-spec 4+):
- Generic `BTreeMap<K, V>` — Liva-side `sizeof<T>` intrinsic veya IRGen monomorphization
- `BTreeSetI64`, `BTreeSetStr` (BTreeMap üzerine ince wrapper)
- In-order iteration: `for (k, v) in btree` (runtime iterator state)
- Range queries: `btree.range(lo, hi)`
- `Comparable` protocol (user-defined key types için)
