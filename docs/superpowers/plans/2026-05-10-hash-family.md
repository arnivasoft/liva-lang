# P1-8 Hash Family (Alt-Spec 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stack<T>/Queue<T> stdlib koleksiyonları için Sema seviyesinde Iterator conformance + stdlib/core/hashable.liva declaration-only protokol dosyası + ilgili testler.

**Architecture:** Mevcut `registerBuiltins()` synthetic conformance desenini Stack/Queue'ya genişlet. Hashable protokolü iterator.liva ile aynı stilde stdlib/core/ altında declaration olarak ekle. Test fixture deseni IteratorProtocolTest'ten kopyalanır.

**Tech Stack:** C++20, LLVM 21 backend, GoogleTest, CMake.

**Spec:** `docs/superpowers/specs/2026-05-10-hash-family-design.md` (commit `70ea425`).

---

## Task 1: Hashable stdlib protokol dosyası

**Files:**
- Create: `stdlib/core/hashable.liva`

- [ ] **Step 1: Hashable protokol dosyasını oluştur**

İçerik:

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

- [ ] **Step 2: Commit**

```bash
git add stdlib/core/hashable.liva
git commit -m "stdlib: add Hashable protocol declaration (P1-8 alt-spec 1)"
```

---

## Task 2: IteratorProtocolTest — Stack/Queue conformance testleri (failing)

**Files:**
- Modify: `tests/unit/IteratorProtocolTest.cpp` (yeni testler ekle)

- [ ] **Step 1: Stack ve Queue Iterator testlerini ekle**

`tests/unit/IteratorProtocolTest.cpp` dosyasının sonuna ekle (son `}` blok kapanışından önce):

```cpp
// ---------------------------------------------------------------------------
// Built-in stdlib Stack<T>: synthetic Iterator conformance for generic
// constraints (`where I: Iterator`). No runtime iteration here — only
// type-checker acceptance.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Built-in stdlib Queue<T>: same synthetic conformance, generic acceptance.
// ---------------------------------------------------------------------------
TEST_F(IteratorProtocolTest, QueueConformsToIterator) {
    auto result = check(R"--(
        import collections::collections
        protocol Iterator {
            func next(mut self) -> i64?
        }
        func consume<I>(it: I) -> i64 where I: Iterator {
            return 0
        }
        func main() {
            var q: Queue<i64> = Queue.new()
            let n = consume(q)
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

- [ ] **Step 2: Test'leri çalıştır ve başarısız olduğunu doğrula**

Run: `cmake --build build-clang --target iterator_protocol_test && ctest --test-dir build-clang -R iterator_protocol_test --output-on-failure`

Expected: `StackConformsToIterator` ve `QueueConformsToIterator` FAIL (çünkü Stack/Queue henüz conformance listesinde yok).

---

## Task 3: TypeChecker registerBuiltins — Stack/Queue synthetic conformance

**Files:**
- Modify: `src/Sema/TypeChecker.cpp` (line ~255)

- [ ] **Step 1: Conformance listesini genişlet**

`src/Sema/TypeChecker.cpp:252-258` satırlarındaki bloğu güncelle:

```cpp
    // Built-in + stdlib iterables conform to Iterator implicitly. IRGen
    // continues to use hardcoded fast paths for these types; the conformance
    // entry exists so that `where T: Iterator` accepts them in generic
    // constraints. Stack and Queue are stdlib structs (collections::collections);
    // their entry here lets generic constraint solving recognize them even
    // when the module is imported.
    for (const char *name : {"Range", "Array", "DynArray", "Map", "Set",
                              "Generator", "Stack", "Queue"}) {
        protocolConformances_["Iterator"].push_back(name);
    }
    protocolConformances_["AsyncIterator"].push_back("Generator");
```

- [ ] **Step 2: Build et**

Run: `cmake --build build-clang --parallel`

Expected: Build success.

- [ ] **Step 3: Iterator testlerini çalıştır ve geçtiğini doğrula**

Run: `ctest --test-dir build-clang -R iterator_protocol_test --output-on-failure`

Expected: All iterator_protocol_test tests PASS (önceki başarısız 2 test artık geçer).

- [ ] **Step 4: Commit**

```bash
git add src/Sema/TypeChecker.cpp tests/unit/IteratorProtocolTest.cpp
git commit -m "sema: Stack/Queue synthetic Iterator conformance + tests"
```

---

## Task 4: HashableProtocolTest dosyasını oluştur (failing)

**Files:**
- Create: `tests/unit/HashableProtocolTest.cpp`

- [ ] **Step 1: Test fixture + 3 test yaz**

İçerik (IteratorProtocolTest fixture'ını birebir kopyala, sadece `check()` helper ile sema runner):

```cpp
// P1-8 Hash family alt-spec 1 — Hashable protocol declaration tests.
//
// Verifies that:
//   1. The Hashable protocol shape parses + type-checks.
//   2. A user struct can `impl T: Hashable` and call its hash() method.
//   3. A generic function `where T: Hashable` accepts conformers.
//
// Note: Hashable Sema enforcement and built-in conformance are intentionally
// scoped out — this iteration only proves the declaration shape works.

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class HashableProtocolTest : public ::testing::Test {
protected:
    struct CheckResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool passed;
    };

    CheckResult check(const std::string &source) {
        CheckResult result;
        result.sm = std::make_unique<SourceManager>("test.liva", source);
        result.diag.setSourceManager(result.sm.get());
        Lexer lexer(*result.sm, result.diag);
        Parser parser(lexer, result.diag);
        result.tu = parser.parseTranslationUnit();

        if (result.diag.hasErrors()) {
            result.passed = false;
            return result;
        }

        Sema sema(result.diag);
        result.passed = sema.analyze(*result.tu);
        return result;
    }
};

