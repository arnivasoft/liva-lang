# P1-8 Hash Family (Alt-Spec 2): Built-in Hashable Conformance

**Status:** Draft → Approved
**Date:** 2026-05-10
**Predecessor:** Alt-Spec 1 (`docs/superpowers/specs/2026-05-10-hash-family-design.md`, commit `70ea425`)
**Successor:** Alt-Spec 3 (HashMap/HashSet runtime — future-work)

---

## Goal

Liva primitif tiplerinin (i8/i16/i32/i64/u8/u16/u32/u64/string/bool/Char) `Hashable` protokolüne otomatik olarak conform etmesini sağlamak ve `42.hash()`, `"foo".hash()` gibi method çağrılarının çalışmasını sağlamak.

Bu alt-spec'in çıktıları:

1. Runtime'da `extern "C"` Hash wrapper fonksiyonları: `liva_hash_i64`, `liva_hash_i32`, `liva_hash_string`, `liva_hash_bool`, `liva_hash_char`.
2. Sema: `protocolConformances_["Hashable"]` listesi tüm primitif tipleri içerir.
3. Sema: TypeChecker primitif `.hash()` method çağrılarını `i64` dönüş tipiyle resolve eder.
4. IRGen: `expr.hash()` primitif receiver üzerinde uygun runtime fonksiyonuna route edilir.
5. Yeni testler: 2 Sema + 2 runtime testi.

Bu alt-spec'in **dışında** kalan:

- `HashMap<K,V>` / `HashSet<T>` runtime tipleri (alt-spec 3 → built-in `Map`/`Set` zaten yeterli; future-work).
- Kullanıcı struct'larının built-in tiplerin `hash()`'ini composing ederek hash üretmesi (manuel olarak yapılabilir; otomatik derive yok).
- Floating-point tipler için (`f32`, `f64`) Hashable — NaN/normalization sorunları nedeniyle bilinçli olarak dışarıda.
- Pointer/reference tipler için Hashable.
- Hashable + Equatable contract enforcement.

## Arka Plan

Alt-spec 1 sonrası mevcut durum:
- `stdlib/core/hashable.liva` — `Hashable { func hash() -> i64 }` protokolü deklare ediliyor (compiler tarafından yüklenmiyor).
- `protocolConformances_["Hashable"]` haritası boş.
- Runtime'da static `fnv1a_bytes` ve `fnv1a_string` helper'ları mevcut (`stdlib/runtime/runtime.cpp:1093-1111`); henüz `extern "C"` ile expose edilmediler.
- `42.hash()` çağrısı şu an Sema'da `err_undeclared_method` üretir.

## Tasarım

### 1. Runtime: extern "C" Hash Wrappers

`stdlib/runtime/runtime.cpp` — mevcut FNV-1a helper'larının altına eklenir (yaklaşık satır 1120):

```cpp
// === Hashable runtime entry points (P1-8 alt-spec 2) ===
// All wrappers return int64_t (Liva i64). Signed return reflects the
// language-level Hashable.hash() -> i64 signature; bit pattern is the
// raw 64-bit FNV-1a result.

extern "C" int64_t liva_hash_i64(int64_t value) {
    return (int64_t)fnv1a_bytes(&value, sizeof(value));
}

extern "C" int64_t liva_hash_i32(int32_t value) {
    int64_t widened = (int64_t)value;
    return (int64_t)fnv1a_bytes(&widened, sizeof(widened));
}

extern "C" int64_t liva_hash_string(const char *str) {
    if (!str) return 0;
    return (int64_t)fnv1a_string(str);
}

extern "C" int64_t liva_hash_bool(int8_t value) {
    uint8_t byte = value ? 1 : 0;
    return (int64_t)fnv1a_bytes(&byte, 1);
}

extern "C" int64_t liva_hash_char(int32_t codepoint) {
    return (int64_t)fnv1a_bytes(&codepoint, sizeof(codepoint));
}
```

