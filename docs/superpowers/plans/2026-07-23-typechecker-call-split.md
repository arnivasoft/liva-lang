# TypeChecker visitCallExpr Parçalama — Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `TypeChecker::visitCallExpr` (src/Sema/TypeChecker.cpp:2134-3187, ~1.053 satır) davranış değiştirmeden 5 sıralı `void` helper'a bölünür; yeni dosya `src/Sema/TypeCheckerCall.cpp`.

**Architecture:** Fonksiyon tamamen sıralı (sıfır erken return, sıfır paylaşılan üst-düzey yerel); her faz verbatim bir `void checkX(CallExpr*)` helper'ına taşınır, visitCallExpr 7 satırlık sıralı çağrı zinciri olur. Spec: `docs/superpowers/specs/2026-07-23-typechecker-call-split-design.md`.

**Tech Stack:** C++20, CMake + Ninja (`build_clang.bat`), GoogleTest (`ctest`, SERİ).

## Global Constraints

- **Davranış-koruyucu saf refactor**: mantık/koşul/mesaj/girinti DEĞİŞMEZ; bug görülürse raporlanır, düzeltilmez.
- Her task sonunda: `build_clang.bat` temiz + `ctest --test-dir build-clang --output-on-failure` (ASLA `-j`; FOREGROUND, 600000ms timeout — background+monitor YASAK) **2340/2340** (3 opt-in skip normal).
- Anchor olarak satır numarası değil faz yorum işaretleri kullanılır (taşımalar sonrası numaralar kayar). Plandaki numaralar taşıma-öncesi dosyaya aittir.
- Faz gövdeleri VERBATIM taşınır. Her faz "anchor yorumundan bir sonraki faz anchor'ının öncesine kadar" kesilir; trailing boş satır aralığa dahil edilir (1:1 boş-satır muhasebesi — IRGenCall Task 4+ kuralı).
- Commit formatı: `refactor(sema): <açıklama>` + zorunlu Co-Authored-By/Claude-Session fragmanları.

## Yeni dosya iskeleti

```cpp
#include "liva/Sema/TypeChecker.h"

namespace liva {

// ... helper tanımları ...

} // namespace liva
```

