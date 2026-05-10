# P1-8 Hash Family (Alt-Spec 1): Iterator Conformance + Hashable Declaration

**Status:** Draft → Approved
**Date:** 2026-05-10
**Predecessor:** P1-9 Iterator/AsyncIterator protocol (`docs/superpowers/specs/2026-05-10-iterator-protocol-design.md`, commit `3c09bf8`)
**Successor:** P1-8 Hash family iteration 2 (HashMap/HashSet real implementation — future-work)

---

## Goal

Tamamlanmamış P1-9 artığı olan **built-in iterable conformance**'ları kapatmak ve Hash family için zemin hazırlamak amacıyla `Hashable` protokolünü stdlib'de **declaration-only** olarak tanıtmak.

Bu alt-spec'in çıktıları:

1. `Stack<T>` ve `Queue<T>` stdlib koleksiyonları (`collections::collections`) Sema seviyesinde `Iterator` protokolüne synthetic conformance kazanır.
2. Yeni stdlib dosyası `stdlib/core/hashable.liva` `Hashable` protokolünü deklare eder.
3. Yeni unit test dosyası `HashableProtocolTest.cpp` Hashable parse/sema akışını kanıtlar; `IteratorProtocolTest.cpp` Stack/Queue conformance testleri ile genişler.

Bu alt-spec'in **dışında** kalan, açıkça future-work olan kapsam:

- `HashMap<K,V>` / `HashSet<T>` runtime implementasyonu (built-in `Map`/`Set` halen mevcut ve yeterli).
- `BTreeMap<K,V>` / `BTreeSet<T>` (ayrı alt-spec: Tree family).
- `PriorityQueue<T>` (ayrı alt-spec: Heap).
- `Hashable` protokolünün **Sema enforcement**'ı (`where K: Hashable` kullanımı altında üye tiplerin gerçekten conformance taşıyıp taşımadığını doğrulamak — declaration var, consumption sonraki iterasyonda).
- Built-in tiplerin (i32, i64, string, Char, bool) **otomatik** `Hashable` conformance'ı (runtime FNV-1a helper'ı zaten var; Hashable üzerinden expose etmek follow-up'tir).
- `for x in stack` / `for x in queue` IRGen runtime path'i (synthetic conformance sadece generic constraint `where T: Iterator` altında tip kabulünü sağlar; doğrudan for-in iteration desteği için IRGen değişikliği gerekecek ve bu kapsam dışı).

## Arka Plan

`registerBuiltins()` (`src/Sema/TypeChecker.cpp:255-258`) şu an `Range`, `Array`, `DynArray`, `Map`, `Set`, `Generator` tiplerini synthetic olarak `protocolConformances_["Iterator"]` kümesine ekliyor. `Stack<T>` ve `Queue<T>` ise compiler-built-in **değil** — `stdlib/collections/collections.liva` içinde tanımlı kullanıcı struct'ları. Bu nedenle `registerBuiltins()`'ten önce sembol tablosunda bulunmuyorlar; yine de `where T: Iterator` constraint'i altında jenerik fonksiyonlara geçirilebilmeleri için Sema'nın conformance haritasına dahil edilmeleri gerekiyor.

`Hashable` protokolü Liva'da henüz tanımlı değil. İleride `HashMap<K,V>` ve `HashSet<T>` koleksiyonları yazılırken `where K: Hashable` constraint'i için bu deklarasyon gerekecek. Bu alt-spec yalnızca dosyayı oluşturur; Sema bağlayıcı kontrol yapmaz.

## Tasarım

### 1. Iterator Conformance — Stack & Queue

**Yaklaşım:** Synthetic conformance (registerBuiltins desenini takip eder).

`src/Sema/TypeChecker.cpp:255` satırındaki liste genişletilir:

