# Dizi Eleman Tiplemesi Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dizi eleman değerleri hedef yuvanın tipine dönüştürülerek yazılsın; dönüştürülemeyecek elemanlar Sema'da temiz hatayla reddedilsin.

**Architecture:** İki katman. Katman 2 (IRGen) tüm eleman-store sitelerinde merkezi bir `coerceToElemType` yardımcısı kullanır ve sessiz-yanlış-veri sınıfını kapatır. Katman 1 (Sema) dizi literali elemanlarını birbiriyle ve `var/let` anotasyonuyla denetleyip dönüştürülemeyecek olanları reddeder, ayrıca literale `resolvedType` atar.

**Tech Stack:** C++20, LLVM 21, CMake + Ninja, GoogleTest. Derleyici kaynağı `src/`, testler `tests/unit/`.

## Global Constraints

- Build: `cmake --build build-clang` (Ninja + clang-cl). ÖNPLAN, timeout 600000 ms.
- Test: `ctest --test-dir build-clang --output-on-failure` — **SERİ koş, asla `-j` verme**. `PgRealRoundTrip`, `HttpLiveRoundTrip`, `WsLiveEchoRoundTrip` atlanır, bu normaldir.
- Tam süit şu an **2519/2519** yeşil. Her görev sonunda bu sayı korunmalı (yeni testler eklendikçe artar).
- Her commit mesajının sonunda AYNEN şu iki satır olmalı:
  ```
  Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
  ```
- Commit mesajları TÜRKÇE yazılır (mevcut geçmişle tutarlı).
- `bool` sayısal SAYILMAZ: `[i32] = [1, true]` hatadır.
- Değer-koruyan dönüşüm tablosu (spec §1.1) bağlayıcıdır; `I32 → F32` ve `U32 → F32` değer-koruyan DEĞİLDİR.
- Sema kontrolü, tipi belirlenemeyen elemanlarda (resolvedType `nullptr`) ve hedef tipi `Named`/`Generic`/`Inferred`/`AssociatedType`/`DynProtocol` olduğunda **sessiz kalır** — yanlış pozitif üretmek gerileme sayılır.
- `[dyn Protocol]` dizileri hem Sema kontrolünden hem IRGen dönüşümünden muaftır.
- clangd'nin `'liva/IR/IRGen.h' file not found` uyarıları include-path yanlış pozitifidir; yetkili olan derlemedir.

## File Structure

