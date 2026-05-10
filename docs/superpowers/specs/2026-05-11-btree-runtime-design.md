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

### 1. Runtime B-tree Implementation

C++'da CLRS Bölüm 18 deseninde B-tree:

- **Degree (t):** 8 — her node 7..15 anahtar tutar, root için minimum 1
- **Node layout:** dynamic alloc — `[num_keys: int32][is_leaf: int8][keys[15]: ?][values[15]: ?][children[16]: void*]`
- **Generic K/V:** `key_size` ve `val_size` çalışma zamanında bilinir; runtime byte-level memcpy ile saklar
- **Comparator dispatch:** `key_kind` enum
  ```cpp
  enum class BTreeKeyKind : int8_t {
      I8 = 0, I16 = 1, I32 = 2, I64 = 3,
      U8 = 4, U16 = 5, U32 = 6, U64 = 7,
      String = 8,  // const char* (null-terminated, lexicographic strcmp)
      Bytes = 9    // raw bytes via memcmp (caller responsibility)
  };
  ```
  Her kind için tip-doğru `compare(const void*, const void*)` static fonksiyonu; B-tree dispatch'i bu pointer üzerinden.

- **API (`extern "C"`):**
  ```cpp
  // Create. Returns opaque handle as int64_t (caller stores in Liva i64).
  // Owner must call liva_btree_free.
  extern "C" int64_t liva_btree_new(int8_t key_kind, int64_t key_size, int64_t val_size);

  // Destroy.
  extern "C" void liva_btree_free(int64_t handle);

  // Insert or update. Copies key_size bytes of key, val_size bytes of value.
  extern "C" void liva_btree_insert(int64_t handle, const void *key, const void *value);

  // Lookup. Returns pointer into internal storage (read-only) or nullptr.
  extern "C" const void *liva_btree_get(int64_t handle, const void *key);

  // Membership check.
  extern "C" int8_t liva_btree_contains(int64_t handle, const void *key);

  // Remove. Returns 1 if removed, 0 if not found.
  extern "C" int8_t liva_btree_remove(int64_t handle, const void *key);

  // Element count.
  extern "C" int64_t liva_btree_size(int64_t handle);
  ```

- **Memory model:**
  - `liva_btree_new` `malloc`'lar `BTree` struct + initial root node; handle iade eder
  - `liva_btree_free` rekürsif olarak tüm node'ları free eder
  - Liva-side ownership: stdlib struct destructor'unda `liva_btree_free` çağrısı (deinit method'u veya manual `free()` method)

- **Yaklaşık LOC:** ~400-500 satır C++. Standart CLRS B-tree implementasyonu.

### 2. Stdlib: `collections::btree`

Yeni dosya: `stdlib/collections/btree.liva`

```liva
// collections::btree — Ordered key/value maps backed by runtime B-tree.
//
// Current concrete types (P1-8 alt-spec 3):
//   - BTreeMapI64I64: i64 keys, i64 values
//   - BTreeMapStrI64: string keys, i64 values
//
// Generic BTreeMap<K, V> and BTreeSet are future-work (alt-spec 4).

extern "C" func liva_btree_new(key_kind: i8, key_size: i64, val_size: i64) -> i64
extern "C" func liva_btree_free(handle: i64)
extern "C" func liva_btree_insert(handle: i64, key: &i8, value: &i8)
extern "C" func liva_btree_get(handle: i64, key: &i8) -> &i8
extern "C" func liva_btree_contains(handle: i64, key: &i8) -> i8
extern "C" func liva_btree_remove(handle: i64, key: &i8) -> i8
extern "C" func liva_btree_size(handle: i64) -> i64

// ---------------------------------------------------------------------------
// BTreeMapI64I64 — i64 keyed, i64 valued ordered map.
// ---------------------------------------------------------------------------

pub struct BTreeMapI64I64 {
    var handle: i64
}

impl BTreeMapI64I64 {
    pub static func new() -> BTreeMapI64I64 {
        // key_kind = 3 (I64), key_size = 8, val_size = 8
        let h = liva_btree_new(3, 8, 8)
        return BTreeMapI64I64 { handle: h }
    }

    pub func insert(ref mut self, key: i64, value: i64) {
        var k: i64 = key
        var v: i64 = value
        liva_btree_insert(self.handle, &k as &i8, &v as &i8)
    }

    pub func get(ref self, key: i64) -> i64? {
        var k: i64 = key
        let p = liva_btree_get(self.handle, &k as &i8)
        if p == nil { return nil }
        return *(p as &i64)
    }

    pub func contains(ref self, key: i64) -> bool {
        var k: i64 = key
        return liva_btree_contains(self.handle, &k as &i8) != 0
    }

    pub func remove(ref mut self, key: i64) -> bool {
        var k: i64 = key
        return liva_btree_remove(self.handle, &k as &i8) != 0
    }

    pub func size(ref self) -> i64 {
        return liva_btree_size(self.handle)
    }

    pub func isEmpty(ref self) -> bool {
        return self.size() == 0
    }

    pub func free(ref mut self) {
        if self.handle != 0 {
            liva_btree_free(self.handle)
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
        // key_kind = 8 (String), key_size = 8 (pointer size), val_size = 8
        let h = liva_btree_new(8, 8, 8)
        return BTreeMapStrI64 { handle: h }
    }

    pub func insert(ref mut self, key: string, value: i64) {
        var k: string = key
        var v: i64 = value
        liva_btree_insert(self.handle, &k as &i8, &v as &i8)
    }

    pub func get(ref self, key: string) -> i64? {
        var k: string = key
        let p = liva_btree_get(self.handle, &k as &i8)
        if p == nil { return nil }
        return *(p as &i64)
    }

    pub func contains(ref self, key: string) -> bool {
        var k: string = key
        return liva_btree_contains(self.handle, &k as &i8) != 0
    }

    pub func remove(ref mut self, key: string) -> bool {
        var k: string = key
        return liva_btree_remove(self.handle, &k as &i8) != 0
    }

    pub func size(ref self) -> i64 {
        return liva_btree_size(self.handle)
    }

    pub func isEmpty(ref self) -> bool {
        return self.size() == 0
    }

    pub func free(ref mut self) {
        if self.handle != 0 {
            liva_btree_free(self.handle)
            self.handle = 0
        }
    }
}
```

**Notlar:**
- `&i8` opaque pointer için kullanılan placeholder; Liva FFI'da raw pointer için doğru tipi (`&void` veya `RawPtr`) yoksa `&i8` cast'i ile gidilir. Mevcut stdlib FFI desenlerine bakılıp uyumlu hale getirilecek.
- String için key_size = 8 = pointer size (runtime tarafında ilk 8 byte'ı `const char *` olarak okuyacak).
- `free()` method'u manuel; Liva'nın destructor desteği varsa `deinit` kullanılır. (Mevcut sqlite wrapper'ına bakılır.)
- Cast syntax (`as &i8`, `&i64`) Liva'da destekleniyor (sqlite wrapper kullanıyor).

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