```cpp
// Built-in + stdlib iterables conform to Iterator implicitly. IRGen continues
// to use hardcoded fast paths for these types; the conformance entry exists
// so that `where T: Iterator` accepts them in generic constraints. Stack and
// Queue are stdlib structs (collections::collections); their entry here lets
// generic constraint solving recognize them even when the module is imported.
for (const char *name : {"Range", "Array", "DynArray", "Map", "Set",
                          "Generator", "Stack", "Queue"}) {
    protocolConformances_["Iterator"].push_back(name);
}
```

**Item type ataması:** `iteratorItemTypes_["Stack"]` / `iteratorItemTypes_["Queue"]` bu evrede **set edilmez**. Bunlar generic tipler (`Stack<T>`) — `T` parametresinden derive edilmeleri gerekir ve mevcut `iteratorItemTypes_` haritası `unordered_map<string, Type*>` formatında değil. P1-9 Task 5'te Range/Array/DynArray/Map için item type yine de set edilmemiş; conformance entry tek başına generic constraint çözücü tarafından yeterli görülüyor.

> **Açık karar:** for-in doğrudan `Stack<i64>` değeri üzerinde iterasyona izin verir mi sorusu bu spec'in dışındadır. Generic constraint satisfaction (`func sum<I>(it: I) where I: Iterator`) için yeterli olan tek koşul `protocolConformances_["Iterator"]` listesinde yer almaktır. for-in runtime path'i için `items: [T]` field'ı zaten DynArray üzerinden iterate edilebilir (kullanıcı `for x in stack.items` yazabilir).

### 2. Hashable Protocol — Stdlib Declaration

Yeni dosya: `stdlib/core/hashable.liva`

```liva
/// Hashable protocol — types that can produce a stable 64-bit hash.
///
/// Hash family collections (HashMap, HashSet) use this protocol as a `where`
/// constraint on key types. The contract:
///   - Two values that compare equal (`a == b`) MUST produce the same hash.
///   - Hash output is **not** guaranteed to be stable across compiler versions
///     or processes — do not persist it.
///
/// Built-in types (`i32`, `i64`, `string`, `bool`, `Char`) will gain
/// automatic conformance in a follow-up iteration; for now, user-defined
/// types must implement `hash()` explicitly.
pub protocol Hashable {
    func hash() -> i64
}
```