| Dosya | Sorumluluk | Görev |
|---|---|---|
| `include/liva/Common/DiagnosticKinds.def` | 3 yeni diagnostik | 1, 2 |
| `include/liva/IR/IRGen.h` | `coerceToElemType` bildirimi | 1 |
| `src/IR/IRGen.cpp` | `coerceToElemType` gövdesi | 1 |
| `src/IR/IRGenDecl.cpp` | site 1 (VarDecl dizi literali init'i) | 1 |
| `src/IR/IRGenExpr.cpp` | site 2/3 (bağımsız dizi literali) | 1, 4 |
| `src/IR/IRGenCallMethod.cpp` | site 4/5 (`push` yerel + üye) | 1 |
| `src/IR/IRGenCall.cpp` | site 6/7/8 (eleman ataması) | 1 |
| `include/liva/Sema/TypeChecker.h` | eleman uyum yardımcıları bildirimi | 2 |
| `src/Sema/TypeChecker.cpp` | uyum kuralı, `visitArrayLiteralExpr`, `visitVarDecl` | 2, 3 |
| `tests/unit/RuntimeExecTest.cpp` | koşum testleri | 1, 4 |
| `tests/unit/SemaTest.cpp` | diagnostik testleri | 2, 3 |
| `roadmap.md` | çözüldü işaretleme | 4 |

---

### Task 1: IRGen eleman dönüşümü

**Files:**
- Modify: `include/liva/Common/DiagnosticKinds.def` (son `DIAG` satırından sonra)
- Modify: `include/liva/IR/IRGen.h` (`dynArrayElemLLVMType` bildiriminin yanına)
- Modify: `src/IR/IRGen.cpp` (`dynArrayElemLLVMType` gövdesinin hemen ardına)
- Modify: `src/IR/IRGenDecl.cpp:1448`
- Modify: `src/IR/IRGenExpr.cpp:1149,1155`
- Modify: `src/IR/IRGenCallMethod.cpp:506,849`
- Modify: `src/IR/IRGenCall.cpp:670,702,746`
- Test: `tests/unit/RuntimeExecTest.cpp`

**Interfaces:**
- Produces: `llvm::Value *IRGen::coerceToElemType(llvm::Value *val, llvm::Type *slotTy, bool srcUnsigned = false);` — Görev 4 bunu yeniden kullanır.
- Produces: `DiagID::err_irgen_array_elem_coerce`

- [ ] **Step 1: Koşum testlerini yaz (RED)**

`tests/unit/RuntimeExecTest.cpp` dosyasında, dosyanın sonundaki
`#endif // LIVA_HAS_LLVM` satırının HEMEN ÖNÜNE ekle:

```cpp
// ============================================================
// Array element store coercion (roadmap 2.3)
// ============================================================
// Element values used to be stored into the slot with no conversion at
// all: an i32 written into an [f64] slot read back as 0.0, into an [i64]
// slot left the upper half uninitialized, and into a [u8] slot wrote
// three bytes past the element.

TEST(RuntimeExecTest, ArrayElemCoerceF64Literal) {
    auto r = compileAndRun(R"--(
        func main() {
            let a: [f64] = [1, 2]
            let x: f64 = a[0]
            let y: f64 = a[1]
            println(x)
            println(y)
        }
    )--", "arr_elem_coerce_f64");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1.000000\n2.000000\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ArrayElemCoerceF64Push) {
    auto r = compileAndRun(R"--(
        func main() {
            var a: [f64] = [1.5]
            a.push(3)
            let x: f64 = a[1]
            println(x)
        }
    )--", "arr_elem_coerce_f64_push");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "3.000000\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ArrayElemCoerceI64Literal) {
    auto r = compileAndRun(R"--(
        func main() {
            let a: [i64] = [7, 8, 9]
            let x: i64 = a[0]
            let y: i64 = a[2]
            println(x)
            println(y)
        }
    )--", "arr_elem_coerce_i64");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "7\n9\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ArrayElemCoerceU8NoNeighborDamage) {
    // A 4-byte store into a 1-byte slot wrote past the end of the buffer.
    // Two adjacent arrays make the overflow observable.
    auto r = compileAndRun(R"--(
        func main() {
            let a: [u8] = [10, 20, 30]
            let b: [u8] = [40, 50, 60]
            let x: u8 = a[2]
            let y: u8 = b[0]
            let z: u8 = b[2]
            println(x)
            println(y)
            println(z)
        }
    )--", "arr_elem_coerce_u8");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "30\n40\n60\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ArrayElemCoerceElementAssign) {
    auto r = compileAndRun(R"--(
        func main() {
            var a: [f64] = [1.5, 2.5]
            a[1] = 7
            let x: f64 = a[1]
            println(x)
        }
    )--", "arr_elem_coerce_assign");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "7.000000\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ArrayElemCoerceNoRegressionSameType) {
    // Same-type stores must be untouched: no conversion instruction, no
    // change in behavior for [i32], [string] and nested [[i32]].
    auto r = compileAndRun(R"--(
        func main() {
            let a: [i32] = [1, 2, 3]
            let s: [string] = ["ab", "cd"]
            let n: [[i32]] = [[1, 2], [3]]
            let x: i32 = a[2]
            let t: string = s[1]
            let row: [i32] = n[0]
            println(x)
            println(t)
            println(row.length)
        }
    )--", "arr_elem_coerce_same_type");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "3\ncd\n2\n") << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: Testleri koş, kırmızı olduklarını doğrula**

```
cmake --build build-clang --target runtime_exec_test
build-clang/tests/runtime_exec_test.exe --gtest_filter='*ArrayElemCoerce*'
```

Beklenen: `ArrayElemCoerceF64Literal`, `ArrayElemCoerceF64Push`,
`ArrayElemCoerceElementAssign` BAŞARISIZ (`0.000000` basılır).
`ArrayElemCoerceI64Literal`, `ArrayElemCoerceU8NoNeighborDamage`,
`ArrayElemCoerceNoRegressionSameType` bu makinede geçebilir — bunlar
gizli/latent bozuklukları ve gerilemeyi pinleyen testlerdir, kırmızı
olmaları beklenmez. Kaç testin kırmızı olduğunu rapora yaz.

- [ ] **Step 3: Diagnostiği ekle**

`include/liva/Common/DiagnosticKinds.def` — `err_generic_struct_type_args_uninferred`
satırının hemen ardına:

```
DIAG(err_irgen_array_elem_coerce, error, "internal: cannot convert array element value to element type")
```

- [ ] **Step 4: `coerceToElemType` bildirimini ekle**

`include/liva/IR/IRGen.h` — `llvm::Type *dynArrayElemLLVMType(const TypeRepr *elemRepr);`
bildiriminin hemen ardına:

```cpp
    /// Convert a value to the LLVM type of the array element slot it is
    /// about to be stored into. Returns the value unchanged when the types
    /// already match. Emits integer widening/narrowing, integer<->float and
    /// float widening/narrowing. Returns nullptr when no conversion exists
    /// (e.g. ptr <-> i32) — Sema should already have rejected that.
    /// LLVM types carry no signedness, so `srcUnsigned` tells the helper
    /// whether the SOURCE value is an unsigned quantity.
    llvm::Value *coerceToElemType(llvm::Value *val, llvm::Type *slotTy,
                                   bool srcUnsigned = false);
```

- [ ] **Step 5: `coerceToElemType` gövdesini yaz**

`src/IR/IRGen.cpp` — `dynArrayElemLLVMType` gövdesinin hemen ardına:

```cpp
llvm::Value *IRGen::coerceToElemType(llvm::Value *val, llvm::Type *slotTy,
                                      bool srcUnsigned) {
    if (!val || !slotTy) return val;
    auto *valTy = val->getType();
    if (valTy == slotTy) return val;

    if (valTy->isIntegerTy() && slotTy->isIntegerTy()) {
        unsigned from = valTy->getIntegerBitWidth();
        unsigned to = slotTy->getIntegerBitWidth();
        if (from == to) return val;
        if (from > to)
            return builder_->CreateTrunc(val, slotTy, "elem.trunc");
        // i1 (bool) is always zero-extended; it has no sign bit to carry.
        if (from == 1 || srcUnsigned)
            return builder_->CreateZExt(val, slotTy, "elem.zext");
        return builder_->CreateSExt(val, slotTy, "elem.sext");
    }
    if (valTy->isIntegerTy() && slotTy->isFloatingPointTy()) {
        if (srcUnsigned)
            return builder_->CreateUIToFP(val, slotTy, "elem.uitofp");
        return builder_->CreateSIToFP(val, slotTy, "elem.sitofp");
    }
    if (valTy->isFloatingPointTy() && slotTy->isIntegerTy())
        return builder_->CreateFPToSI(val, slotTy, "elem.fptosi");
    if (valTy->isFloatingPointTy() && slotTy->isFloatingPointTy()) {
        if (valTy->getPrimitiveSizeInBits() < slotTy->getPrimitiveSizeInBits())
            return builder_->CreateFPExt(val, slotTy, "elem.fpext");
        return builder_->CreateFPTrunc(val, slotTy, "elem.fptrunc");
    }
    return nullptr;
}
```

- [ ] **Step 6: Site 1 — VarDecl dizi literali init'i**

`src/IR/IRGenDecl.cpp`, `// Store initial elements` yorumunun altındaki döngü.
ŞU AN:

```cpp
            for (uint64_t i = 0; i < initLen; ++i) {
                auto *elemPtr = builder_->CreateGEP(elemType, dataPtr,
                    builder_->getInt64(i), "arr.init." + std::to_string(i));
                builder_->CreateStore(initVals[i], elemPtr);
            }
```

BUNUNLA DEĞİŞTİR:

```cpp
            for (uint64_t i = 0; i < initLen; ++i) {
                auto *elemPtr = builder_->CreateGEP(elemType, dataPtr,
                    builder_->getInt64(i), "arr.init." + std::to_string(i));
                auto *storedInit = coerceToElemType(initVals[i], elemType);
                if (!storedInit) {
                    diag_.report(node->getStartLoc(),
                                 DiagID::err_irgen_array_elem_coerce);
                    return nullptr;
                }
                builder_->CreateStore(storedInit, elemPtr);
            }
```

- [ ] **Step 7: Site 2/3 — bağımsız dizi literali**

`src/IR/IRGenExpr.cpp`, `visitArrayLiteralExpr` içinde. ŞU AN:

```cpp
    auto *ep0 = builder_->CreateGEP(elemType, dataPtr,
        builder_->getInt64(0), "arrlit.e0");
    builder_->CreateStore(firstVal, ep0);
    for (uint64_t i = 1; i < numElements; ++i) {
        auto *val = visit(elements[i].get());
        if (!val) continue;
        auto *ep = builder_->CreateGEP(elemType, dataPtr,
            builder_->getInt64(i), "arrlit.e" + std::to_string(i));
        builder_->CreateStore(val, ep);
    }
```

BUNUNLA DEĞİŞTİR:

```cpp
    auto *ep0 = builder_->CreateGEP(elemType, dataPtr,
        builder_->getInt64(0), "arrlit.e0");
    auto *storedFirst = coerceToElemType(firstVal, elemType);
    if (!storedFirst) {
        diag_.report(node->getStartLoc(), DiagID::err_irgen_array_elem_coerce);
        return nullptr;
    }
    builder_->CreateStore(storedFirst, ep0);
    for (uint64_t i = 1; i < numElements; ++i) {
        auto *val = visit(elements[i].get());
        if (!val) continue;
        auto *ep = builder_->CreateGEP(elemType, dataPtr,
            builder_->getInt64(i), "arrlit.e" + std::to_string(i));
        auto *stored = coerceToElemType(val, elemType);
        if (!stored) {
            diag_.report(node->getStartLoc(), DiagID::err_irgen_array_elem_coerce);
            return nullptr;
        }
        builder_->CreateStore(stored, ep);
    }
```

- [ ] **Step 8: Site 4 — `arr.push(x)` (yerel dizi)**

`src/IR/IRGenCallMethod.cpp`, `"push.tmp"` alloca'sının ardındaki store.
ŞU AN:

```cpp
                    auto *elemAlloca = createEntryBlockAlloca(func, "push.tmp",
                                                              daIt->second.elementType);
                    builder_->CreateStore(val, elemAlloca);
```

BUNUNLA DEĞİŞTİR:

```cpp
                    auto *elemAlloca = createEntryBlockAlloca(func, "push.tmp",
                                                              daIt->second.elementType);
                    auto *pushVal = coerceToElemType(val, daIt->second.elementType);
                    if (!pushVal) {
                        diag_.report(node->getStartLoc(),
                                     DiagID::err_irgen_array_elem_coerce);
                        return nullptr;
                    }
                    builder_->CreateStore(pushVal, elemAlloca);
```

- [ ] **Step 9: Site 5 — `self.field.push(x)` (üye dizi)**

`src/IR/IRGenCallMethod.cpp`, `"mpush.tmp"` alloca'sının ardındaki store.
ŞU AN:

```cpp
                    auto *elemAlloca = createEntryBlockAlloca(func, "mpush.tmp",
                                                              daInfo->elementType);
                    builder_->CreateStore(val, elemAlloca);
```

BUNUNLA DEĞİŞTİR:

```cpp
                    auto *elemAlloca = createEntryBlockAlloca(func, "mpush.tmp",
                                                              daInfo->elementType);
                    auto *mpushVal = coerceToElemType(val, daInfo->elementType);
                    if (!mpushVal) {
                        diag_.report(node->getStartLoc(),
                                     DiagID::err_irgen_array_elem_coerce);
                        return nullptr;
                    }
                    builder_->CreateStore(mpushVal, elemAlloca);
```

- [ ] **Step 10: Site 6/7/8 — eleman atamaları**

`src/IR/IRGenCall.cpp`, `visitAssignExpr` içinde ÜÇ ayrı store.

(a) Dinamik dizi (`stored` değişkenli, `elemPtr`'a yazan). ŞU AN:

```cpp
                    builder_->CreateStore(stored, elemPtr);
                }
                return val;
```

BUNUNLA DEĞİŞTİR:

```cpp
                    auto *coerced = coerceToElemType(stored,
                                                     daIt->second.elementType);
                    if (!coerced) {
                        diag_.report(node->getStartLoc(),
                                     DiagID::err_irgen_array_elem_coerce);
                        return nullptr;
                    }
                    builder_->CreateStore(coerced, elemPtr);
                }
                return val;
```

(b) Sabit dizi (`storedFixed`, `gep`'e yazan). ŞU AN:

```cpp
                builder_->CreateStore(storedFixed, gep);
```

BUNUNLA DEĞİŞTİR:

```cpp
                auto *fixedElemTy = arrayType->isArrayTy()
                                        ? arrayType->getArrayElementType()
                                        : storedFixed->getType();
                auto *coercedFixed = coerceToElemType(storedFixed, fixedElemTy);
                if (!coercedFixed) {
                    diag_.report(node->getStartLoc(),
                                 DiagID::err_irgen_array_elem_coerce);
                    return nullptr;
                }
                builder_->CreateStore(coercedFixed, gep);
```

(c) Üye dizi (`storedMem`, `elemPtr`'a yazan). ŞU AN:

```cpp
                builder_->CreateStore(storedMem, elemPtr);
                return val;
```

BUNUNLA DEĞİŞTİR:

```cpp
                auto *coercedMem = coerceToElemType(storedMem,
                                                    daInfo->elementType);
                if (!coercedMem) {
                    diag_.report(node->getStartLoc(),
                                 DiagID::err_irgen_array_elem_coerce);
                    return nullptr;
                }
                builder_->CreateStore(coercedMem, elemPtr);
                return val;
```

- [ ] **Step 11: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/runtime_exec_test.exe --gtest_filter='*ArrayElemCoerce*'
```

Beklenen: 6/6 PASSED.

- [ ] **Step 12: Tam süit**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. Bir test kırılırsa DURDUR ve raporunda hangi
testin, hangi çıktıyla kırıldığını yaz — özellikle `[u8]` kullanan
gzip/crypto stdlib testlerini kontrol et.

- [ ] **Step 13: Commit**

```bash
git add include/liva/Common/DiagnosticKinds.def include/liva/IR/IRGen.h src/IR tests/unit/RuntimeExecTest.cpp
git commit -F- <<'EOF'
fix(irgen): dizi eleman store'ları yuva tipine dönüştürülüyor

Eleman değerleri hedef yuvanın LLVM tipine hiç dönüştürülmeden
depolanıyordu: [f64] yuvasına yazılan i32 geri okunduğunda 0.0 veriyor,
[i64] yuvasının üst yarısı ilklenmemiş kalıyor, [u8] yuvasına yapılan
4 baytlık store ise elemanın 3 bayt ötesine taşıyordu.

Merkezi coerceToElemType yardımcısı 8 eleman-store sitesinde uygulandı:
VarDecl dizi literali, bağımsız dizi literali (2 store), push (yerel +
üye), eleman ataması (dinamik + sabit + üye). Dönüşüm mümkün değilse
savunma diagnostiği.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 2: Sema eleman uyum kuralı ve literal içi birleştirme

**Files:**
- Modify: `include/liva/Common/DiagnosticKinds.def`
- Modify: `include/liva/Sema/TypeChecker.h`
- Modify: `src/Sema/TypeChecker.cpp` (`visitArrayLiteralExpr`, şu an ~2539)
- Test: `tests/unit/SemaTest.cpp`

**Interfaces:**
- Consumes: hiçbir şey (Görev 1'den bağımsız).
- Produces:
  - `enum class TypeChecker::ElemAssign { Ok, Mismatch, LiteralOutOfRange };`
  - `ElemAssign TypeChecker::checkArrayElement(const TypeRepr *target, const Expr *elem) const;`
  - `DiagID::err_array_element_type_mismatch`, `DiagID::err_array_element_literal_range`
  - Görev 3 her ikisini de yeniden kullanır.

- [ ] **Step 1: Diagnostikleri ekle**

`include/liva/Common/DiagnosticKinds.def` — `err_irgen_array_elem_coerce`
satırının hemen ardına (Görev 1 bu satırı eklemiş olmalı; yoksa dosyanın
son `DIAG` satırının ardına):

```
DIAG(err_array_element_type_mismatch, error, "array element of type '%0' cannot be stored in an array of '%1'")
DIAG(err_array_element_literal_range, error, "literal %0 does not fit in array element type '%1'")
```

Eleman indeksi mesajda YOKTUR: diagnostik zaten elemanın kendi kaynak
konumunu gösterir, indeks gereksiz tekrardır.

- [ ] **Step 2: SemaTest testlerini yaz (RED)**

`tests/unit/SemaTest.cpp` dosyasının SONUNA ekle (son `TEST_F`'in ardına):

```cpp
// ============================================================
// Array literal element typing (roadmap 2.3)
// ============================================================

TEST_F(SemaTest, ArrayLiteralMixedIntAndString) {
    auto result = check(R"(
        func main() {
            let a: [i32] = [1, "a"]
            println(a.length)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_array_element_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralMixedIntAndBool) {
    auto result = check(R"(
        func main() {
            let a: [i32] = [1, true]
            println(a.length)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_array_element_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralHomogeneousIntAccepted) {
    auto result = check(R"(
        func main() {
            let a: [i32] = [1, 2, 3]
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_literal_range));
}

TEST_F(SemaTest, ArrayLiteralIntThenFloatPromotes) {
    // The candidate element type is promoted from i32 to f64; the earlier
    // integer literal is value-preserving into f64, so no error.
    auto result = check(R"(
        func main() {
            let a = [1, 2.5]
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralEmptyAccepted) {
    auto result = check(R"(
        func main() {
            var a: [i32] = []
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralStringHomogeneousAccepted) {
    auto result = check(R"(
        func main() {
            let a: [string] = ["ab", "cd"]
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
}
```

- [ ] **Step 3: Testleri koş, kırmızı olduklarını doğrula**

```
cmake --build build-clang --target sema_test
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.ArrayLiteral*'
```

Beklenen: `ArrayLiteralMixedIntAndString` ve `ArrayLiteralMixedIntAndBool`
BAŞARISIZ (diagnostik üretilmiyor). Diğer 4'ü geçer.

Not: hedef test ikilisinin adı `sema_test` değilse
`ctest --test-dir build-clang -N | grep -i sema` ile doğru hedefi bul.

- [ ] **Step 4: Yardımcıları bildir**

`include/liva/Sema/TypeChecker.h` — `bool typesCompatible(...) const;`
bildiriminin hemen ardına:

```cpp
    /// Whether an array literal element may be stored into an array whose
    /// element type is `target`.
    enum class ElemAssign { Ok, Mismatch, LiteralOutOfRange };

    /// Value-preserving numeric conversions are silent for any expression;
    /// lossy ones are allowed only for integer/float LITERAL elements whose
    /// value fits. Returns Ok when the check cannot be made (unknown element
    /// type, or a target that is generic/inferred/protocol-typed).
    ElemAssign checkArrayElement(const TypeRepr *target, const Expr *elem) const;
```

- [ ] **Step 5: Yardımcıları uygula**

`src/Sema/TypeChecker.cpp` — `visitArrayLiteralExpr`'in HEMEN ÖNÜNE
(dosya-yerel yardımcılar anonim namespace'te):

```cpp
namespace {

bool isIntegerKind(TypeRepr::Kind k) {
    switch (k) {
    case TypeRepr::Kind::I8:  case TypeRepr::Kind::I16:
    case TypeRepr::Kind::I32: case TypeRepr::Kind::I64:
    case TypeRepr::Kind::U8:  case TypeRepr::Kind::U16:
    case TypeRepr::Kind::U32: case TypeRepr::Kind::U64:
        return true;
    default:
        return false;
    }
}

bool isFloatKind(TypeRepr::Kind k) {
    return k == TypeRepr::Kind::F32 || k == TypeRepr::Kind::F64;
}

// `bool` is deliberately NOT numeric: [i32] = [1, true] is an error.
bool isNumericKind(TypeRepr::Kind k) {
    return isIntegerKind(k) || isFloatKind(k);
}

// Conversions that cannot lose information. I32 -> F32 and U32 -> F32 are
// absent on purpose: a 24-bit mantissa cannot hold every 32-bit integer.
bool isValuePreserving(TypeRepr::Kind from, TypeRepr::Kind to) {
    using K = TypeRepr::Kind;
    if (from == to) return true;
    switch (from) {
    case K::I8:  return to == K::I16 || to == K::I32 || to == K::I64 ||
                        to == K::F32 || to == K::F64;
    case K::I16: return to == K::I32 || to == K::I64 ||
                        to == K::F32 || to == K::F64;
    case K::I32: return to == K::I64 || to == K::F64;
    case K::U8:  return to == K::U16 || to == K::U32 || to == K::U64 ||
                        to == K::I16 || to == K::I32 || to == K::I64 ||
                        to == K::F32 || to == K::F64;
    case K::U16: return to == K::U32 || to == K::U64 ||
                        to == K::I32 || to == K::I64 ||
                        to == K::F32 || to == K::F64;
    case K::U32: return to == K::U64 || to == K::I64 || to == K::F64;
    case K::F32: return to == K::F64;
    default:     return false;
    }
}

bool integerLiteralFits(int64_t v, TypeRepr::Kind k) {
    using K = TypeRepr::Kind;
    switch (k) {
    case K::I8:  return v >= -128 && v <= 127;
    case K::I16: return v >= -32768 && v <= 32767;
    case K::I32: return v >= -2147483648LL && v <= 2147483647LL;
    case K::I64: return true;
    case K::U8:  return v >= 0 && v <= 255;
    case K::U16: return v >= 0 && v <= 65535;
    case K::U32: return v >= 0 && v <= 4294967295LL;
    case K::U64: return v >= 0;
    default:     return false;
    }
}

// Targets the check cannot judge: a type parameter, an inferred type, a
// trait object. Staying silent here avoids false positives inside generic
// code, where the element type is not known until monomorphization.
bool isUnjudgeableTarget(TypeRepr::Kind k) {
    using K = TypeRepr::Kind;
    return k == K::Named || k == K::Generic || k == K::Inferred ||
           k == K::AssociatedType || k == K::DynProtocol;
}

} // namespace
```

Ardından, hâlâ `visitArrayLiteralExpr`'in önünde, metodu ekle:

```cpp
TypeChecker::ElemAssign
TypeChecker::checkArrayElement(const TypeRepr *target, const Expr *elem) const {
    if (!target || !elem) return ElemAssign::Ok;
    if (isUnjudgeableTarget(target->getKind())) return ElemAssign::Ok;
    const TypeRepr *elemType = elem->getResolvedType();
    if (!elemType) return ElemAssign::Ok;
    if (isUnjudgeableTarget(elemType->getKind())) return ElemAssign::Ok;

    if (!isNumericKind(target->getKind()) || !isNumericKind(elemType->getKind()))
        return typesCompatible(target, elemType) ? ElemAssign::Ok
                                                 : ElemAssign::Mismatch;

    if (isValuePreserving(elemType->getKind(), target->getKind()))
        return ElemAssign::Ok;

    // Lossy: only literals, and only when the value fits.
    if (elem->getKind() == ASTNode::NodeKind::IntegerLiteralExpr &&
        isIntegerKind(target->getKind())) {
        auto *lit = static_cast<const IntegerLiteralExpr *>(elem);
        return integerLiteralFits(lit->getValue(), target->getKind())
                   ? ElemAssign::Ok
                   : ElemAssign::LiteralOutOfRange;
    }
    if (elem->getKind() == ASTNode::NodeKind::FloatLiteralExpr &&
        isFloatKind(target->getKind()))
        return ElemAssign::Ok;

    return ElemAssign::Mismatch;
}
```

- [ ] **Step 6: `visitArrayLiteralExpr`'i eleman birleştirmesiyle değiştir**

`src/Sema/TypeChecker.cpp` — ŞU AN:

```cpp
void TypeChecker::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    for (auto &elem : node->getElements()) {
        visit(elem.get());
    }
}
```

BUNUNLA DEĞİŞTİR:

```cpp
void TypeChecker::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    for (auto &elem : node->getElements()) {
        visit(elem.get());
    }
    // An empty literal carries no element type; leave it unresolved so the
    // existing empty-literal codegen path is untouched.
    if (node->getElements().empty()) return;

    // Unify the elements against a running candidate. The candidate is
    // promoted (never demoted) when a later element cannot be stored into
    // it but it can be stored into the later element's type — [1, 2.5]
    // starts at i32 and settles on f64.
    const TypeRepr *candidate = nullptr;
    const Expr *candidateElem = nullptr;
    for (auto &elemPtr : node->getElements()) {
        const Expr *elem = elemPtr.get();
        const TypeRepr *elemType = elem->getResolvedType();
        if (!elemType) continue;
        if (!candidate) {
            candidate = elemType;
            candidateElem = elem;
            continue;
        }
        if (checkArrayElement(candidate, elem) == ElemAssign::Ok) continue;
        if (checkArrayElement(elemType, candidateElem) == ElemAssign::Ok) {
            candidate = elemType;
            candidateElem = elem;
            continue;
        }
        diag_.report(elem->getStartLoc(),
                     DiagID::err_array_element_type_mismatch,
                     typeToString(elemType), typeToString(candidate));
    }
}
```

**DİKKAT:** Bu adımda `setResolvedType` ÇAĞRILMAZ. `resolvedType`
atanması Görev 3'ün işidir ve ayrı olarak doğrulanır.

- [ ] **Step 7: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.ArrayLiteral*'
```

Beklenen: 6/6 PASSED.

- [ ] **Step 8: Tam süit**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. Kırılan olursa DURDUR ve rapora yaz — özellikle
stdlib'de heterojen görünen ama geçerli dizi literalleri olabilir.

- [ ] **Step 9: Commit**

```bash
git add include/liva/Common/DiagnosticKinds.def include/liva/Sema/TypeChecker.h src/Sema/TypeChecker.cpp tests/unit/SemaTest.cpp
git commit -F- <<'EOF'
feat(sema): dizi literali elemanları birbiriyle birleştiriliyor

Dizi literalinin elemanları hiç karşılaştırılmıyordu: [1, "a"] Sema'dan
geçiyor ve IRGen'de tampon taşmasına dönüşüyordu.

Eleman uyum kuralı: değer-koruyan sayısal dönüşüm her ifade için sessiz,
kayıplı dönüşüm yalnız literalde ve aralık kontrolüyle, bool sayısal
sayılmaz. Tipi belirlenemeyen elemanlarda ve generik/çıkarımsal hedef
tiplerinde kural sessiz kalır.

Aday eleman tipi yükseltilebilir (asla düşürülmez): [1, 2.5] i32'den
başlayıp f64'te karar kılar.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 3: Anotasyon yönlendirmeli kontrol ve `resolvedType`

**Files:**
- Modify: `src/Sema/TypeChecker.cpp` (`visitVarDecl` ~914-916 ve
  `visitArrayLiteralExpr`)
- Test: `tests/unit/SemaTest.cpp`

**Interfaces:**
- Consumes: `TypeChecker::checkArrayElement`, `ElemAssign`,
  `DiagID::err_array_element_type_mismatch`,
  `DiagID::err_array_element_literal_range` (Görev 2).
- Produces: `ArrayLiteralExpr` düğümlerinde `resolvedType` — Görev 4 bunu okur.

Bu görev planın **en riskli adımıdır**: `resolvedType` atamak IRGen'de
`getResolvedType()` okuyan çok sayıda yolu etkileyebilir ve
`TypeChecker.cpp`'deki mevcut anotasyon-uyum kontrolünü (aşağıda) tetikler.

- [ ] **Step 1: SemaTest testlerini yaz (RED)**

`tests/unit/SemaTest.cpp` — Görev 2'nin eklediği bloğun sonuna:

```cpp
TEST_F(SemaTest, ArrayLiteralWidenIntLiteralsToI64Accepted) {
    // Value-preserving: i32 literals into an [i64]. This compiles today and
    // must keep compiling.
    auto result = check(R"(
        func main() {
            let a: [i64] = [1, 2, 3]
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralIntLiteralsToF64Accepted) {
    auto result = check(R"(
        func main() {
            let a: [f64] = [1, 2]
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralNarrowingLiteralInRangeAccepted) {
    auto result = check(R"(
        func main() {
            let a: [u8] = [1, 2, 255]
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_literal_range));
}

TEST_F(SemaTest, ArrayLiteralNarrowingLiteralOutOfRangeRejected) {
    auto result = check(R"(
        func main() {
            let a: [u8] = [1, 2, 300]
            println(a.length)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_array_element_literal_range));
}

TEST_F(SemaTest, ArrayLiteralNarrowingVariableRejected) {
    // Lossy conversions are literal-only: an i32 VARIABLE into [u8] is an
    // error, because the value is not known at compile time.
    auto result = check(R"(
        func main() {
            let n = 5
            let a: [u8] = [n]
            println(a.length)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_array_element_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralWidenVariableToI64Accepted) {
    // Value-preserving conversions are allowed for variables too, so [i64]
    // arrays stay usable (the language has no i64 literal).
    auto result = check(R"(
        func main() {
            let n = 5
            let a: [i64] = [n, 7]
            println(a.length)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_array_element_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_type_mismatch));
}

TEST_F(SemaTest, ArrayLiteralAnnotationStringVsIntRejected) {
    auto result = check(R"(
        func main() {
            let a: [string] = [1, 2]
            println(a.length)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_array_element_type_mismatch));
}
```

- [ ] **Step 2: Testleri koş, kırmızı olduklarını doğrula**

```
cmake --build build-clang --target sema_test
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.ArrayLiteral*'
```

Beklenen: `ArrayLiteralNarrowingLiteralOutOfRangeRejected`,
`ArrayLiteralNarrowingVariableRejected`,
`ArrayLiteralAnnotationStringVsIntRejected` BAŞARISIZ. Diğerleri geçer.

- [ ] **Step 3: `visitArrayLiteralExpr`'e `resolvedType` atamasını ekle**

`src/Sema/TypeChecker.cpp` — Görev 2'de yazılan gövdenin SONUNA, döngüden
sonra:

```cpp
    if (candidate)
        node->setResolvedType(std::make_unique<ArrayTypeRepr>(
            cloneTypeRepr(candidate), /*isDynamic=*/true));
}
```

`ArrayTypeRepr`'in dinamik-dizi kurucusunun tam imzası için
`include/liva/AST/Type.h`'ye bak ve orada ne varsa onu kullan; yukarıdaki
çağrı imza uyuşmazsa derlenmez.

- [ ] **Step 3b: Birleştirme döngüsünde iki hata varyantını ayır**

Görev 2 incelemesinin bulgusu: `visitArrayLiteralExpr`'in döngüsü
`checkArrayElement(...) != Ok` olan HER durumda
`err_array_element_type_mismatch` basıyor, dolayısıyla
`err_array_element_literal_range` bu yoldan hiç tetiklenmiyor.
`let u: u32 = 5` + `let a = [u, -1]` şu an "cannot be stored in an array
of 'u32'" diyor; doğrusu aralık mesajıdır.

`src/Sema/TypeChecker.cpp`, `visitArrayLiteralExpr` döngüsünün sonundaki
tek `diag_.report(...)` çağrısını bul. ŞU AN:

```cpp
        diag_.report(elem->getStartLoc(),
                     DiagID::err_array_element_type_mismatch,
                     typeToString(elemType), typeToString(candidate));
```

BUNUNLA DEĞİŞTİR:

```cpp
        if (checkArrayElement(candidate, elem) == ElemAssign::LiteralOutOfRange) {
            auto *intLit = static_cast<const IntegerLiteralExpr *>(elem);
            diag_.report(elem->getStartLoc(),
                         DiagID::err_array_element_literal_range,
                         std::to_string(intLit->getValue()),
                         typeToString(candidate));
        } else {
            diag_.report(elem->getStartLoc(),
                         DiagID::err_array_element_type_mismatch,
                         typeToString(elemType), typeToString(candidate));
        }
```

`LiteralOutOfRange` yalnız `IntegerLiteralExpr` için döndürüldüğünden
`static_cast` güvenlidir (bkz. Görev 2'nin `checkArrayElement` gövdesi).

Buna karşılık gelen test — `tests/unit/SemaTest.cpp`, Adım 1'de eklediğin
bloğun sonuna:

```cpp
TEST_F(SemaTest, ArrayLiteralUnannotatedLiteralOutOfRangeUsesRangeDiag) {
    // Unannotated literal: the candidate element type comes from the first
    // element (u32), and -1 does not fit it. The range diagnostic must win
    // over the generic mismatch one.
    auto result = check(R"(
        func main() {
            let u: u32 = 5
            let a = [u, 0 - 1]
            println(a.length)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_array_element_literal_range) ||
                hasDiag(result, DiagID::err_array_element_type_mismatch))
        << "bir hata bekleniyordu";
}
```

**DİKKAT — bu test kasıtlı olarak gevşek yazılmıştır ve öyle kalmamalıdır.**
`0 - 1` bir `BinaryExpr`'dir, `IntegerLiteralExpr` DEĞİLDİR, dolayısıyla
`LiteralOutOfRange` yoluna girmez. Testi çalıştırmadan önce Liva'da
NEGATİF bir tamsayı literalinin nasıl yazıldığını belirle (`-1` tek bir
`IntegerLiteralExpr` mi üretiyor, yoksa unary/binary ifade mi?) —
`build-clang/livac.exe probe.liva --dump-ast` ile bak. Negatif literal
tek bir `IntegerLiteralExpr` ise testi `[u, -1]` yaz ve assert'i
`EXPECT_TRUE(hasDiag(result, DiagID::err_array_element_literal_range));`
biçimine SIKILAŞTIR. Değilse, aralık yoluna gerçekten giren bir vaka bul
(ör. hedefi `u8` yapıp `[u, 300]` yaz — 300 pozitif bir tamsayı
literalidir ve `u8`'e sığmaz) ve testi ona göre sıkılaştır. Hangi yolu
seçtiğini ve `--dump-ast` çıktısını raporuna yaz. Gevşek `||` assert'ini
teslim etme.

- [ ] **Step 4: `visitVarDecl`'e anotasyon yönlendirmeli kontrolü ekle**

`src/Sema/TypeChecker.cpp`, `visitVarDecl` içinde. ŞU AN:

```cpp
    if (node->hasInit()) {
        visit(const_cast<Expr *>(node->getInit()));
    }
```

BUNUNLA DEĞİŞTİR:

```cpp
    if (node->hasInit()) {
        visit(const_cast<Expr *>(node->getInit()));
    }

    // An annotated array literal is checked against the ANNOTATION rather
    // than against its own elements, and then takes the annotation's type.
    // Leaving the unified element type on the literal would make the
    // generic annotation-vs-init check below compare [i64] with [i32] and
    // reject `let a: [i64] = [1, 2, 3]`, which compiles today.
    if (node->hasInit() && node->hasTypeAnnotation() && node->getType() &&
        node->getType()->getKind() == TypeRepr::Kind::Array &&
        node->getInit()->getKind() == ASTNode::NodeKind::ArrayLiteralExpr) {
        auto *annArr = static_cast<const ArrayTypeRepr *>(node->getType());
        const TypeRepr *annElem = annArr->getElement();
        // [dyn Protocol] elements are boxed on a separate path.
        if (annElem && annElem->getKind() != TypeRepr::Kind::DynProtocol) {
            auto *lit = static_cast<ArrayLiteralExpr *>(
                const_cast<Expr *>(node->getInit()));
            for (auto &elemPtr : lit->getElements()) {
                const Expr *elem = elemPtr.get();
                switch (checkArrayElement(annElem, elem)) {
                case ElemAssign::Ok:
                    break;
                case ElemAssign::Mismatch:
                    diag_.report(elem->getStartLoc(),
                                 DiagID::err_array_element_type_mismatch,
                                 typeToString(elem->getResolvedType()),
                                 typeToString(annElem));
                    break;
                case ElemAssign::LiteralOutOfRange: {
                    auto *intLit = static_cast<const IntegerLiteralExpr *>(elem);
                    diag_.report(elem->getStartLoc(),
                                 DiagID::err_array_element_literal_range,
                                 std::to_string(intLit->getValue()),
                                 typeToString(annElem));
                    break;
                }
                }
            }
            lit->setResolvedType(cloneTypeRepr(node->getType()));
        }
    }
```

- [ ] **Step 5: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.ArrayLiteral*'
```

Beklenen: 13/13 PASSED.

- [ ] **Step 6: Tam süit — bu görevin ASIL kapısı**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. `resolvedType` ataması IRGen'in birçok yolunu
etkileyebilir; bir test kırılırsa DURDUR, hangi testin hangi çıktıyla
kırıldığını ve `--emit-ir` ile incelediğin farkı rapora yaz. Kendi başına
düzeltmeye çalışma.

- [ ] **Step 7: Commit**

```bash
git add src/Sema/TypeChecker.cpp tests/unit/SemaTest.cpp
git commit -F- <<'EOF'
feat(sema): anotasyonlu dizi literali anotasyona göre denetleniyor

var/let anotasyonu [T] ve init bir dizi literaliyse elemanlar doğrudan
T'ye göre denetlenir; kayıplı literaller aralık kontrolünden geçer,
kayıplı değişkenler reddedilir, değer-koruyan dönüşümler serbesttir.

Literalin resolvedType'ı anotasyonun tipine ayarlanır. Aksi halde mevcut
genel anotasyon-uyum kontrolü [i64] ile [i32]'yi karşılaştırıp bugün
çalışan `let a: [i64] = [1, 2, 3]` kodunu reddederdi.

Anotasyonsuz literaller elemanlarından birleştirilen [U] tipini alır.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 4: Bağımsız literalde yuva tipini `resolvedType`'tan al

**Files:**
- Modify: `src/IR/IRGenExpr.cpp` (`visitArrayLiteralExpr`)
- Modify: `roadmap.md`
- Test: `tests/unit/RuntimeExecTest.cpp`

**Interfaces:**
- Consumes: `IRGen::coerceToElemType` (Görev 1),
  `ArrayLiteralExpr::getResolvedType()` (Görev 3).
- Produces: yok (son görev).

`visitArrayLiteralExpr` yuva tipini İLK elemandan alıyor. Görev 3'ten
sonra `[1, 2.5]` literalinin `resolvedType`'ı `[f64]`; yuva tipi ilk
elemandan (`i32`) alınırsa `2.5` yuvaya sığmaz ve dönüşüm yanlış yönde
olur. Yuva tipi varsa `resolvedType`'tan alınmalıdır.

- [ ] **Step 1: Koşum testini yaz (RED)**

`tests/unit/RuntimeExecTest.cpp` — Görev 1'in eklediği bloğun sonuna:

```cpp
TEST(RuntimeExecTest, ArrayElemCoercePromotedLiteralSlot) {
    // The literal's unified element type is f64, so the slot must be 8
    // bytes wide and the integer literal converted into it — not the other
    // way round.
    auto r = compileAndRun(R"--(
        func main() {
            let a = [1, 2.5]
            let x: f64 = a[0]
            let y: f64 = a[1]
            println(x)
            println(y)
        }
    )--", "arr_elem_coerce_promoted");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1.000000\n2.500000\n") << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: Testi koş, kırmızı olduğunu doğrula**

```
cmake --build build-clang --target runtime_exec_test
build-clang/tests/runtime_exec_test.exe --gtest_filter='*ArrayElemCoercePromoted*'
```

Beklenen: BAŞARISIZ. Çıktıyı rapora yaz (derleme hatası da olabilir —
anotasyonsuz `let a = [...]` bağlaması dilde kısıtlı olabilir; öyleyse
Adım 1'deki testi `let a: [f64] = [1, 2.5]` biçimine çevir ve bunu rapora
yaz).

- [ ] **Step 3: Yuva tipini `resolvedType`'tan al**

`src/IR/IRGenExpr.cpp`, `visitArrayLiteralExpr`. ŞU AN:

```cpp
    auto *firstVal = visit(elements[0].get());
    if (!firstVal) return nullptr;
    auto *elemType = firstVal->getType();
```

BUNUNLA DEĞİŞTİR:

```cpp
    auto *firstVal = visit(elements[0].get());
    if (!firstVal) return nullptr;
    // Sema unified the elements and recorded the result; that type is
    // authoritative. Falling back to the first element's type would make
    // [1, 2.5] allocate 4-byte slots for f64 values.
    llvm::Type *elemType = firstVal->getType();
    if (auto *rt = node->getResolvedType())
        if (rt->getKind() == TypeRepr::Kind::Array)
            elemType = dynArrayElemLLVMType(
                static_cast<const ArrayTypeRepr *>(rt)->getElement());
```

- [ ] **Step 4: Derle ve testi koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/runtime_exec_test.exe --gtest_filter='*ArrayElemCoerce*'
```

Beklenen: 7/7 PASSED.

- [ ] **Step 5: Tam süit**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**.

- [ ] **Step 6: roadmap satırını güncelle**

`roadmap.md` — 2.3 bölümündeki şu satırı bul (metnin başı):

```
| Heterojen dizi literalinin eleman tipleri Sema'da hiç birleştirilmiyor
```

Tüm satırı bununla değiştir:

```
| Dizi eleman store'ları dönüşüm yapmıyordu ve heterojen literal Sema'dan geçiyordu (çözüldü 2026-07) | İki katman: IRGen'de `coerceToElemType` 8 eleman-store sitesinde (VarDecl literali, bağımsız literal ×2, push yerel+üye, eleman ataması dinamik+sabit+üye) — `[f64]=[1,2]` artık 2.0 okuyor, `[u8]` yuvasına 4 baytlık store taşması ve `[i64]` yuvasının ilklenmemiş üst yarısı kapandı. Sema'da eleman uyum kuralı: değer-koruyan dönüşüm her ifade için sessiz, kayıplı yalnız literalde + aralık kontrolü, bool sayısal değil; anotasyonlu literal anotasyona göre denetlenip onun tipini alıyor | `IRGen.cpp` `coerceToElemType`, `TypeChecker.cpp` `checkArrayElement`/`visitArrayLiteralExpr`/`visitVarDecl`; 7 koşum + 13 Sema testi |
```

- [ ] **Step 7: Commit**

```bash
git add src/IR/IRGenExpr.cpp tests/unit/RuntimeExecTest.cpp roadmap.md
git commit -F- <<'EOF'
fix(irgen): bağımsız dizi literalinde yuva tipi Sema'nın birleştirdiği tip

Yuva tipi ilk elemandan alınıyordu; Sema elemanları birleştirip [1, 2.5]
literalini [f64] olarak çözdüğünden ilk elemanın i32 tipi 4 baytlık yuva
ayırıyor ve f64 değerleri yanlış yöne dönüştürüyordu.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 5: İşaretsiz kaynakların doğru genişletilmesi

**Files:**
- Modify: `include/liva/IR/IRGen.h` (`coerceToElemType` bildiriminin yanı)
- Modify: `src/IR/IRGen.cpp` (`coerceToElemType` gövdesinin yanı)
- Modify: `src/IR/IRGenDecl.cpp`, `src/IR/IRGenExpr.cpp`,
  `src/IR/IRGenCallMethod.cpp`, `src/IR/IRGenCall.cpp` (Görev 1'in 8 sitesi)
- Test: `tests/unit/RuntimeExecTest.cpp`

**Interfaces:**
- Consumes: `IRGen::coerceToElemType(llvm::Value *, llvm::Type *, bool srcUnsigned = false)` (Görev 1).
- Produces: `bool IRGen::isUnsignedTypeRepr(const TypeRepr *t) const;`

Görev 1 incelemesinin Critical bulgusu: `coerceToElemType`'ın `srcUnsigned`
parametresi HİÇBİR çağrı sitesinde `true` geçilmiyor, dolayısıyla dar bir
işaretsiz tipten geniş bir yuvaya yapılan her genişletme `sext` üretiyor.
Doğrulanmış tekrar: `let a: [u8] = [10, 20, 200]; let x: u8 = a[2];`
`b: [u32]`'ye `b.push(x)` → `x` doğru olarak 200 basılıyor ama `b[1]`
**-56** okunuyor. Bu bir gerileme değildir (Görev 1 öncesi de yanlıştı,
üstelik belirsizdi), ama Görev 1'in vaadini yarım bırakır.

Kapsam yalnız **kaynak** işaretliliğidir. Hedefin işaretliliği
(`f64 → [u32]` için `FPToUI`) bu görevin DIŞINDADIR: `varDynArrayTypes`
yalnız LLVM tipi taşıyor, eleman `TypeRepr`'ı yok. Adım 6 bu yolun
erişilebilir olup olmadığını ölçer ve raporlar.

- [ ] **Step 1: Koşum testlerini yaz (RED)**

`tests/unit/RuntimeExecTest.cpp` — Görev 1'in eklediği
`ArrayElemCoerce*` bloğunun sonuna:

```cpp
TEST(RuntimeExecTest, ArrayElemCoerceUnsignedWidensZeroExtended) {
    // A u8 whose high bit is set must zero-extend into a wider slot.
    // Sign-extending it turns 200 into -56.
    auto r = compileAndRun(R"--(
        func main() {
            let a: [u8] = [10, 20, 200]
            let x: u8 = a[2]
            var b: [u32] = [0]
            b.push(x)
            let y: u32 = b[1]
            println(y)
        }
    )--", "arr_elem_coerce_unsigned_push");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "200\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ArrayElemCoerceUnsignedWidensOnAssign) {
    auto r = compileAndRun(R"--(
        func main() {
            let a: [u8] = [10, 20, 200]
            let x: u8 = a[2]
            var b: [u32] = [0, 0]
            b[1] = x
            let y: u32 = b[1]
            println(y)
        }
    )--", "arr_elem_coerce_unsigned_assign");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "200\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, ArrayElemCoerceSignedStillSignExtends) {
    // The counterpart: a signed negative source must KEEP sign-extending.
    // A blanket switch to zext would print 4294967286 here.
    auto r = compileAndRun(R"--(
        func main() {
            let n = 0 - 10
            var b: [i64] = [0]
            b.push(n)
            let y: i64 = b[1]
            println(y)
        }
    )--", "arr_elem_coerce_signed_still_sext");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "-10\n") << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: Testleri koş, kırmızı olduklarını doğrula**

```
cmake --build build-clang --target runtime_exec_test
build-clang/tests/runtime_exec_test.exe --gtest_filter='*ArrayElemCoerceUnsigned*:*ArrayElemCoerceSigned*'
```

Beklenen: iki `Unsigned` testi BAŞARISIZ (`-56` basılır),
`ArrayElemCoerceSignedStillSignExtends` GEÇER (gerilemeyi pinler).

- [ ] **Step 3: `isUnsignedTypeRepr` bildirimini ekle**

`include/liva/IR/IRGen.h` — `coerceToElemType` bildiriminin hemen ardına:

```cpp
    /// Whether a Liva type is an unsigned integer. LLVM types carry no
    /// signedness, so element stores recover it from the source
    /// expression's static Liva type.
    bool isUnsignedTypeRepr(const TypeRepr *t) const;
```

- [ ] **Step 4: `isUnsignedTypeRepr` gövdesini yaz**

`src/IR/IRGen.cpp` — `coerceToElemType` gövdesinin hemen ardına:

```cpp
bool IRGen::isUnsignedTypeRepr(const TypeRepr *t) const {
    if (!t) return false;
    switch (t->getKind()) {
    case TypeRepr::Kind::U8:  case TypeRepr::Kind::U16:
    case TypeRepr::Kind::U32: case TypeRepr::Kind::U64:
        return true;
    default:
        return false;
    }
}
```

- [ ] **Step 5: 8 sitede `srcUnsigned`'ı besle**

Her sitede, `coerceToElemType(val, slotTy)` çağrısını
`coerceToElemType(val, slotTy, isUnsignedTypeRepr(<kaynak ifade>->getResolvedType()))`
biçimine çevir. Kaynak ifade siteye göre:

| Site | Dosya | Kaynak ifade |
|---|---|---|
| 1 | `IRGenDecl.cpp` VarDecl dizi literali | ilgili `arrayLit->getElements()[i]` |
| 2 | `IRGenExpr.cpp` `arrlit.e0` | `elements[0].get()` |
| 3 | `IRGenExpr.cpp` `arrlit.e<i>` | `elements[i].get()` |
| 4 | `IRGenCallMethod.cpp` `push.tmp` | `node->getArgs()[0].get()` |
| 5 | `IRGenCallMethod.cpp` `mpush.tmp` | `node->getArgs()[0].get()` |
| 6 | `IRGenCall.cpp` dinamik eleman ataması | `node->getValue()` |
| 7 | `IRGenCall.cpp` sabit dizi ataması | `node->getValue()` |
| 8 | `IRGenCall.cpp` üye eleman ataması | `node->getValue()` |

**Site 1 için DİKKAT — indeks hizası:** `initVals` şu an
`if (val) initVals.push_back(val);` ile dolduruluyor, yani bir eleman
`nullptr` üretirse `initVals[i]` artık `elements[i]`'ye karşılık gelmez ve
yanlış ifadeden işaretlilik okursun. Bunu önlemek için `initVals`'ı
`std::vector<llvm::Value *>` olarak bırak ve YANINDA aynı sırayla dolan
`std::vector<const Expr *> initExprs;` tut — `initVals.push_back(val)`
yapılan her yerde (dyn-protocol boxing dalı dahil) `initExprs.push_back`
de yap. Store döngüsünde `initExprs[i]` kullan.

- [ ] **Step 6: Hedef-işaretliliği yolunun erişilebilirliğini ölç**

Aşağıdaki programı
`C:/Users/Kadir/AppData/Local/Temp/claude/F--Cpp-Projects-liva-lang/6cd7e925-e4dd-4fea-ad87-b79eaee75c70/scratchpad/t5_fpu.liva`
olarak yaz ve derle:

```liva
func main() {
    var b: [u32] = [0]
    b.push(3.7)
    let y: u32 = b[0]
    println(y)
}
```

```
build-clang/livac.exe t5_fpu.liva -o t5_fpu.exe
```

Sonucu (derleme hatası mı, çalışıyor mu, ne basıyor) raporuna yaz.
**Hiçbir kod değiştirme** — bu adım yalnız ölçümdür. `FPToUI` bu görevin
kapsamı dışındadır.

- [ ] **Step 7: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/runtime_exec_test.exe --gtest_filter='*ArrayElemCoerce*'
```

Beklenen: Görev 1'in 6 testi + Görev 4'ün 1 testi (varsa) + bu görevin 3
testi, hepsi PASSED.

- [ ] **Step 8: Tam süit**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. Kırılan olursa DURDUR ve rapora yaz —
özellikle `[u8]` kullanan gzip/crypto testleri.

- [ ] **Step 9: Commit**

```bash
git add include/liva/IR/IRGen.h src/IR tests/unit/RuntimeExecTest.cpp
git commit -F- <<'EOF'
fix(irgen): dizi eleman store'unda işaretsiz kaynaklar sıfır-genişletiliyor

coerceToElemType'ın srcUnsigned parametresi hiçbir çağrı sitesinde
geçilmiyordu; dar bir işaretsiz tipten geniş bir yuvaya yapılan her
genişletme sext üretiyordu. `let x: u8 = a[2]` (200) bir [u32] dizisine
push edildiğinde -56 okunuyordu.

İşaretlilik LLVM tipinde taşınmadığından kaynak ifadenin statik Liva
tipinden türetiliyor (isUnsignedTypeRepr) ve 8 eleman-store sitesinin
tamamında besleniyor. İşaretli kaynaklar sext davranışını korur.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

## Self-Review Notları

- **Spec kapsamı:** §1.1 uyum kuralı → Görev 2 Adım 5; §1.2 birleştirme →
  Görev 2 Adım 6 + Görev 3 Adım 3; §1.3 anotasyon kontrolü → Görev 3
  Adım 4; §1.4 diagnostikler → Görev 1 Adım 3 + Görev 2 Adım 1; §2
  `coerceToElemType` → Görev 1 Adım 4-5; §2.1 sekiz site → Görev 1
  Adım 6-10; §2.1 site 2/3 özel durumu → Görev 4; test stratejisi →
  Görev 1/2/3/4 test adımları.
- **Sıralama:** Görev 1 ve Görev 2 birbirinden bağımsızdır; Görev 3
  Görev 2'ye, Görev 4 hem Görev 1'e hem Görev 3'e bağlıdır.
