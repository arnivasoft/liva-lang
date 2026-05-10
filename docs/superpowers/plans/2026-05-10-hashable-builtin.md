# P1-8 Hash Family (Alt-Spec 2): Built-in Hashable — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Liva primitif tiplerinin (i8/i16/i32/i64/u8/u16/u32/u64/string/bool/Char) Hashable protokolüne otomatik conform etmesi ve `42.hash()`/`"foo".hash()` çağrılarının end-to-end çalışması.

**Architecture:** Runtime `extern "C"` `liva_hash_*` wrapper'ları + Sema conformance entries + TypeChecker primitif `.hash()` method resolution + IRGen MemberExpr dispatch branch.

**Tech Stack:** C++20, LLVM 21, GoogleTest, CMake.

**Spec:** `docs/superpowers/specs/2026-05-10-hashable-builtin-design.md` (commit `f66ff75`).

---

## Task 1: Runtime `liva_hash_*` Wrappers

**Files:**
- Modify: `stdlib/runtime/runtime.cpp` (yaklaşık satır 1120, mevcut FNV-1a helper'larının altı)

- [ ] **Step 1: Mevcut FNV-1a helper'larını bul**

`stdlib/runtime/runtime.cpp:1093-1119` aralığında `fnv1a_bytes`, `fnv1a_string`, `compute_hash` static fonksiyonları var. Bunların hemen altına yeni wrapper'lar eklenecek.

- [ ] **Step 2: 5 wrapper fonksiyonunu ekle**

`compute_hash` fonksiyonunun altına (1120 civarı) ekle:

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

- [ ] **Step 3: Build runtime**

```
cmake --build build-clang --parallel
```

Expected: BUILD SUCCESS.

- [ ] **Step 4: Sanity-check — runtime tests hâlâ geçiyor**

```
ctest --test-dir build-clang -R RuntimeExecTest -j 1 --output-on-failure 2>&1 | tail -5
```

Expected: All existing RuntimeExecTest tests still PASS (yeni wrapper'lar IRGen tarafından henüz çağrılmıyor).

- [ ] **Step 5: Commit**

```bash
git add stdlib/runtime/runtime.cpp
git commit -m "runtime: add extern C liva_hash_* wrappers (P1-8 alt-spec 2)"
```

---

## Task 2: Sema — Built-in Hashable Conformance + Method Resolution

**Files:**
- Modify: `src/Sema/TypeChecker.cpp`

- [ ] **Step 1: Hashable conformance entries ekle**

`src/Sema/TypeChecker.cpp` içinde `registerBuiltins()` fonksiyonunun sonuna ekle (Iterator entry'lerinin altına, `protocolConformances_["AsyncIterator"].push_back("Generator");` satırından sonra):

```cpp
    // P1-8 alt-spec 2: built-in types conform to Hashable. The hash() method
    // is dispatched at IRGen to runtime liva_hash_* functions; here we only
    // register conformance so generic `where T: Hashable` accepts primitives.
    for (const char *name : {"i8", "i16", "i32", "i64",
                              "u8", "u16", "u32", "u64",
                              "string", "bool", "Char"}) {
        protocolConformances_["Hashable"].push_back(name);
    }
```

- [ ] **Step 2: TypeChecker'da primitif `.hash()` method resolution path'ini bul**

Grep:
```
grep -n "MemberExpr\|visitMemberExpr\|method.*resolution" src/Sema/TypeChecker.cpp | head -30
```

Method resolution genelde `visitCallExpr` veya `visitMemberExpr` içinde. Receiver'ın primitif tip olduğu durumda metot adı "hash" ise `i64` döner ve `err_undeclared_method` üretilmez.

- [ ] **Step 3: Primitive `.hash()` resolution branch'ini ekle**

Mevcut method resolution mantığının primitif tip kontrolü olan kısmına (string method'ları gibi) yeni bir case ekle:

- Receiver type kind ∈ {I8, I16, I32, I64, U8, U16, U32, U64, String, Bool, Char}
- Method name == "hash"
- Args boş (0 arg)
- Return type → I64

Implementation detay: kod tabanının deseni neyse onu takip et. Eğer `typeMethodReturnTypes_` haritası varsa register et; yoksa method resolver fonksiyonunda inline check.

**Eğer ayrı bir method resolution helper'ı yoksa**, en pragmatik yaklaşım: visitMemberExpr veya visitCallExpr içinde method-name == "hash" + receiver tip primitif kontrolü ekleyip dönüş tipini i64 olarak işaretlemek.

- [ ] **Step 4: Build et**

```
cmake --build build-clang --parallel
```

Expected: BUILD SUCCESS.

- [ ] **Step 5: Sema unit testlerini çalıştır**

```
ctest --test-dir build-clang -R "sema_test|hashable_protocol_test" -j 1 --output-on-failure 2>&1 | tail -10
```

Expected: All existing sema tests PASS. New Hashable tests yok henüz; ama mevcut hashable_protocol_test'in 3 testi hâlâ geçer.

- [ ] **Step 6: Commit (tek başına; testler Task 4'te)**

```bash
git add src/Sema/TypeChecker.cpp
git commit -m "sema: built-in Hashable conformance + primitive .hash() resolution"
```

---

## Task 3: IRGen — Primitive `.hash()` Method Dispatch

**Files:**
- Modify: `src/IR/IRGenCall.cpp`

- [ ] **Step 1: visitCallExpr MemberExpr dispatch alanını bul**

`src/IR/IRGenCall.cpp` satır 74'te `visitCallExpr` MemberExpr branch başlıyor. Mevcut özel-case'ler: `Result.ok/err`, `r.unwrap()`, `File.open(...)`. Yeni branch bunların yanına eklenir.

- [ ] **Step 2: Primitive `.hash()` dispatch branch'ini ekle**

`File.open` branch'inden sonra (line ~150 civarı, ama dosyada yer açan yer neredeyse herhangi bir noktaya — sadece mevcut struct method dispatch'ten ÖNCE olmalı) ekle:

```cpp
        // Primitive .hash() dispatch (P1-8 alt-spec 2)
        if (methodName == "hash" && node->getArgs().empty()) {
            auto *recv = visit(memberExpr->getObject());
            if (recv) {
                auto *recvTy = recv->getType();
                // String (ptr) → liva_hash_string
                if (recvTy->isPointerTy()) {
                    auto *fn = getOrPanic("liva_hash_string");
                    return builder_->CreateCall(fn, {recv}, "hash.str");
                }
                // Integer (i1/i8/i16/i32/i64) → liva_hash_*
                if (recvTy->isIntegerTy()) {
                    auto bits = recvTy->getIntegerBitWidth();
                    if (bits == 1) {
                        auto *fn = getOrPanic("liva_hash_bool");
                        auto *zextI8 = builder_->CreateZExt(recv, builder_->getInt8Ty(), "bool.zext");
                        return builder_->CreateCall(fn, {zextI8}, "hash.bool");
                    }
                    if (bits == 32) {
                        auto *fn = getOrPanic("liva_hash_i32");
                        return builder_->CreateCall(fn, {recv}, "hash.i32");
                    }
                    llvm::Value *widened = recv;
                    if (bits < 64) {
                        widened = builder_->CreateSExt(recv, builder_->getInt64Ty(), "int.sext");
                    } else if (bits > 64) {
                        widened = builder_->CreateTrunc(recv, builder_->getInt64Ty(), "int.trunc");
                    }
                    auto *fn = getOrPanic("liva_hash_i64");
                    return builder_->CreateCall(fn, {widened}, "hash.i64");
                }
            }
        }
```

**Dikkat:** Bu branch user-defined struct method dispatch'ten ÖNCE eklenmeli. Eğer kullanıcı `struct Point { var v: i32 } impl Point: Hashable { func hash() -> i64 { ... } }` yazarsa, primitive branch tetiklenmez çünkü receiver Point pointer-to-struct olur (struct alloca pointer'ı). String case'i `isPointerTy()` ile yakalıyor — bu noktada struct receiver'lar da pointer olabilir! Bu nedenle:

**Düzeltme:** Önce check etmek istediğimiz primitif receiver tipi `string` (`i8*` veya benzeri). Struct pointer'ı da `isPointerTy()` true döner. Bunu disambiguate etmek için Sema tarafında receiver tipi bilgisi gerekir — IRGen value type yetmez.

Pratik çözüm: Member call'un Sema tarafında resolve edilmiş receiver type'ını AST node'da taşımak (eğer mevcut altyapı buna izin veriyorsa). Veya bu branch'i mevcut struct method dispatch'inden SONRA eklemek — struct method dispatch fail ederse primitive fallback olarak.

**En sağlam yaklaşım:** Primitive branch'i mevcut struct method dispatch'ten SONRA ekle, fallback olarak. Çünkü:
- Struct receiver için Sema önce user-defined `hash()` method'unu bulur ve normal dispatch path'i çalışır.
- Primitive receiver için Sema bir method bulamaz; IRGen'in normal dispatch path'i fallback'e düşer → primitive branch tetiklenir.

Bu durumda branch ÇOK SONRA, normal method dispatch flow'unun sonunda — fallback olarak. Tam yer için dosyayı oku ve regular method dispatch'in nerede bittiğini bul; primitive branch oraya yerleştir.

**Alternatif:** IRGen'de receiver'ın Liva tip bilgisi varsa (örn. AST'da resolved type cached) doğrudan onu kullan. `memberExpr->getObject()->getResolvedType()` benzeri varsa onu sorgula.

- [ ] **Step 3: Build et**

```
cmake --build build-clang --parallel
```

Expected: BUILD SUCCESS. Link error olursa `liva_hash_*` semboller registry'ye eklenmemiş demektir; ondan emin ol.

- [ ] **Step 4: Smoke test — basit bir runtime fonksiyon**

Geçici test snippet'i:
```liva
func main() {
    let x: i64 = 5
    let h = x.hash()
    print(h)
}
```

Manuel olarak `livac` ile derleyip çalıştır; numerik bir değer basmalı (0 değil).

- [ ] **Step 5: Commit**

```bash
git add src/IR/IRGenCall.cpp
git commit -m "irgen: primitive .hash() method dispatch to liva_hash_* runtime"
```

---

## Task 4: Sema + Runtime Testleri + Final Regression

**Files:**
- Modify: `tests/unit/HashableProtocolTest.cpp`
- Modify: `tests/unit/RuntimeExecTest.cpp`
- Modify: `status.md`

- [ ] **Step 1: HashableProtocolTest'e 2 yeni test ekle**

`tests/unit/HashableProtocolTest.cpp` dosyasının sonuna (son `}` blok kapanışından önce) ekle:

```cpp
// ---------------------------------------------------------------------------
// Built-in primitives (i32, string, bool, ...) conform to Hashable
// implicitly. `where T: Hashable` accepts them in generic constraints.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Calling .hash() on a primitive resolves with i64 return type.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, PrimitiveHashCallReturnsI64) {
    auto result = check(R"--(
        func main() {
            let x: i64 = 42
            let h: i64 = x.hash()
            let s = "abc"
            let g: i64 = s.hash()
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

- [ ] **Step 2: RuntimeExecTest'e 2 yeni runtime testi ekle**

`tests/unit/RuntimeExecTest.cpp` — diğer Generator/runtime testlerinin yanına ekle. Test fixture'ı dosyanın baş kısmındaki `runProgram` veya benzer helper'a uygun olmalı.

```cpp
TEST_F(RuntimeExecTest, HashI64StableValue) {
    std::string output;
    int exitCode = runLivaProgram(R"--(
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
    )--", output);
    EXPECT_EQ(exitCode, 0);
    EXPECT_NE(output.find("equal"), std::string::npos);
}

TEST_F(RuntimeExecTest, HashStringDifferentInputs) {
    std::string output;
    int exitCode = runLivaProgram(R"--(
        func main() {
            let h1 = "foo".hash()
            let h2 = "bar".hash()
            if h1 == h2 {
                print("collision")
            } else {
                print("different")
            }
        }
    )--", output);
    EXPECT_EQ(exitCode, 0);
    EXPECT_NE(output.find("different"), std::string::npos);
}
```

**Önemli:** `RuntimeExecTest` fixture'ında zaten kullanılan helper API'sini takip et (`runProgram` vs `runLivaProgram` — dosyayı oku ve eşleştir).

- [ ] **Step 3: Build et**

```
cmake --build build-clang --parallel
```

Expected: BUILD SUCCESS.

- [ ] **Step 4: Yeni testleri çalıştır**

```
ctest --test-dir build-clang -R "hashable_protocol_test|HashI64Stable|HashStringDifferent" -j 1 --output-on-failure
```

Expected: All new tests (2 Hashable + 2 Runtime) PASS.

Eğer Runtime test'leri "method not found" ile başarısız olursa, Task 2 veya 3 eksik kalmış demektir.

- [ ] **Step 5: Full regression**

```
ctest --test-dir build-clang -j 1 2>&1 | tail -5
```

Expected: 2274/2274 tests pass (2270 prior + 4 new).

- [ ] **Step 6: status.md güncelle**

`status.md` içinde:
- Test sayısı: `2270/2270` → `2274/2274`
- Yeni satır ekle (P1-8 alt-spec 1 satırının altına):

```markdown
- **P1-8 (alt-spec 2): Built-in Hashable** — primitif tipler (i8/i16/i32/i64/u8/u16/u32/u64/string/bool/Char) otomatik `Hashable` conformance; `.hash()` method dispatch FNV-1a runtime'a route edilir. `42.hash()`, `"foo".hash()` end-to-end çalışır.
```

- [ ] **Step 7: Commit**

```bash
git add tests/unit/HashableProtocolTest.cpp tests/unit/RuntimeExecTest.cpp status.md
git commit -m "test: Hashable primitive sema + runtime tests; P1-8 alt-spec 2 complete (2274)"
```

---

## Doğrulama Checklist

- [ ] Runtime'da 5 yeni `liva_hash_*` extern "C" wrapper.
- [ ] Sema `protocolConformances_["Hashable"]` 11 primitif tip içerir.
- [ ] TypeChecker `.hash()` primitif çağrısını i64 dönüş tipiyle resolve eder.
- [ ] IRGen `expr.hash()` primitif receiver'da runtime fonksiyonuna route eder.
- [ ] 4 yeni test eklendi ve geçer.
- [ ] Full `ctest --test-dir build-clang -j 1` 2274/2274 PASS.
- [ ] status.md güncellendi.