**Tasarım notları:**
- Tüm integer tipler (i8/i16/i32/i64/u8/u16/u32/u64) `liva_hash_i64` üzerine route edilir — IRGen'de sign/zero extend ile 64-bit'e genişletilir; FNV-1a tüm 8 byte üzerinde hesaplar. Bu, `i32(5).hash() == i64(5).hash()` sağlar (tutarlılık).
- `liva_hash_i32` ayrı bir entry olarak verilir çünkü zero-extend semantiği IRGen tarafında daha temiz görünür; FUTURE: tek `liva_hash_i64` yeterli olabilir.
- `bool` için 1 byte hash; `i8` ile aynı hash değerini üretir (sorun değil).
- `Char` Liva'da 32-bit codepoint olduğu için `i32` ile aynı hash değerini üretir (sorun değil).
- `null` string için 0 döner (defensive; gerçek Liva string'leri null olmayacak).

### 2. Sema: Conformance Registration

`src/Sema/TypeChecker.cpp` — `registerBuiltins()` sonuna eklenir (Iterator entry'lerinden sonra):

```cpp
// P1-8 alt-spec 2: built-in types conform to Hashable.
// hash() dispatch at IRGen routes to liva_hash_* runtime functions.
for (const char *name : {"i8", "i16", "i32", "i64",
                          "u8", "u16", "u32", "u64",
                          "string", "bool", "Char"}) {
    protocolConformances_["Hashable"].push_back(name);
}
```

### 3. Sema: Primitive `.hash()` Method Resolution

TypeChecker'da `visitCallExpr` (veya method resolution path'i) primitif receiver tiplerinde `hash()` çağrısını `i64` dönüş tipiyle kabul etmelidir.

**Yaklaşım:** Mevcut `typeMethodReturnTypes_` haritası veya benzer bir lookup tablosu var; orada `i32::hash` → `i64`, `string::hash` → `i64` vb. kaydı yapılır. Mevcut altyapı incelenmeli; gerekirse method resolution path'inde primitif tip + "hash" özel case'i eklenir.

**Fallback yaklaşım:** Method resolver'da bir kademe önce — eğer receiver tip primitif ise ve method ismi "hash" ise, çağrı tipi `i64` olarak işaretlenip diagnostic atılmaz; gerçek implementation IRGen tarafında halledilir. Bu pattern halihazırda `unwrap` gibi metotlar için kullanılıyor olabilir.

### 4. IRGen: Method Dispatch

`src/IR/IRGenCall.cpp` — `visitCallExpr`'deki MemberExpr dispatch dalına yeni branch (mevcut `unwrap` benzeri branch'lerin yanına, yaklaşık satır 130 civarı):

```cpp
// Primitive .hash() dispatch (P1-8 alt-spec 2)
if (methodName == "hash" && node->getArgs().empty()) {
    auto *recv = visit(memberExpr->getObject());
    if (!recv) return nullptr;
    auto *recvTy = recv->getType();

    // String (i8*) → liva_hash_string
    if (recvTy->isPointerTy()) {
        auto *fn = getOrPanic("liva_hash_string");
        return builder_->CreateCall(fn, {recv}, "hash.str");
    }
    // Integer (i1/i8/i16/i32/i64) → liva_hash_i64 with sign-extend
    if (recvTy->isIntegerTy()) {
        auto *bits = recvTy->getIntegerBitWidth();
        llvm::Value *widened = recv;
        if (bits == 1) {
            // bool → liva_hash_bool
            auto *fn = getOrPanic("liva_hash_bool");
            auto *zextI8 = builder_->CreateZExt(recv, builder_->getInt8Ty(), "bool.zext");
            return builder_->CreateCall(fn, {zextI8}, "hash.bool");
        }
        if (bits == 32) {
            // Char and i32 both use liva_hash_i32 (codepoint-as-i32)
            auto *fn = getOrPanic("liva_hash_i32");
            return builder_->CreateCall(fn, {recv}, "hash.i32");
        }
        if (bits < 64) {
            widened = builder_->CreateSExt(recv, builder_->getInt64Ty(), "int.sext");
        }
        auto *fn = getOrPanic("liva_hash_i64");
        return builder_->CreateCall(fn, {widened}, "hash.i64");
    }
}
```

**Notlar:**
- Bool/Char ayrı entry'leri kullanılır (deterministic farklılaşmak için değil, FNV-1a girdi byte sayısı doğal olarak değişiyor).
- Method dispatch sıralaması: önce user-defined `hash()` (impl Point: Hashable { func hash() -> i64 }) kontrol edilir, sonra primitif fallback. Mevcut MemberExpr dispatch sırası buna izin veriyor; user-defined struct method'u önce çözülüyor.
- Forward declaration: `liva_hash_*` fonksiyonları `runtime.h` veya symbol-registry'de kayıtlı olmalı. `getOrPanic` ihtiyaca göre dinamik kayıt yapıyor olabilir; doğrulanmalı.

### 5. Diagnostics

Yeni diagnostic eklenmez. Primitif `.hash()` artık geçerli olduğu için var olan `err_undeclared_method` üretilmez.

### 6. Test Stratejisi

#### 6.1 HashableProtocolTest genişletmesi

`tests/unit/HashableProtocolTest.cpp` (Sema):