(Sema'da LIVA_HAS_LLVM guard'ı ve Intrinsics include'u YOKTUR — IRGenCall iskeletini kopyalama.)

## Faz haritası (taşıma-öncesi satırlar)

| Faz | Anchor | Aralık (~) | Helper |
|---|---|---|---|
| 1 | `// Propagate function param types to untyped closure args` | 2137–2160 | `propagateClosureParamTypes(CallExpr *node)` |
| 2 | `// Propagate DynArray element type to closure params for map/filter/forEach` | 2161–2202 | `propagateDynArrayClosureTypes(CallExpr *node)` |
| 3 | `// Check argument count for user-defined functions / class constructors` | 2203–2276 | `checkCallArgCount(CallExpr *node)` |
| 4 | `// Try to resolve return type from callee` | 2277–2983 | `resolveCallReturnType(CallExpr *node)` |
| 5 | `// Map/Set method call resolution: m.insert(), m.get(), m.contains(), m.remove()` | 2984–3186 | `resolveMapSetMethodCall(CallExpr *node)` |

`visit(node->getCallee());` (satır 2135) dispatcher'da kalır. Fonksiyonun kapanış `}`'si 3187.

## Nihai hedef biçim

```cpp
void TypeChecker::visitCallExpr(CallExpr *node) {
    visit(node->getCallee());
    propagateClosureParamTypes(node);
    propagateDynArrayClosureTypes(node);
    checkCallArgCount(node);
    resolveCallReturnType(node);
    resolveMapSetMethodCall(node);
}
```

---

### Task 0: Yapısal doğrulama

**Files:** yok (salt-okunur; commit yok)

- [ ] **Step 1: Faz sınırlarını doğrula** — `grep -n` ile 5 anchor yorumunun satırlarını bul; spec'teki sırayla ve yaklaşık aralıklarla eşleştiğini doğrula.
- [ ] **Step 2: Erken return yokluğunu doğrula** — `sed -n '2134,3187p' src/Sema/TypeChecker.cpp | grep -c "return;"` → beklenen **0**. 0 değilse DURDUR ve raporla (sıralı-void varsayımı çöker).
- [ ] **Step 3: Üst-düzey başıboş statement / paylaşılan yerel kontrolü** — aralıkta 4-boşluk girintili, `if`/`}`/`//` dışı satır var mı: `sed -n '2136,3186p' src/Sema/TypeChecker.cpp | grep -vE '^\s*(//|if |\}|\{|$)' | grep -E '^    [a-zA-Z]'`. Beklenen: boş. Çıktı varsa her satırı sınıflandır (faz-yerel lambda/tablo ise fazıyla taşınır; fazlar-arası paylaşılan yerel ise DURDUR, raporla).

---

### Task 1: Faz 1+2+3 → TypeCheckerCall.cpp

**Files:**
- Create: `src/Sema/TypeCheckerCall.cpp`
- Modify: `src/Sema/TypeChecker.cpp`, `include/liva/Sema/TypeChecker.h:57` civarı, `CMakeLists.txt:81` civarı

**Interfaces:**
- Produces: `void propagateClosureParamTypes(CallExpr *node); void propagateDynArrayClosureTypes(CallExpr *node); void checkCallArgCount(CallExpr *node);` (private, TypeChecker.h)

- [ ] **Step 1: TypeChecker.h'ye bildirimleri ekle** — `visitCallExpr` bildiriminin altına:

```cpp
    // --- visitCallExpr fazları (TypeCheckerCall.cpp) — sıralı, void, yan-etkiyle çalışır ---
    void propagateClosureParamTypes(CallExpr *node);
    void propagateDynArrayClosureTypes(CallExpr *node);
    void checkCallArgCount(CallExpr *node);
    void resolveCallReturnType(CallExpr *node);
    void resolveMapSetMethodCall(CallExpr *node);
```

(5 bildirimin tamamı şimdi eklenir; tanımlar task task gelir — linker yalnızca çağrılanları arar, bildirim fazlası zararsızdır.)

- [ ] **Step 2: TypeCheckerCall.cpp'yi oluştur** — iskelet + 3 helper; her helper gövdesi = kendi fazının anchor yorumu dahil, sonraki anchor'ın öncesine kadar VERBATIM:

```cpp
void TypeChecker::propagateClosureParamTypes(CallExpr *node) {
    // ... faz 1 VERBATIM ...
}

void TypeChecker::propagateDynArrayClosureTypes(CallExpr *node) {
    // ... faz 2 VERBATIM ...
}

void TypeChecker::checkCallArgCount(CallExpr *node) {
    // ... faz 3 VERBATIM ...
}
```

- [ ] **Step 3: TypeChecker.cpp'de fazları çağrılarla değiştir** — silinen 1-2-3 aralığının yerine:

```cpp
    propagateClosureParamTypes(node);
    propagateDynArrayClosureTypes(node);
    checkCallArgCount(node);
```

(Faz 4 anchor'ı bu üç satırın hemen altında kalır.)

- [ ] **Step 4: CMakeLists.txt liva_sema hedefine** `src/Sema/TypeCheckerCall.cpp` ekle (TypeChecker.cpp satırının altına).
- [ ] **Step 5: Derle** — `build_clang.bat`, temiz.
- [ ] **Step 6: Test** — seri ctest, 2340/2340.
- [ ] **Step 7: Commit** — `refactor(sema): visitCallExpr faz 1-3 TypeCheckerCall.cpp'ye taşındı`

---

### Task 2: Faz 4 → resolveCallReturnType

**Files:** Modify: `src/Sema/TypeCheckerCall.cpp`, `src/Sema/TypeChecker.cpp`

**Interfaces:**
- Consumes: Task 1'in dosyası ve bildirimleri.
- Produces: `resolveCallReturnType` tanımı.

- [ ] **Step 1: Faz 4'ü taşı** — `// Try to resolve return type from callee` anchor'ından `// Map/Set method call resolution` anchor'ının öncesine kadar VERBATIM, `TypeCheckerCall.cpp`'de `checkCallArgCount` tanımının altına `void TypeChecker::resolveCallReturnType(CallExpr *node) { ... }` olarak.
- [ ] **Step 2: TypeChecker.cpp'de yerine** `    resolveCallReturnType(node);` koy.
- [ ] **Step 3: Derle** → **Step 4: Test (2340/2340)** → **Step 5: Commit** — `refactor(sema): visitCallExpr faz 4 resolveCallReturnType'a taşındı`

---

### Task 3: Faz 5 → resolveMapSetMethodCall + nihai biçim

**Files:** Modify: `src/Sema/TypeCheckerCall.cpp`, `src/Sema/TypeChecker.cpp`

- [ ] **Step 1: Faz 5'i taşı** — anchor'dan fonksiyonun kapanış `}`'sinin öncesine kadar VERBATIM → `void TypeChecker::resolveMapSetMethodCall(CallExpr *node) { ... }`.
- [ ] **Step 2: visitCallExpr'ın nihai biçimini doğrula** — plandaki 7 satırlık hedefle birebir eşleşmeli.
- [ ] **Step 3: Derle** → **Step 4: Test (2340/2340)** → **Step 5: Commit** — `refactor(sema): visitCallExpr faz 5 taşındı — parçalama tamam`

---

### Task 4: Kapanış

**Files:** Modify: `roadmap.md`

- [ ] **Step 1: Multiset mutabakatı** — dal tabanından HEAD'e: TypeChecker.cpp'den silinen satırlar vs TypeCheckerCall.cpp'ye eklenen gövde satırları; fark yalnızca iskelet/imza/çağrı satırları olmalı. Sonucu raporla.
- [ ] **Step 2: Satır sayıları** — `wc -l src/Sema/TypeChecker*.cpp`; TypeChecker.cpp ~3.510, TypeCheckerCall.cpp ~1.075 beklenir.
- [ ] **Step 3: Tam seri test** — 2340/2340, sayı değişmemiş olmalı.
- [ ] **Step 4: roadmap.md** — öncelik tablosu 1. satırı: `` `visitCallExpr`'ı parçala — **IRGenCall + TypeChecker tamamlandı (2026-07)** `` yap.
- [ ] **Step 5: Commit** — `refactor(sema): TypeChecker visitCallExpr parçalama tamamlandı — 5 faz helper'ı`

## Sapma protokolü

IRGenCall planıyla aynı: test kırmızıysa task'ı geri al ve raporla; fazlar-arası paylaşılan yerel/erken-return keşfi = DURDUR; başıboş üst-düzey statement dispatcher'da bırakılır ve raporlanır.
