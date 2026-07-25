# Argüman ve Atama Tiplemesi Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fonksiyon argümanları ve atamalar Sema'da denetlensin, IRGen de değeri hedefin tipine dönüştürsün.

**Architecture:** Dizi eleman tiplemesinde kurulan model iki yeni yüzeye taşınıyor. Sema'da mevcut uyum kuralı `checkAssignable` adıyla genelleştirilip argüman ve atama denetimine besleniyor. IRGen'de dönüşüm site sayımıyla değil, callee'nin `llvm::FunctionType`'ına göre **boğaz noktasında** yapılıyor.

**Tech Stack:** C++20, LLVM 21, CMake + Ninja, GoogleTest. Derleyici kaynağı `src/`, testler `tests/unit/`.

## Global Constraints

- Build: `cmake --build build-clang`. ÖNPLAN, timeout 600000 ms.
- Test: `ctest --test-dir build-clang --output-on-failure` — **SERİ koş, asla `-j` verme**. Foreground, timeout 600000 ms. `PgRealRoundTrip`, `HttpLiveRoundTrip`, `WsLiveEchoRoundTrip` atlanır, bu normaldir.
- Tam süit şu an **2559/2559** yeşil, 0 başarısız. Bu sayı korunmalı (yeni testlerle artar).
- Her commit mesajının sonunda AYNEN şu iki satır olmalı:
  ```
  Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
  ```
- Commit mesajları TÜRKÇE yazılır.
- **Sema denetimi yalnız bildirimi ÇÖZÜLEBİLEN çağrılarda çalışır.** Builtin, closure değişkeni üzerinden çağrı, dinamik dispatch → sessiz kal. Yanlış pozitif gerilemedir.
- **`checkAssignable` yargılayamadığı yerde sessiz kalır:** `getResolvedType()` null olan ifade, ya da hedef kind'ı `Named`/`Generic`/`Inferred`/`AssociatedType`/`DynProtocol`.
- **`coerceToElemType` dönüşüm yapamazsa argüman DOKUNULMADAN bırakılır** — Sema zaten reddetmiş olmalı; verifier bugünkü gibi arka-durdurucu kalır.
- `[dyn Protocol]` ve trait-object yolları etkilenmemeli.
- clangd'nin `file not found` uyarıları include-path yanlış pozitifidir; yetkili olan derlemedir.

## File Structure

| Dosya | Sorumluluk | Görev |
|---|---|---|
| `include/liva/Sema/TypeChecker.h` | `Assignability` + `checkAssignable` + `checkCallArgTypes` bildirimleri | 1, 2 |
| `src/Sema/TypeChecker.cpp` | yeniden adlandırma, atama denetimi | 1, 3 |
| `src/Sema/TypeCheckerCall.cpp` | argüman denetimi | 2 |
| `include/liva/Common/DiagnosticKinds.def` | 4 yeni diagnostik | 2, 3 |
| `include/liva/IR/IRGen.h` | `coerceCallArgs` bildirimi | 4 |
| `src/IR/IRGen.cpp` | `coerceCallArgs` gövdesi | 4 |
| `src/IR/IRGenCall.cpp`, `src/IR/IRGenCallMethod.cpp` | boğaz noktası uygulaması, atama dönüşümü | 4, 5 |
| `tests/unit/SemaTest.cpp` | denetim testleri | 1, 2, 3 |
| `tests/unit/RuntimeExecTest.cpp` | koşum testleri | 4, 5 |
| `roadmap.md` | çözüldü işaretleme | 5 |

---

### Task 1: `checkArrayElement` → `checkAssignable` yeniden adlandırması

**Files:**
- Modify: `include/liva/Sema/TypeChecker.h:157,163`
- Modify: `src/Sema/TypeChecker.cpp:934-948, 2650-2683` ve `visitArrayLiteralExpr` içindeki çağrılar

**Interfaces:**
- Produces: `enum class TypeChecker::Assignability { Ok, Mismatch, LiteralOutOfRange };`
- Produces: `Assignability TypeChecker::checkAssignable(const TypeRepr *target, const Expr *value) const;`
- Görev 2 ve 3 ikisini de kullanır.

Bu görev **davranışı değiştirmez** — saf yeniden adlandırma. Gövde zaten
tamamen genel; yalnız adı dizi elemanına özgü.

- [ ] **Step 1: Bildirimi yeniden adlandır**

`include/liva/Sema/TypeChecker.h` — ŞU AN:

```cpp
    enum class ElemAssign { Ok, Mismatch, LiteralOutOfRange };
```

BUNUNLA DEĞİŞTİR:

```cpp
    /// Whether a value may be stored into a location of a given type —
    /// an array element, a function parameter, or an assignment target.
    enum class Assignability { Ok, Mismatch, LiteralOutOfRange };
```

Aynı dosyada, ŞU AN:

```cpp
    ElemAssign checkArrayElement(const TypeRepr *target, const Expr *elem) const;
```

BUNUNLA DEĞİŞTİR:

```cpp
    Assignability checkAssignable(const TypeRepr *target, const Expr *value) const;
```

Bildirimin üzerindeki yorum bloğunda "array literal element" geçen ifadeyi
"value" olarak güncelle; kuralın kendisini (değer-koruyan sessiz, kayıplı
literal-only, yargılanamayanda sessiz) AYNEN bırak.

- [ ] **Step 2: Gövdeyi ve tüm çağrıları yeniden adlandır**

`src/Sema/TypeChecker.cpp`'de şu değişiklikleri yap:
- `TypeChecker::ElemAssign` → `TypeChecker::Assignability` (gövde imzası)
- `TypeChecker::checkArrayElement(const TypeRepr *target, const Expr *elem)`
  → `TypeChecker::checkAssignable(const TypeRepr *target, const Expr *value)`;
  gövde içindeki `elem` parametresi `value` olur (gövdedeki tüm kullanımları
  da).
- Gövde içindeki `ElemAssign::` → `Assignability::` (hepsi)
- `visitVarDecl` (~934) ve `visitArrayLiteralExpr` içindeki
  `checkArrayElement(` çağrıları → `checkAssignable(`; oradaki
  `ElemAssign::` nitelemeleri → `Assignability::`

Doğrulama: `grep -rn "checkArrayElement\|ElemAssign" include/ src/` HİÇBİR
şey döndürmemeli.

- [ ] **Step 3: Derle ve tam süiti koş**

```
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. Saf yeniden adlandırma olduğu için tek bir test
bile değişmemeli. Değişirse DURDUR ve raporla — yanlışlıkla davranış
değiştirmişsindir.

- [ ] **Step 4: Commit**

```bash
git add include/liva/Sema/TypeChecker.h src/Sema/TypeChecker.cpp
git commit -F- <<'EOF'
refactor(sema): checkArrayElement -> checkAssignable

Uyum kuralının gövdesi zaten tamamen geneldi (değer-koruyan sessiz,
kayıplı literal-only, yargılanamayan hedefte sessiz); yalnız adı dizi
elemanına özgüydü. Argüman ve atama denetimi de aynı kuralı kullanacağı
için ad genelleştirildi. Davranış değişmedi.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 2: Sema argüman denetimi

