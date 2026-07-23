# TypeChecker `visitCallExpr` Parçalama — Tasarım

Tarih: 2026-07-23
Kapsam: `src/Sema/TypeChecker.cpp` içindeki `TypeChecker::visitCallExpr` (satır 2134–3187, ~1.053 satır). Roadmap #1'in ikinci yarısı; IRGenCall split'inin (bkz. `2026-07-23-irgencall-split-design.md`) devamı.
Tür: Davranış-koruyucu saf refactor. Bug düzeltmesi yok; her adımda 2340 test yeşil.

## Tespit (IRGenCall'dan temel farklar)

- Fonksiyon `void` dönüşlü; **sıfır erken `return;`** — tamamen sıralı akış.
- **Sıfır üst-düzey paylaşılan yerel değişken**; tüm fazlar kendi if-blokları içinde kapalı.
- Dolayısıyla dispatch sözleşmesi (optional vb.) GEREKMEZ: fazlar sıralı `void` helper çağrılarına birebir dönüşür.

## Beş faz (yorum-işareti anchor'larıyla)

| # | Anchor (yorum) | ~Satır | Helper adı |
|---|---|---|---|
| 1 | `// Propagate function param types to untyped closure args` | 24 | `propagateClosureParamTypes` |
| 2 | `// Propagate DynArray element type to closure params for map/filter/forEach` | 42 | `propagateDynArrayClosureTypes` |
| 3 | `// Check argument count for user-defined functions / class constructors` | 74 | `checkCallArgCount` |
| 4 | `// Try to resolve return type from callee` | ~707 | `resolveCallReturnType` |
| 5 | `// Map/Set method call resolution: m.insert(), m.get(), m.contains(), m.remove()` | ~203 | `resolveMapSetMethodCall` |

Fonksiyonun ilk satırı `visit(node->getCallee());` dispatcher'da kalır.

## Hedef biçim

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

- Yeni dosya: `src/Sema/TypeCheckerCall.cpp` — iskelet: `#include "liva/Sema/TypeChecker.h"` + `namespace liva { ... }` (Sema'da LIVA_HAS_LLVM guard'ı yok).
- Bildirimler `include/liva/Sema/TypeChecker.h`'ye private `void` metodlar olarak eklenir (`visitCallExpr` bildiriminin yanına).
- `CMakeLists.txt` liva_sema hedefine (satır ~81) `src/Sema/TypeCheckerCall.cpp` eklenir.
- Faz gövdeleri VERBATIM taşınır (girinti dahil dokunulmaz). Faz 4 (~707 satır) önce tek helper olarak taşınır; 2.000 satır eşiğini zorlamadığından alt-bölme bu spec'in kapsamı dışındadır.

## Doğrulama (IRGenCall'da kanıtlanan reçete)

- Taşıma öncesi yapısal kontrol: her faz tam if-blok(lar)ından mı oluşuyor, fazlar arasında başıboş üst-düzey statement var mı.
- Her adım: verbatim taşı → CMake → `build_clang.bat` → `ctest` (SERİ, `-j` yok, foreground) 2340/2340 → commit.
- Sonda: silinen/eklenen satırların multiset mutabakatı sıfır farkla kapanmalı (iskelet satırları hariç).

## Kapsam dışı

- Faz 4'ün iç alt-bölmesi (gerekirse ayrı iş).
- TypeChecker.cpp'nin geri kalanının bölünmesi, builtin isim listesi tekilleştirmesi (roadmap'te ayrı).
- Her türlü davranış değişikliği.