**Notlar:**
- `iterator.liva` gibi bu dosya da derleyici tarafından otomatik **yüklenmez**. Kullanıcı `import core::hashable` yaparsa modül loader sembolü bulur. Sema enforcement gelmediği için bu evrede import dahi opsiyoneldir.
- Method signature `func hash() -> i64` — özyinelemeli `Self` veya `inout Hasher` parametresi yok (Liva ileride generic + inout method desteğini olgunlaştırırsa Hasher pattern'ine geçilebilir).
- `pub` öneki: protocol public olduğu için modül dışı koddan kullanılabilir.

### 3. Diagnostics

Yeni diagnostic eklenmez. Mevcut `err_protocol_not_satisfied`, `err_method_signature_mismatch` ve constraint solver diagnostics sufficient.

### 4. Test Stratejisi

#### 4.1 IteratorProtocolTest genişletmesi

`tests/unit/IteratorProtocolTest.cpp` mevcut dosyasına 2 test eklenir:

**Test A: StackConformsToIterator**

```cpp
TEST_F(IteratorProtocolTest, StackConformsToIterator) {
    auto result = check(R"--(
        import collections::collections
        protocol Iterator {
            func next(mut self) -> i64?
        }
        func consume<I>(it: I) -> i64 where I: Iterator {
            return 0
        }
        func main() {
            var s: Stack<i64> = Stack.new()
            let n = consume(s)
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

**Test B: QueueConformsToIterator** — aynı şablon `Queue<i64>` ile.

#### 4.2 HashableProtocolTest (yeni dosya)

`tests/unit/HashableProtocolTest.cpp` — fixture `IteratorProtocolTest`'in birebir kopyası (aynı `check()` helper'ı). En az 3 test:

**Test 1: HashableProtocolParses** — Hashable deklarasyonu içeren snippet parse + sema hatasız geçer.

**Test 2: UserTypeConformsToHashable**

```cpp
TEST_F(HashableProtocolTest, UserTypeConformsToHashable) {
    auto result = check(R"--(
        protocol Hashable {
            func hash() -> i64
        }
        struct Point { var x: i32; var y: i32 }
        impl Point: Hashable {
            func hash() -> i64 {
                return (self.x as i64) * 1000 + (self.y as i64)
            }
        }
        func main() {
            let p = Point { x: 3, y: 4 }
            let h = p.hash()
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

**Test 3: GenericHashableConstraintAcceptsConformer**

```cpp
TEST_F(HashableProtocolTest, GenericConstraintAcceptsHashableConformer) {
    auto result = check(R"--(
        protocol Hashable { func hash() -> i64 }
        struct K { var v: i32 }
        impl K: Hashable { func hash() -> i64 { return self.v as i64 } }
        func bucket<T>(k: T) -> i64 where T: Hashable {
            return k.hash() % 16
        }
        func main() {
            let b = bucket(K { v: 42 })
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

#### 4.3 CMake entegrasyonu

`tests/CMakeLists.txt` içinde yeni test executable hedefi olarak `HashableProtocolTest.cpp` register edilir (mevcut `IteratorProtocolTest` desenini takip eder).

## Dosya Manifest

| Eylem  | Dosya                                            | Boyut tahmini   |
| ------ | ------------------------------------------------ | --------------- |
| Create | `stdlib/core/hashable.liva`                      | ~20 satır       |
| Create | `tests/unit/HashableProtocolTest.cpp`            | ~100 satır      |
| Modify | `src/Sema/TypeChecker.cpp` (line ~255)           | +2 isim         |
| Modify | `tests/unit/IteratorProtocolTest.cpp`            | +2 test, ~40 LOC|
| Modify | `tests/CMakeLists.txt`                           | +1 hedef        |

## Doğrulama

1. Tüm 2259+ mevcut test geçer (`ctest --test-dir build-clang -j 1`).
2. Yeni 5 test (Stack + Queue + 3× Hashable) geçer.
3. `collections::collections` modülü mevcut StdlibModuleTest'lerinde regresyon vermez (Stack/Queue tip kayıt yolu değişmedi, sadece conformance haritası genişledi).

## Riskler & Hafifletmeler

| Risk                                                                | Hafifletme |
| ------------------------------------------------------------------- | ---------- |
| Stack/Queue'nun item-type bilgisi eksik kalır → `where I.Item == X` kullanımı patlar | Bu alt-spec scope dışı. Future-work'te `iteratorItemTypes_` generic-aware hale getirilir. |
| Hashable declaration test'i `import core::hashable` ile çalışmayabilir (loader registry) | Test dosyaları protokolü inline yazar (mevcut IteratorProtocolTest deseni). Stdlib dosyası declaration için "future API" olarak durur. |
| `Hashable` ismi mevcut bir sembol ile çakışabilir | Grep ile doğrulanır — şu an çakışma yok. |

## Tahmini Süre

~1 gün (subagent-driven, 5 task):
1. Hashable stdlib dosyasını oluştur
2. TypeChecker.cpp synthetic conformance (Stack/Queue)
3. IteratorProtocolTest genişletmesi
4. HashableProtocolTest yaz
5. CMake + final regression

## Sonraki Adımlar

Bu alt-spec onaylandıktan sonra:
- `writing-plans` skill'i çağırılır → `docs/superpowers/plans/2026-05-10-hash-family.md`
- `subagent-driven-development` skill'i ile uygulama

İleride (P1-8 iterasyon 2):
- Built-in tiplerin (i32/i64/string/bool/Char) otomatik Hashable conformance'ı + `hash()` IRGen builtin'i
- Hashable Sema enforcement
- HashMap/HashSet generic koleksiyonları