**Files:**
- Modify: `include/liva/Common/DiagnosticKinds.def`
- Modify: `include/liva/Sema/TypeChecker.h:62` civarı
- Modify: `src/Sema/TypeCheckerCall.cpp` (`checkCallArgCount`'un hemen ardı)
- Modify: `src/Sema/TypeChecker.cpp:2241` (çağrı sitesi)
- Test: `tests/unit/SemaTest.cpp`

**Interfaces:**
- Consumes: `TypeChecker::checkAssignable`, `TypeChecker::Assignability` (Görev 1)
- Produces: `void TypeChecker::checkCallArgTypes(CallExpr *node);`
- Produces: `DiagID::err_arg_type_mismatch`, `DiagID::err_arg_literal_range`

Bu görev **dildeki her çağrıyı** görür. Tam süit asıl kapıdır.

- [ ] **Step 1: Diagnostikleri ekle**

`include/liva/Common/DiagnosticKinds.def` — `err_array_element_literal_range`
satırının hemen ardına:

```
DIAG(err_arg_type_mismatch, error, "argument of type '%0' cannot be passed to parameter of type '%1'")
DIAG(err_arg_literal_range, error, "literal %0 does not fit in parameter type '%1'")
```

- [ ] **Step 2: SemaTest testlerini yaz (RED)**

`tests/unit/SemaTest.cpp` dosyasının SONUNA ekle:

```cpp
// ============================================================
// Function argument type checking (roadmap 2.3)
// ============================================================
// Arguments were never type-checked. A [string] passed to a [i32]
// parameter compiled and ran silently, because both lower to the same
// %DynArray and the LLVM verifier has nothing to complain about.

TEST_F(SemaTest, ArgArrayElementMismatchRejected) {
    auto result = check(R"(
        func mkStr() -> [string] { return ["a", "b"] }
        func take(xs: [i32]) -> i64 { return xs.length }
        func main() {
            println(take(mkStr()))
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_arg_type_mismatch));
}

TEST_F(SemaTest, ArgIntLiteralIntoI64ParamAccepted) {
    // Value-preserving: the language has no i64 literal, so this must work.
    auto result = check(R"(
        func take(n: i64) -> i64 { return n }
        func main() {
            println(take(5))
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_literal_range));
}

TEST_F(SemaTest, ArgIntLiteralIntoF64ParamAccepted) {
    auto result = check(R"(
        func take(x: f64) -> f64 { return x }
        func main() {
            println(take(3))
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_type_mismatch));
}

TEST_F(SemaTest, ArgNarrowingLiteralInRangeAccepted) {
    auto result = check(R"(
        func take(b: u8) -> u8 { return b }
        func main() {
            println(take(200))
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_literal_range));
}

TEST_F(SemaTest, ArgNarrowingLiteralOutOfRangeRejected) {
    auto result = check(R"(
        func take(b: u8) -> u8 { return b }
        func main() {
            println(take(300))
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_arg_literal_range));
}

TEST_F(SemaTest, ArgNarrowingVariableRejected) {
    auto result = check(R"(
        func take(b: u8) -> u8 { return b }
        func main() {
            let n = 5
            println(take(n))
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_arg_type_mismatch));
}

TEST_F(SemaTest, ArgBuiltinCallNotChecked) {
    // Builtins have no resolvable FuncDecl; the check must stay silent.
    auto result = check(R"(
        func main() {
            println("hi")
            println(strLen("hi"))
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_literal_range));
}

TEST_F(SemaTest, ArgGenericParamNotChecked) {
    // A [T] parameter is unjudgeable before monomorphization.
    auto result = check(R"(
        func first<T>(xs: [T]) -> i64 { return xs.length }
        func main() {
            let v: [i32] = [1, 2, 3]
            println(first(v))
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_type_mismatch));
}

TEST_F(SemaTest, ArgMatchingTypesAccepted) {
    auto result = check(R"(
        func takeI32(n: i32) -> i32 { return n }
        func takeS(s: string) -> string { return s }
        func takeArr(xs: [i32]) -> i64 { return xs.length }
        func main() {
            println(takeI32(5))
            println(takeS("hi"))
            let v: [i32] = [1, 2, 3]
            println(takeArr(v))
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_arg_literal_range));
}
```

- [ ] **Step 3: Testleri koş, kırmızı olduklarını doğrula**

```
cmake --build build-clang --target sema_test
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.Arg*'
```

Beklenen: `ArgArrayElementMismatchRejected`,
`ArgNarrowingLiteralOutOfRangeRejected`, `ArgNarrowingVariableRejected`
BAŞARISIZ (diagnostik hiç üretilmiyor). Diğer 6'sı geçer. Kaç testin
kırmızı olduğunu rapora yaz.

- [ ] **Step 4: Bildirimi ekle**

`include/liva/Sema/TypeChecker.h` — `void checkCallArgCount(CallExpr *node);`
bildiriminin hemen ardına:

```cpp
    /// Type-check a call's arguments against the callee's declared
    /// parameters. Stays silent when the callee's declaration cannot be
    /// resolved (builtins, calls through a closure variable, dynamic
    /// dispatch) — a false positive there would reject working code.
    void checkCallArgTypes(CallExpr *node);
```

- [ ] **Step 5: Gövdeyi yaz**

`src/Sema/TypeCheckerCall.cpp` — `checkCallArgCount` gövdesinin HEMEN
ARDINA:

```cpp
void TypeChecker::checkCallArgTypes(CallExpr *node) {
    // Only a direct call to a named user function is judged. Method calls,
    // builtins and calls through a value all reach here with a callee this
    // lookup cannot resolve, and are deliberately left alone.
    if (node->getCallee()->getKind() != ASTNode::NodeKind::IdentifierExpr)
        return;
    auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
    auto *sym = scopes_.lookup(ident->getName());
    if (!sym || sym->kind != Symbol::Kind::Function || !sym->funcDecl)
        return;
    const auto &params = sym->funcDecl->getParams();
    const auto &args = node->getArgs();

    size_t paramIdx = 0;
    for (size_t argIdx = 0; argIdx < args.size(); ++argIdx) {
        // Skip an implicit self parameter if the declaration carries one.
        while (paramIdx < params.size() && params[paramIdx].isSelf)
            ++paramIdx;
        if (paramIdx >= params.size())
            break;
        // A variadic parameter swallows this argument and every one after
        // it; packing is a separate code path with its own rules.
        if (params[paramIdx].isVariadic)
            break;
        const Expr *arg = args[argIdx].get();
        switch (checkAssignable(params[paramIdx].type.get(), arg)) {
        case Assignability::Ok:
            break;
        case Assignability::Mismatch:
            diag_.report(arg->getStartLoc(), DiagID::err_arg_type_mismatch,
                         typeToString(arg->getResolvedType()),
                         typeToString(params[paramIdx].type.get()));
            break;
        case Assignability::LiteralOutOfRange: {
            auto *lit = static_cast<const IntegerLiteralExpr *>(arg);
            diag_.report(arg->getStartLoc(), DiagID::err_arg_literal_range,
                         std::to_string(lit->getValue()),
                         typeToString(params[paramIdx].type.get()));
            break;
        }
        }
        ++paramIdx;
    }
}
```

`Symbol`'ün `funcDecl` alanının gerçek adı için
`include/liva/Sema/Scope.h` (ya da `Symbol`'ün tanımlandığı başlık) —
`classDecl`/`structDecl` alanlarının yanında olmalı. Alan yoksa ya da adı
farklıysa, `funcDecls_` haritası gibi mevcut bir çözüm yolunu kullan ve
seçimini rapora yaz. **Uydurma alan adı yazma — derlenmez.**

- [ ] **Step 6: Çağrı sitesine bağla**

`src/Sema/TypeChecker.cpp:2241` — ŞU AN:

```cpp
    checkCallArgCount(node);
```

BUNUNLA DEĞİŞTİR:

```cpp
    checkCallArgCount(node);
    checkCallArgTypes(node);
```

- [ ] **Step 7: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.Arg*'
```

Beklenen: 9/9 PASSED.

- [ ] **Step 8: Tam süit — bu görevin ASIL kapısı**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. Argüman denetimi dildeki her çağrıyı gördüğü
için burada bir yanlış pozitif çıkması olasıdır. Kırılan olursa DURDUR ve
raporla: hangi test, hangi kaynak satırı, hangi tip çifti. Kendi başına
kuralı gevşetmeye çalışma.

- [ ] **Step 9: Commit**

```bash
git add include/liva/Common/DiagnosticKinds.def include/liva/Sema/TypeChecker.h src/Sema/TypeCheckerCall.cpp src/Sema/TypeChecker.cpp tests/unit/SemaTest.cpp
git commit -F- <<'EOF'
feat(sema): fonksiyon argümanları tip denetiminden geçiyor

Argümanlar hiç denetlenmiyordu: `func take(xs: [i32])` çağrısına
`[string]` geçmek sessizce derleniyor ve çalışıyordu, çünkü iki DynArray
aynı LLVM tipine sahip ve verifier'ın şikâyet edeceği bir şey yok.

Denetim yalnız bildirimi çözülebilen doğrudan çağrılarda çalışır;
builtin, closure değişkeni üzerinden çağrı ve dinamik dispatch sessiz
kalır. Variadic parametreye denk gelen ve sonrasındaki argümanlar
denetlenmez (paketleme ayrı kod yolu).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 3: Sema atama denetimi

**Files:**
- Modify: `include/liva/Common/DiagnosticKinds.def`
- Modify: `src/Sema/TypeChecker.cpp` (`visitAssignExpr`)
- Test: `tests/unit/SemaTest.cpp`

**Interfaces:**
- Consumes: `TypeChecker::checkAssignable`, `TypeChecker::Assignability` (Görev 1)
- Produces: `DiagID::err_assign_type_mismatch`, `DiagID::err_assign_literal_range`

- [ ] **Step 1: Diagnostikleri ekle**

`include/liva/Common/DiagnosticKinds.def` — `err_arg_literal_range`
satırının hemen ardına:

```
DIAG(err_assign_type_mismatch, error, "cannot assign a value of type '%0' to a variable of type '%1'")
DIAG(err_assign_literal_range, error, "literal %0 does not fit in type '%1'")
```

- [ ] **Step 2: SemaTest testlerini yaz (RED)**

`tests/unit/SemaTest.cpp` dosyasının SONUNA ekle:

```cpp
// ============================================================
// Assignment type checking (roadmap 2.3)
// ============================================================
// visitAssignExpr only checked mutability. `a = mkStr()` on an [i32]
// variable compiled and then read a pointer as an i32.

TEST_F(SemaTest, AssignArrayElementMismatchRejected) {
    auto result = check(R"(
        func mkStr() -> [string] { return ["a", "b"] }
        func main() {
            var a: [i32] = [1, 2, 3]
            a = mkStr()
            println(a.length)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_type_mismatch));
}

TEST_F(SemaTest, AssignMatchingTypeAccepted) {
    auto result = check(R"(
        func main() {
            var n = 5
            var s = "hi"
            n = 7
            s = "yo"
            println(n)
            println(s)
        }
    )");
    EXPECT_FALSE(hasDiag(result, DiagID::err_assign_type_mismatch));
    EXPECT_FALSE(hasDiag(result, DiagID::err_assign_literal_range));
}

TEST_F(SemaTest, AssignNarrowingLiteralOutOfRangeRejected) {
    auto result = check(R"(
        func mkU8() -> u8 { return 1 }
        func main() {
            var b: u8 = mkU8()
            b = 300
            println(b)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_literal_range));
}

TEST_F(SemaTest, AssignStringToIntRejected) {
    auto result = check(R"(
        func main() {
            var n = 5
            n = "hi"
            println(n)
        }
    )");
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_type_mismatch));
}
```

- [ ] **Step 3: Testleri koş, kırmızı olduklarını doğrula**

```
cmake --build build-clang --target sema_test
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.Assign*'
```

Beklenen: `AssignArrayElementMismatchRejected`,
`AssignNarrowingLiteralOutOfRangeRejected`, `AssignStringToIntRejected`
BAŞARISIZ. `AssignMatchingTypeAccepted` geçer.

- [ ] **Step 4: `visitAssignExpr`'e denetimi ekle**

`src/Sema/TypeChecker.cpp`, `visitAssignExpr`. ŞU AN gövdenin sonu:

```cpp
            diag_.reportHelp(node->getStartLoc(),
                             static_cast<uint32_t>(ident->getName().size()),
                             "declare with 'var' instead of 'let' to make it mutable",
                             "", DiagID::note_use_var_for_mutable);
        }
    }
}
```

BUNUNLA DEĞİŞTİR:

```cpp
            diag_.reportHelp(node->getStartLoc(),
                             static_cast<uint32_t>(ident->getName().size()),
                             "declare with 'var' instead of 'let' to make it mutable",
                             "", DiagID::note_use_var_for_mutable);
        }
        // Type-check the assigned value against the variable's declared
        // type. Only identifier targets are judged — member fields and
        // element targets go through their own paths.
        if (sym && sym->type) {
            const Expr *value = node->getValue();
            switch (checkAssignable(sym->type, value)) {
            case Assignability::Ok:
                break;
            case Assignability::Mismatch:
                diag_.report(value->getStartLoc(),
                             DiagID::err_assign_type_mismatch,
                             typeToString(value->getResolvedType()),
                             typeToString(sym->type));
                break;
            case Assignability::LiteralOutOfRange: {
                auto *lit = static_cast<const IntegerLiteralExpr *>(value);
                diag_.report(value->getStartLoc(),
                             DiagID::err_assign_literal_range,
                             std::to_string(lit->getValue()),
                             typeToString(sym->type));
                break;
            }
            }
        }
    }
}
```

- [ ] **Step 5: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/sema_test.exe --gtest_filter='SemaTest.Assign*'
```

Beklenen: 4/4 PASSED.

- [ ] **Step 6: Tam süit**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. Kırılan olursa DURDUR ve raporla.

- [ ] **Step 7: Commit**

```bash
git add include/liva/Common/DiagnosticKinds.def src/Sema/TypeChecker.cpp tests/unit/SemaTest.cpp
git commit -F- <<'EOF'
feat(sema): atamalar tip denetiminden geçiyor

visitAssignExpr yalnız mutability denetliyordu. `var a: [i32] = [1,2,3];
a = mkStr()` derleniyor ve `a[0]` pointer'ı i32 olarak okuyup çöp
basıyordu.

Yalnız identifier hedefler denetlenir; üye alan ve eleman hedefleri kendi
yollarından geçer.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 4: IRGen argüman boğaz noktası

**Files:**
- Modify: `include/liva/IR/IRGen.h` (`coerceToElemType` bildiriminin yanı)
- Modify: `src/IR/IRGen.cpp` (`coerceToElemType` gövdesinin yanı)
- Modify: `src/IR/IRGenCall.cpp`, `src/IR/IRGenCallMethod.cpp`
- Test: `tests/unit/RuntimeExecTest.cpp`

**Interfaces:**
- Consumes: `llvm::Value *IRGen::coerceToElemType(llvm::Value *, llvm::Type *, bool srcUnsigned = false)`, `bool IRGen::isUnsignedTypeRepr(const TypeRepr *) const` — ikisi de mevcut.
- Produces: `void IRGen::coerceCallArgs(llvm::FunctionType *fnTy, std::vector<llvm::Value *> &args, const std::vector<const TypeRepr *> &argTypes = {});`

**Neden boğaz noktası:** argüman geçişi 20'den fazla ayrı döngüde
yapılıyor. Yardımcı, tipler zaten eşleşiyorsa **hiçbir şey yapmaz** —
dolayısıyla fazla uygulamak ZARARSIZ, eksik uygulamak sessiz delik
bırakır. Bu asimetri gereği geniş uygula.

- [ ] **Step 1: Koşum testlerini yaz (RED)**

`tests/unit/RuntimeExecTest.cpp` dosyasının sonundaki
`#endif // LIVA_HAS_LLVM` satırının HEMEN ÖNÜNE:

```cpp
// ============================================================
// Call argument coercion (roadmap 2.3)
// ============================================================
// Arguments were passed at their own LLVM type. Since integer literals
// are strictly i32, no function taking i64/f64/u8 could be called with a
// literal at all — the module failed LLVM verification.

TEST(RuntimeExecTest, CallArgIntLiteralIntoI64Param) {
    auto r = compileAndRun(R"--(
        func take(n: i64) -> i64 { return n }
        func main() {
            println(take(5))
        }
    )--", "callarg_i64_literal");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "5\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CallArgIntLiteralIntoF64Param) {
    auto r = compileAndRun(R"--(
        func take(x: f64) -> f64 { return x }
        func main() {
            println(take(3))
        }
    )--", "callarg_f64_literal");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "3.000000\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CallArgIntLiteralIntoU8Param) {
    auto r = compileAndRun(R"--(
        func take(b: u8) -> u8 { return b }
        func main() {
            println(take(200))
        }
    )--", "callarg_u8_literal");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "200\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, CallArgMatchingTypesNoRegression) {
    auto r = compileAndRun(R"--(
        struct Box {
            var n: i32
        }
        impl Box {
            func get(ref self) -> i32 { return self.n }
        }
        func takeI32(n: i32) -> i32 { return n }
        func takeS(s: string) -> string { return s }
        func takeArr(xs: [i32]) -> i64 { return xs.length }
        func main() {
            println(takeI32(5))
            println(takeS("hi"))
            let v: [i32] = [1, 2, 3]
            println(takeArr(v))
            let b = Box { n: 9 }
            println(b.get())
        }
    )--", "callarg_no_regression");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "5\nhi\n3\n9\n") << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: Testleri koş, kırmızı olduklarını doğrula**

```
cmake --build build-clang --target runtime_exec_test
build-clang/tests/runtime_exec_test.exe --gtest_filter='*CallArg*'
```

Beklenen: ilk üçü BAŞARISIZ ("LLVM module verification failed: Call
parameter type does not match function signature!"),
`CallArgMatchingTypesNoRegression` GEÇER. Gerçek çıktıyı rapora yaz.

- [ ] **Step 3: `coerceCallArgs` bildirimini ekle**

`include/liva/IR/IRGen.h` — `isUnsignedTypeRepr` bildiriminin hemen ardına:

```cpp
    /// Coerce each argument to the callee's declared parameter type. The
    /// callee's own signature is the authority — it is exactly what the
    /// LLVM verifier checks — so this works no matter which of the many
    /// argument-collection loops produced the vector. Arguments beyond the
    /// declared parameter count (variadic packing) are left untouched, and
    /// so is any argument for which no conversion exists: Sema should have
    /// rejected that, and the verifier stays the backstop.
    /// `argTypes` supplies each argument's static Liva type where the
    /// caller has it, for signedness; a shorter or empty vector simply
    /// means "assume signed", which is the pre-existing behaviour.
    void coerceCallArgs(llvm::FunctionType *fnTy,
                        std::vector<llvm::Value *> &args,
                        const std::vector<const TypeRepr *> &argTypes = {});
```

- [ ] **Step 4: Gövdeyi yaz**

`src/IR/IRGen.cpp` — `isUnsignedTypeRepr` gövdesinin hemen ardına:

```cpp
void IRGen::coerceCallArgs(llvm::FunctionType *fnTy,
                           std::vector<llvm::Value *> &args,
                           const std::vector<const TypeRepr *> &argTypes) {
    if (!fnTy) return;
    const size_t declared = fnTy->getNumParams();
    for (size_t i = 0; i < args.size() && i < declared; ++i) {
        if (!args[i]) continue;
        bool srcUnsigned =
            i < argTypes.size() && isUnsignedTypeRepr(argTypes[i]);
        if (auto *converted =
                coerceToElemType(args[i], fnTy->getParamType(i), srcUnsigned))
            args[i] = converted;
    }
}
```

- [ ] **Step 5: Boğaz noktasını uygula**

`src/IR/IRGenCall.cpp` ve `src/IR/IRGenCallMethod.cpp`'de, callee'si bir
`llvm::Function *` olan HER `CreateCall` çağrısından hemen önce
`coerceCallArgs(<callee>->getFunctionType(), <argVector>);` ekle.

Siteleri şöyle bul:

```
grep -n "CreateCall(callee\|CreateCall(func\|CreateCall(targetFunc\|CreateCall(methodFn" src/IR/IRGenCall.cpp src/IR/IRGenCallMethod.cpp
```

Bilinen başlangıç noktaları `IRGenCall.cpp`'de `:258`, `:334`, `:410`
civarı (monomorfize generik çağrı, variadic yol, normal kullanıcı
fonksiyonu) ve `IRGenCallMethod.cpp`'deki metod-dispatch çağrıları.

Kurallar:
- Argüman vektörünün adı sitesine göre değişiyor (`args`, `argValues`);
  o sitedeki gerçek adı kullan.
- Aynı vektör iki `CreateCall` dalında (void / non-void) kullanılıyorsa
  `coerceCallArgs`'ı **dallardan ÖNCE bir kez** çağır.
- `argTypes` bu görevde HİÇBİR sitede doldurulmaz — varsayılan boş vektör
  geçilir. İşaretlilik takibi kapsam dışıdır; boş vektör bugünkü
  davranıştır.
- Runtime builtin çağrılarına (`getOrPanic(...)` ile alınan fonksiyonlar)
  **dokunma** — kapsam dışı.

Kaç siteye uyguladığını ve nasıl bulduğunu rapora yaz.

- [ ] **Step 6: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/runtime_exec_test.exe --gtest_filter='*CallArg*'
```

Beklenen: 4/4 PASSED.

- [ ] **Step 7: Tam süit**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**. Kırılan olursa DURDUR ve raporla.

- [ ] **Step 8: Pinleme kanıtı**

Değişikliği yerel olarak geri al (`coerceCallArgs` çağrılarını yorum
satırına çevir yeter), yeniden derle, `*CallArg*` filtresini koş — ilk üç
testin BAŞARISIZ olduğunu göster. Sonra geri koy, yeniden derle, 4/4
PASSED olduğunu göster. Her iki çıktıyı rapora yaz. Çalışma ağacını temiz
ve derlemeyi güncel bırak.

- [ ] **Step 9: Commit**

```bash
git add include/liva/IR/IRGen.h src/IR tests/unit/RuntimeExecTest.cpp
git commit -F- <<'EOF'
fix(irgen): çağrı argümanları callee imzasına dönüştürülüyor

Argümanlar kendi LLVM tipleriyle geçiriliyordu. Tamsayı literalleri katı
i32 olduğundan i64/f64/u8 parametreli hiçbir fonksiyon literalle
çağrılamıyordu — modül LLVM doğrulamasından geçmiyordu.

Dönüşüm site sayımıyla değil boğaz noktasında: callee'nin FunctionType'ı
otoritedir (verifier'ın denetlediği şeyin ta kendisi). Yardımcı tipler
eşleşiyorsa hiçbir şey yapmaz, dolayısıyla fazla uygulamak zararsız,
eksik uygulamak sessiz delik bırakır.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

### Task 5: IRGen atama dönüşümü ve roadmap

**Files:**
- Modify: `src/IR/IRGenCall.cpp` (`visitAssignExpr` identifier dalı)
- Modify: `roadmap.md`
- Test: `tests/unit/RuntimeExecTest.cpp`

**Interfaces:**
- Consumes: `IRGen::coerceToElemType` (mevcut)
- Produces: yok (son görev)

- [ ] **Step 1: Koşum testini yaz (RED)**

`tests/unit/RuntimeExecTest.cpp` — Görev 4'ün eklediği `CallArg*` bloğunun
sonuna:

```cpp
TEST(RuntimeExecTest, AssignIntLiteralIntoI64Variable) {
    // Same gap as call arguments, on the assignment path: an i32 literal
    // stored into an i64 slot.
    auto r = compileAndRun(R"--(
        func mkI64() -> i64 { return 1 }
        func main() {
            var n: i64 = mkI64()
            n = 7
            println(n)
        }
    )--", "assign_i64_literal");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "7\n") << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: Testi koş, kırmızı olduğunu doğrula**

```
cmake --build build-clang --target runtime_exec_test
build-clang/tests/runtime_exec_test.exe --gtest_filter='*AssignIntLiteralIntoI64*'
```

Beklenen: BAŞARISIZ. Gerçek çıktıyı (verifier hatası mı, yanlış değer mi)
rapora yaz. **Zaten geçiyorsa** bunu rapora yaz, testi olduğu gibi bırak
(gerileme bekçisi olarak yerini korur) ve Step 3'e yine de devam et —
dönüşüm eksikliği gerçek, yalnız bu forma yansımıyor olabilir.

- [ ] **Step 3: Atama dönüşümünü ekle**

`src/IR/IRGenCall.cpp`, `visitAssignExpr`. Identifier hedefli **skaler**
store'u bul — `vars_.namedValues` üzerinden alınan alloca'ya yazan,
`builder_->CreateStore(val, it->second);` biçimindeki satır (dosyada
`:913` civarı; dizi/eleman dalları DEĞİL). ŞU AN:

```cpp
            builder_->CreateStore(val, it->second);
```

BUNUNLA DEĞİŞTİR:

```cpp
            if (auto *coerced = coerceToElemType(
                    val, it->second->getAllocatedType(),
                    isUnsignedTypeRepr(node->getValue()->getResolvedType())))
                val = coerced;
            builder_->CreateStore(val, it->second);
```

`it->second`'ın tipi `llvm::AllocaInst *` değilse `getAllocatedType()`
derlenmez; o durumda sitedeki gerçek tipi kullan (ör. `llvm::Value*` ise
bir `llvm::dyn_cast<llvm::AllocaInst>` ile al) ve seçimini rapora yaz.

- [ ] **Step 4: Derle ve testleri koş (GREEN)**

```
cmake --build build-clang
build-clang/tests/runtime_exec_test.exe --gtest_filter='*CallArg*:*AssignIntLiteral*'
```

Beklenen: 5/5 PASSED.

- [ ] **Step 5: Tam süit**

```
ctest --test-dir build-clang --output-on-failure
```

Beklenen: **0 başarısız**.

- [ ] **Step 6: roadmap satırını güncelle**

`roadmap.md` — 2.3 bölümünde şu metinle başlayan satırı bul:

```
| Fonksiyon ARGÜMANLARI ve ATAMALAR Sema'da hiç tip denetimi görmüyor
```

Tüm satırı bununla değiştir:

```
| Fonksiyon argümanları ve atamalar hiç tip denetimi görmüyordu (çözüldü 2026-07) | Sema: `checkAssignable` (eski `checkArrayElement`) argüman ve atama yollarına bağlandı — denetim yalnız bildirimi çözülebilen doğrudan çağrılarda çalışır, builtin/closure/dinamik dispatch sessiz kalır. IRGen: dönüşüm site sayımı yerine BOĞAZ NOKTASINDA, callee'nin FunctionType'ına göre (`coerceCallArgs`) — böylece `takeI64(5)`/`takeF64(3)`/`takeU8(200)` gibi bugüne dek LLVM verifier hatası veren çağrılar çalışıyor. Atama yolunda da skaler dönüşüm eklendi | `TypeCheckerCall.cpp` `checkCallArgTypes`, `TypeChecker.cpp` `visitAssignExpr`, `IRGen.cpp` `coerceCallArgs`; 13 Sema + 5 koşum testi. Kapsam dışı kalanlar: native builtin argümanları, variadic paketleme, üye alan ataması |
```

- [ ] **Step 7: Commit**

```bash
git add src/IR/IRGenCall.cpp tests/unit/RuntimeExecTest.cpp roadmap.md
git commit -F- <<'EOF'
fix(irgen): identifier hedefli atamada değer hedef tipine dönüştürülüyor

Argüman yolundaki boşluğun atama karşılığı: i32 literal i64 yuvaya
dönüştürülmeden yazılıyordu.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
EOF
```

---

## Self-Review Notları

- **Spec kapsamı:** §1.1 genelleştirme → Görev 1; §1.2 argüman denetimi →
  Görev 2; §1.3 atama denetimi → Görev 3; §1.4 diagnostikler → Görev 2 ve
  3 Adım 1; §2.1 boğaz noktası → Görev 4; §2.2 atama dönüşümü → Görev 5;
  test stratejisi → her görevin test adımları; risk 4 (Sema ve IRGen aynı
  turda) → beş görev tek dalda.
- **Sıralama:** Görev 1 önce (2 ve 3 ona bağlı). Görev 2 ve 3 birbirinden
  bağımsız. Görev 4 ve 5 Sema'dan bağımsız ama aynı dalda olmalı.
- **Bilinçli belirsizlik bırakılan yerler:** Görev 2 Adım 5'te `Symbol`'ün
  fonksiyon-bildirimi alanının adı ve Görev 5 Adım 3'te alloca'nın tipi —
  ikisinde de implementer'a "gerçek adı bul, uydurma" talimatı ve raporlama
  yükümlülüğü verildi.