**Test A — BuiltinPrimitivesAreHashable:**
```cpp
TEST_F(HashableProtocolTest, BuiltinPrimitivesAreHashable) {
    auto result = check(R"--(
        protocol Hashable {
            func hash() -> i64
        }
        func bucket<T>(k: T) -> i64 where T: Hashable {
            return k.hash()
        }
        func main() {
            let a = bucket(42)
            let b = bucket("foo")
            let c = bucket(true)
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

**Test B — PrimitiveHashCallReturnsI64:**
```cpp
TEST_F(HashableProtocolTest, PrimitiveHashCallReturnsI64) {
    auto result = check(R"--(
        func main() {
            let x: i64 = (42).hash()
            let y: i64 = "abc".hash()
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

#### 6.2 RuntimeExecTest

`tests/unit/RuntimeExecTest.cpp`:

**Test C — HashI64StableValue:**

Runtime FNV-1a (offset basis 0xcbf29ce484222325, prime 0x100000001b3) için `i64(5).hash()` deterministic bir değer üretmeli. Test, çıktının iki kez aynı olduğunu doğrular.

```cpp
TEST_F(RuntimeExecTest, HashI64StableValue) {
    auto exitCode = runProgram(R"--(
        func main() {
            let x: i64 = 5
            let h1 = x.hash()
            let h2 = x.hash()
            if h1 == h2 {
                print("equal")
            } else {
                print("differ")
            }
        }
    )--");
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(capturedOutput(), "equal\n");
}
```

**Test D — HashStringDifferentInputs:**

`"foo".hash() != "bar".hash()` doğrulanır.

#### 6.3 CMake & Symbol Registry

- `liva_hash_*` fonksiyonları IRGen tarafından `getOrPanic` ile çözüldüğü için linkage zaten varsayılan symbol resolution yoluyla çalışmalı.
- Eğer `getOrPanic` kayıtlı olmayan fonksiyonlarda panic ederse, fonksiyon prototipleri IRGen başlatma kodunda manuel olarak eklenir (`runtimeFunctions_` benzeri bir registry varsa).

## Dosya Manifest

| Eylem  | Dosya                                            | Boyut         |
| ------ | ------------------------------------------------ | ------------- |
| Modify | `stdlib/runtime/runtime.cpp`                     | +5 fonksiyon, ~30 LOC |
| Modify | `src/Sema/TypeChecker.cpp`                       | +Hashable conformance entry + method resolution path |
| Modify | `src/IR/IRGenCall.cpp`                           | +hash dispatch branch, ~40 LOC |
| Modify | `tests/unit/HashableProtocolTest.cpp`            | +2 test |
| Modify | `tests/unit/RuntimeExecTest.cpp`                 | +2 test |

## Doğrulama

1. Tüm 2270+ mevcut test geçer (`ctest --test-dir build-clang -j 1`).
2. Yeni 4 test (2 sema + 2 runtime) geçer.
3. `(42).hash()` çağrısı gibi end-to-end runtime senaryolar deterministic değer döner.

## Riskler & Hafifletmeler

| Risk                                              | Hafifletme |
| ------------------------------------------------- | ---------- |
| Method dispatch sıralaması user-defined `hash()`'i bozar | Mevcut MemberExpr branch'leri user-defined method'u önce çözer; yeni primitive branch sadece receiver primitif ise tetiklenir. |
| `liva_hash_*` symbol resolution başarısız | Runtime build'inde `extern "C"` ile expose; IRGen `getOrPanic` veya manuel `runtimeFunctions_` register'ı kullanır. |
| TypeChecker primitif method resolution mevcut altyapı ile bağdaşmaz | Adapter olarak `typeMethodReturnTypes_` veya benzer mevcut harita kullanılır; gerekirse ad-hoc receiver-type check. |
| Bool/Char hash'leri sürpriz değerler üretir | Test'lerle deterministic-stable doğrulanır; future-work HashMap implementation'ında problem olursa ayrı normalization tasarlanır. |

## Tahmini Süre

~3 gün (subagent-driven, 4 task):
1. Runtime `liva_hash_*` wrappers
2. Sema conformance + primitive `.hash()` resolution
3. IRGen method dispatch
4. Sema + runtime testleri + final regression

## Sonraki Adımlar

Alt-spec 2 onaylandıktan sonra:
- `writing-plans` skill → `docs/superpowers/plans/2026-05-10-hashable-builtin.md`
- `subagent-driven-development` skill ile uygulama

İleride (alt-spec 3):
- `HashMap<K,V>` ve `HashSet<T>` runtime tipleri (Map/Set built-in'leri zaten yeterli olduğundan opsiyonel)
- Floating-point Hashable (NaN-canonicalization ile)
- Tuple/Array Hashable derivation