// ---------------------------------------------------------------------------
// The Hashable protocol declaration shape parses and type-checks.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, HashableProtocolParses) {
    auto result = check(R"--(
        protocol Hashable {
            func hash() -> i64
        }
        func main() {}
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// A user struct implementing Hashable can call its hash() method directly.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, UserTypeConformsToHashable) {
    auto result = check(R"--(
        protocol Hashable {
            func hash() -> i64
        }
        struct Point {
            var x: i32
            var y: i32
        }
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

// ---------------------------------------------------------------------------
// A generic constraint `where T: Hashable` accepts conforming types.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, GenericConstraintAcceptsHashableConformer) {
    auto result = check(R"--(
        protocol Hashable {
            func hash() -> i64
        }
        struct K {
            var v: i32
        }
        impl K: Hashable {
            func hash() -> i64 {
                return self.v as i64
            }
        }
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

- [ ] **Step 2: CMakeLists.txt'e test executable ekle**

`tests/CMakeLists.txt` dosyasında `iterator_protocol_test` bloğunun altına ekle:

```cmake
liva_add_test(hashable_protocol_test
    unit/HashableProtocolTest.cpp
)
target_link_libraries(hashable_protocol_test PRIVATE liva_sema liva_parser)
```

- [ ] **Step 3: Build ve testleri çalıştır**

Run: `cmake --build build-clang --target hashable_protocol_test && ctest --test-dir build-clang -R hashable_protocol_test --output-on-failure`

Expected: All 3 Hashable tests PASS (mevcut Sema constraint solver desteklemeli; Hashable sade `func hash() -> i64` signature'ı standart protocol/impl yolundan geçer).

- [ ] **Step 4: Eğer testler başarısız olursa, debug et**

Olası başarısızlık nedenleri ve aksiyon:
- "operator `as` undefined": `as` cast operator standart yolu kullanıyor — başarılı olmalı; aksi halde test'lerde `as i64` yerine doğrudan `self.v` (zaten i32) → fonksiyon dönüş tipi `-> i64` için `self.v as i64`'ı koruyup parser destek doğrula.
- "method `hash` ambiguous": Liva muhtemelen bir built-in `hash` sembolü tutuyor olabilir; conflict varsa method'u `func hashCode() -> i64` olarak yeniden adlandır (hem protokolde hem impl'de).
- "protocol bound `T: Hashable` unmatched": constraint solver registerBuiltins'in protocolConformances_'ı protocol declaration'a göre eşleştiriyor; bu testlerde Hashable inline tanımlandığı için scope içinde olmalı.

Sorun çıkarsa SmsallReproducer ile en basit test'i ayrıştır ve fix'le yeniden çalıştır.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/HashableProtocolTest.cpp tests/CMakeLists.txt
git commit -m "test: add HashableProtocolTest for stdlib Hashable declaration"
```

---

## Task 5: Final regression — tüm test suite

- [ ] **Step 1: Full build**

Run: `cmake --build build-clang --parallel`

Expected: Success.

- [ ] **Step 2: Full test suite (serial)**

Run: `ctest --test-dir build-clang -j 1`

Expected: All tests PASS. Sayım önceki ~2259'dan ~2264 civarına çıkmalı (Stack/Queue Iterator 2 test + 3 Hashable test = 5 yeni test).

- [ ] **Step 3: Eğer regresyon varsa**

Specific failure'ı analiz et:
- Mevcut `StdlibModuleTest.CollectionsGenericStack` veya benzer testler conformance listesi değişiminden etkilenmemeli (Sema synthetic seed read-only).
- `IRGenTest`'lerde "Stack iteration" gibi bir test yoksa (yok) IRGen tarafı etkilenmemeli.

- [ ] **Step 4: Status doc update**

`status.md` içinde test sayısını güncelle (X/X format) ve P1-8 alt-spec 1 satırı ekle.

- [ ] **Step 5: Commit**

```bash
git add status.md
git commit -m "status: P1-8 Hash family alt-spec 1 complete (2264 tests)"
```

---

## Doğrulama Checklist

- [ ] `stdlib/core/hashable.liva` oluşturuldu.
- [ ] `src/Sema/TypeChecker.cpp` synthetic conformance listesi Stack/Queue içeriyor.
- [ ] `tests/unit/IteratorProtocolTest.cpp` +2 test (Stack, Queue).
- [ ] `tests/unit/HashableProtocolTest.cpp` 3 test (parse, user conform, generic constraint).
- [ ] `tests/CMakeLists.txt` `hashable_protocol_test` hedefi register edildi.
- [ ] Full `ctest --test-dir build-clang -j 1` geçer.
- [ ] `status.md` güncellendi.

## Notlar

- Liva'da `as` cast'ı destekleniyor (P1-9 testlerinde kullanılıyor) — Hashable test'lerinde `as i64` güvenle kullanılabilir.
- Spec açıkça built-in Hashable conformance'ı (i32, i64, string, ...) **scope dışı** bırakıyor — testlerde sadece user-defined struct'lar üzerinden Hashable kullanılıyor. `bucket<T>(k: T)` çağrısı `K` instance'ı ile yapılıyor, primitif değil.
- `iterator.liva` gibi `hashable.liva` da derleyici tarafından otomatik load edilmez. Test'ler protokolü inline tanımlıyor.
