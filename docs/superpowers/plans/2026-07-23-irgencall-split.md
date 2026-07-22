# IRGenCall visitCallExpr Parçalama — Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `IRGen::visitCallExpr` (src/IR/IRGenCall.cpp:71–6598, ~6.500 satır) davranış değiştirmeden 8 domain dosyasına bölünür; visitCallExpr kısa bir dispatcher olur.

**Architecture:** Her domain bloğu `std::optional<llvm::Value*> tryEmitXxx(CallExpr*, const std::string&)` helper'ına **verbatim** taşınır (blok içi return'lere dokunulmaz; helper sonu `return std::nullopt;`). Dispatcher dolu optional görünce `return *r;` yapar. MemberExpr yolu tek parça halinde `tryEmitMethodCall(CallExpr*)`'a taşınır.

**Tech Stack:** C++20, LLVM 21, CMake + Ninja (`build_clang.bat`), GoogleTest (`ctest`, SERİ çalıştırma zorunlu).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-23-irgencall-split-design.md` — bu plan onun uygulamasıdır.
- **Davranış-koruyucu saf refactor**: mantık, koşul, hata mesajı, fallback DEĞİŞMEZ. Yolda görülen bug'lar düzeltilmez, commit mesajına değil `roadmap.md`'ye not edilir.
- Her task sonunda: `build_clang.bat` temiz derler + `ctest --test-dir build-clang --output-on-failure` (ASLA `-j` verme — paralel koşum bilinen race'leri tetikler) **2340/2340 yeşil**.
- Blok kodu taşınırken TEK değişiklik: girinti düzeltmesi bile YAPILMAZ, kod olduğu gibi kopyalanır.
- Taşınan aralık yalnızca **tam üst-düzey `if (...) { ... }` bloklarından** oluşmalı; blok dışında başıboş (side-effect'li) satır varsa taşıma DURDURULUR ve duruma göre o satır dispatcher'da bırakılır (plan sapması olarak rapor edilir).
- Satır numaraları her taşımadan sonra kayar — anchor olarak satır numarası değil **bölüm yorum işaretleri** (`// === Stdlib: X ===` vb.) kullanılır. Bu plandaki satır numaraları orijinal (taşıma öncesi) dosyaya aittir, ilk task'tan sonra yalnızca yönlendirme amaçlıdır.
- Yeni dosyaların hepsi aynı iskeleti kullanır (aşağıda "Dosya iskeleti").
- Commit mesajı formatı: `refactor(irgen): <açıklama>` + zorunlu Co-Authored-By/Claude-Session fragmanları.

## Dosya iskeleti (her yeni .cpp için)

```cpp
#include "liva/IR/IRGen.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/IR/Intrinsics.h>

namespace liva {

// ... helper tanımı(ları) buraya ...

} // namespace liva

#endif // LIVA_HAS_LLVM
```

## Domain → kaynak aralığı haritası (orijinal satırlar)

Aralıklar "bölüm başlangıç işareti → bir sonraki işaretin öncesi" kuralıyla kesilir.

| Helper | Dosya | Orijinal aralıklar |
|---|---|---|
| `tryEmitMethodCall(node)` | IRGenCallMethod.cpp | 74–2070 (MemberExpr yolu, tek parça) |
| `tryEmitCoreBuiltin(node, funcName)` | IRGenCallCore.cpp | 2106–2210 (len/toString/charToString/parse*), 4699–4706 (readLine), 4707–4784 (format), 4785–4947 (math), 6261–6313 (print/println) |
| `tryEmitSysBuiltin(node, funcName)` | IRGenCallSys.cpp | 2211–2257 (random), 2258–2306 (env/exit/args), 2307–2317 (clock), 2318–2400 (bench/sleep/isCancelled), 3783–3838 (dir), 3839–3976 (path), 3977–4056 (subprocess), 4275–4306 (logging), 4307–4364 (testing), 4365–4494 (datetime), 4495–4698 (encoding) |
| `tryEmitStringBuiltin(node, funcName)` | IRGenCallString.cpp | 2401–2584 (regex), 4948–5096 (string utils), 5097–5286 (bytes↔hex/base64url), 5287–5346 (UTF-8) |
| `tryEmitConcurrencyBuiltin(node, funcName)` | IRGenCallConcurrency.cpp | 2585–2638 (sync), 2639–2679 (rwlock), 2680–2777 (condvar), 2778–2838 (channel), 2977–2984 (taskgroup), 2985–3068 (task control), 3069–3093 (select/withTimeout), 3094–3117 (thread pool), 3118–3150 (async I/O) |
| `tryEmitDataBuiltin(node, funcName)` | IRGenCallData.cpp | 2839–2976 (TOML), 4057–4075 (JSON), 4076–4145 (JSON DOM), 4146–4274 (JSON DOM building) |
| `tryEmitNetBuiltin(node, funcName)` | IRGenCallNet.cpp | 3151–3782 (networking) |
| `tryEmitUIBuiltin(node, funcName)` | IRGenCallUI.cpp | 5347–6260 (UI) |

Dispatcher'da kalanlar: satır 72 (debug loc), 2072–2077 (funcName çıkarımı), 2079–2104 (class ctor), 6315–6598 (indirect closure call + kullanıcı fonksiyonu fallback'i).

Dispatcher'ın helper çağrı sırası (her helper'ın orijinaldeki İLK bloğunun sırasına göre): Method → [funcName çıkarımı] → [class ctor] → Core → Sys → String → Concurrency → Data → Net → UI → [kuyruk].

## Nihai dispatcher hedef biçimi (Task 9 sonunda)

```cpp
llvm::Value *IRGen::visitCallExpr(CallExpr *node) {
    if (diBuilder_) emitDebugLocation(node->getStartLoc());

    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        if (auto r = tryEmitMethodCall(node)) return *r;
    }

    // Get function name
    std::string funcName;
    if (node->getCallee()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getCallee());
        funcName = ident->getName();
    }

    // Class constructor call: ClassName(args) → overload resolution on arg count
    if (classNames_.count(funcName)) {
        /* 2080–2104 bloğu OLDUĞU GİBİ burada kalır */
    }

    if (auto r = tryEmitCoreBuiltin(node, funcName)) return *r;
    if (auto r = tryEmitSysBuiltin(node, funcName)) return *r;
    if (auto r = tryEmitStringBuiltin(node, funcName)) return *r;
    if (auto r = tryEmitConcurrencyBuiltin(node, funcName)) return *r;
    if (auto r = tryEmitDataBuiltin(node, funcName)) return *r;
    if (auto r = tryEmitNetBuiltin(node, funcName)) return *r;
    if (auto r = tryEmitUIBuiltin(node, funcName)) return *r;

    /* 6315–6598 kuyruğu OLDUĞU GİBİ burada kalır */
}
```

**ÖNEMLİ — MemberExpr sarmalama detayı:** Orijinal 74–2070 bloğu `if (callee == MemberExpr) { ...iç kod... }` şeklindedir ve iç kod bazı yollarda return etmeden if'ten düşer (fall-through → funcName çıkarımına devam). `tryEmitMethodCall` bu nedenle **if'in yalnızca GÖVDESİNİ** içerir; dispatcher'daki `if (callee == MemberExpr)` koşulu korunur. Gövde return etmeden helper sonuna düşerse `return std::nullopt;` → dispatcher devam eder = orijinal fall-through ile birebir aynı.

---

### Task 0: Ad-benzersizliği güvenlik doğrulaması

**Files:**
- Create: `C:\Users\Kadir\AppData\Local\Temp\claude\F--Cpp-Projects-liva-lang\6cd7e925-e4dd-4fea-ad87-b79eaee75c70\scratchpad\check_names.sh` (commit edilmez)

**Interfaces:**
- Produces: funcName literal envanteri — sonraki tüm task'ların ön koşulu. Bir ad birden fazla blokta geçiyorsa o bloklar AYNI helper'da orijinal göreli sırayla tutulmak zorundadır.

- [ ] **Step 1: funcName literal'lerini çıkar ve mükerrer kontrolü yap**

```bash
grep -oE 'funcName == "[A-Za-z0-9_]+"' src/IR/IRGenCall.cpp | sort | uniq -c | sort -rn | awk '$1 > 1'
```

Beklenen: **boş çıktı** = her ad tek blokta. Not: aynı blok içinde `funcName == "print" || funcName == "println"` gibi tekrarlar aynı satırdaysa sorun değildir; `awk '$1>1'` çıktısı verirse her birinin satır numaralarını `grep -n` ile bulup AYNI blokta mı ayrı blokta mı olduğunu elle doğrula. Ayrı bloklardaysa: bu planın domain haritasında ikisini aynı helper'a taşı ve sapmayı raporla.

- [ ] **Step 2: print/println çift kullanımını doğrula**

```bash
grep -n 'funcName == "print"' src/IR/IRGenCall.cpp
```

Beklenen: yalnızca 6262 civarında tek blok (`print` ve `println` aynı if'te). Başka satır çıkarsa Step 1'deki kurala göre işlem yap.

- [ ] **Step 3: Taşınacak aralıkların yapısal bütünlüğünü doğrula**

2106–6260 aralığında, üst-düzey `if` blokları DIŞINDA kalan (girintisi 4 boşluk olan ve `if`/`}` ile başlamayan) side-effect'li satır olup olmadığını kontrol et:

```bash
awk 'NR>=2106 && NR<=6260' src/IR/IRGenCall.cpp | grep -vE '^\s*(//|if |\}|\{|$)' | grep -E '^    [a-zA-Z]' | head -20
```

Beklenen: boş ya da yalnızca yorum/blok-içi satırlar. Üst-düzey başıboş statement görünürse: o satır ait olduğu bölge helper'ına taşınMAZ, dispatcher'da kalır; sapma raporlanır.

---

### Task 1: MemberExpr yolu → IRGenCallMethod.cpp

**Files:**
- Create: `src/IR/IRGenCallMethod.cpp`
- Modify: `src/IR/IRGenCall.cpp` (74–2070 gövdesi çıkar, dispatcher çağrısı koy)
- Modify: `include/liva/IR/IRGen.h` (bildirim, `visitCallExpr` bildiriminin hemen altına)
- Modify: `CMakeLists.txt:97` civarı (yeni dosya)

**Interfaces:**
- Produces: `std::optional<llvm::Value *> tryEmitMethodCall(CallExpr *node);` — sonraki task'lar dispatcher biçimini bunun üzerine kurar.

- [ ] **Step 1: IRGen.h'ye bildirimi ekle**

`include/liva/IR/IRGen.h:107` `visitCallExpr` bildiriminin altına:

```cpp
    // --- visitCallExpr domain helpers (IRGenCall*.cpp) ---
    // Dolu optional = çağrı işlendi (değer nullptr olabilir = hata);
    // std::nullopt = bu domain'in işi değil.
    std::optional<llvm::Value *> tryEmitMethodCall(CallExpr *node);
```

- [ ] **Step 2: IRGenCallMethod.cpp'yi oluştur**

"Dosya iskeleti"ni kullan; helper gövdesine IRGenCall.cpp 74–2070 aralığındaki `if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {` satırının **bir altından** kapanış `}`'sinin **bir üstüne** kadar olan iç gövdeyi VERBATIM yapıştır:

```cpp
std::optional<llvm::Value *> IRGen::tryEmitMethodCall(CallExpr *node) {
    auto *memberExpr = static_cast<MemberExpr *>(node->getCallee());
    const auto &methodName = memberExpr->getMember();
    // ... 74–2070 iç gövdesi VERBATIM ...
    return std::nullopt;   // orijinal fall-through
}
```

DİKKAT: Orijinal gövdenin ilk iki satırı zaten `auto *memberExpr = ...` ve `const auto &methodName = ...`'dir (satır 75–76) — bunları çoğaltma, gövdeyle birlikte gelirler.

- [ ] **Step 3: IRGenCall.cpp'de bloğu dispatcher çağrısıyla değiştir**

74–2070 aralığının tamamını şununla değiştir:

```cpp
    if (node->getCallee()->getKind() == ASTNode::NodeKind::MemberExpr) {
        if (auto r = tryEmitMethodCall(node)) return *r;
    }
```

- [ ] **Step 4: CMakeLists.txt'e dosyayı ekle**

`CMakeLists.txt` `liva_irgen` hedefine, `src/IR/IRGenCall.cpp` satırının altına: `        src/IR/IRGenCallMethod.cpp`

- [ ] **Step 5: Derle**

Run: `build_clang.bat` — Beklenen: hatasız link. Yaygın hata: gövdenin kullandığı statik/serbest yardımcılar (dosya-yerel fonksiyon/lambda) IRGenCall.cpp'de kalmış olabilir; öyleyse onları da IRGenCallMethod.cpp'ye taşı (yalnızca oradan kullanılıyorsa) ya da IRGen.h'de bildirilen ortak yardımcıya çevirme YAPMADAN durumu raporla.

- [ ] **Step 6: Testleri çalıştır**

Run: `ctest --test-dir build-clang --output-on-failure` — Beklenen: 2340/2340 PASS (3 opt-in skip normal).

- [ ] **Step 7: Commit**

```bash
git add src/IR/IRGenCallMethod.cpp src/IR/IRGenCall.cpp include/liva/IR/IRGen.h CMakeLists.txt
git commit -m "refactor(irgen): MemberExpr yolu tryEmitMethodCall'a taşındı (IRGenCallMethod.cpp)"
```

(Zorunlu commit fragmanlarıyla birlikte.)

---

### Task 2: Core builtin'leri → IRGenCallCore.cpp

**Files:**
- Create: `src/IR/IRGenCallCore.cpp`
- Modify: `src/IR/IRGenCall.cpp`, `include/liva/IR/IRGen.h`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1'in dispatcher biçimi.
- Produces: `std::optional<llvm::Value *> tryEmitCoreBuiltin(CallExpr *node, const std::string &funcName);`

- [ ] **Step 1: IRGen.h'ye bildirimi ekle** (Task 1'deki bloğun altına)

```cpp
    std::optional<llvm::Value *> tryEmitCoreBuiltin(CallExpr *node,
                                                    const std::string &funcName);
```

- [ ] **Step 2: IRGenCallCore.cpp'yi oluştur ve 4 aralığı taşı**

İskelet + helper; şu aralıklar bu sırayla VERBATIM gövdeye:
1. `// Handle len() built-in` işaretinden `// === Stdlib: Random ===` işaretinin öncesine kadar (orijinal 2106–2210),
2. `// Handle readLine() built-in` → `// === Stdlib: String utility functions ===` öncesi (4699–4947; readLine+format+math bitişiktir),
3. `// Handle print/println built-ins` bloğu (6261–6313, kapanış `}` dahil).

```cpp
std::optional<llvm::Value *>
IRGen::tryEmitCoreBuiltin(CallExpr *node, const std::string &funcName) {
    // ... taşınan bloklar VERBATIM ...
    return std::nullopt;
}
```

- [ ] **Step 3: IRGenCall.cpp'de taşınan aralıkları sil, dispatcher çağrısını ekle**

Class-ctor bloğunun (`if (classNames_.count(funcName))`) kapanışından hemen sonra:

```cpp
    if (auto r = tryEmitCoreBuiltin(node, funcName)) return *r;
```

- [ ] **Step 4: CMakeLists'e `src/IR/IRGenCallCore.cpp` ekle**

- [ ] **Step 5: Derle** — Run: `build_clang.bat`, beklenen: temiz.

- [ ] **Step 6: Test** — Run: `ctest --test-dir build-clang --output-on-failure`, beklenen: 2340/2340.

- [ ] **Step 7: Commit** — `refactor(irgen): core builtin'ler tryEmitCoreBuiltin'e taşındı (IRGenCallCore.cpp)`

---

### Task 3: Sys builtin'leri → IRGenCallSys.cpp

**Files:** Create: `src/IR/IRGenCallSys.cpp`; Modify: `src/IR/IRGenCall.cpp`, `include/liva/IR/IRGen.h`, `CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<llvm::Value *> tryEmitSysBuiltin(CallExpr *node, const std::string &funcName);`

- [ ] **Step 1: Bildirimi IRGen.h'ye ekle** (Task 2 kalıbıyla, adı `tryEmitSysBuiltin`)

- [ ] **Step 2: Aralıkları taşı** — işaret-anchor'lu, bu sırayla:
1. `// === Stdlib: Random ===` → `// === Stdlib: Regex ===` öncesi (2211–2400; random+process/env+clock+bench/sleep/isCancelled bitişiktir),
2. `// === Directory operations ===` → `// === JSON ===` öncesi (3783–4056; dir+path+subprocess bitişiktir),
3. `// === Logging ===` → `// Handle readLine() built-in` öncesi (4275–4698; logging+testing+datetime+encoding bitişiktir; readLine Task 2'de taşındığı için buradaki sınır artık `// === Stdlib: String utility functions ===` işaretidir).

- [ ] **Step 3: Dispatcher çağrısını ekle** — `tryEmitCoreBuiltin` satırının altına: `if (auto r = tryEmitSysBuiltin(node, funcName)) return *r;`

- [ ] **Step 4: CMakeLists'e ekle** → **Step 5: Derle** → **Step 6: Test (2340/2340)** → **Step 7: Commit** — `refactor(irgen): sys builtin'ler tryEmitSysBuiltin'e taşındı (IRGenCallSys.cpp)`

---

### Task 4: String builtin'leri → IRGenCallString.cpp

**Files:** Create: `src/IR/IRGenCallString.cpp`; Modify: `src/IR/IRGenCall.cpp`, `include/liva/IR/IRGen.h`, `CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<llvm::Value *> tryEmitStringBuiltin(CallExpr *node, const std::string &funcName);`

- [ ] **Step 1: Bildirim** (`tryEmitStringBuiltin`)
- [ ] **Step 2: Aralıkları taşı**:
1. `// === Stdlib: Regex ===` → `// === Stdlib: Synchronization ===` öncesi (2401–2584),
2. `// === Stdlib: String utility functions ===` → `// === Stdlib: UI (wxWidgets wrapper) ===` öncesi (4948–5346; string utils+bytes+UTF-8 bitişiktir).
- [ ] **Step 3: Dispatcher**: `if (auto r = tryEmitStringBuiltin(node, funcName)) return *r;` (Sys'in altına)
- [ ] **Step 4-7:** CMake → derle → test (2340/2340) → commit `refactor(irgen): regex+string builtin'ler tryEmitStringBuiltin'e taşındı (IRGenCallString.cpp)`

---

### Task 5: Concurrency builtin'leri → IRGenCallConcurrency.cpp

**Files:** Create: `src/IR/IRGenCallConcurrency.cpp`; Modify: `src/IR/IRGenCall.cpp`, `include/liva/IR/IRGen.h`, `CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<llvm::Value *> tryEmitConcurrencyBuiltin(CallExpr *node, const std::string &funcName);`

- [ ] **Step 1: Bildirim** (`tryEmitConcurrencyBuiltin`)
- [ ] **Step 2: Aralıkları taşı**:
1. `// === Stdlib: Synchronization ===` → `// === Stdlib: TOML ===` öncesi (2585–2838; sync+rwlock+condvar+channel bitişiktir),
2. `// === Stdlib: TaskGroup ===` → `// === Stdlib: Networking ===` öncesi (2977–3150; taskgroup+task control+select+threadpool+async I/O bitişiktir).
- [ ] **Step 3: Dispatcher**: String'in altına.
- [ ] **Step 4-7:** CMake → derle → test (2340/2340) → commit `refactor(irgen): concurrency builtin'ler tryEmitConcurrencyBuiltin'e taşındı`

---

### Task 6: Data builtin'leri → IRGenCallData.cpp

**Files:** Create: `src/IR/IRGenCallData.cpp`; Modify: `src/IR/IRGenCall.cpp`, `include/liva/IR/IRGen.h`, `CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<llvm::Value *> tryEmitDataBuiltin(CallExpr *node, const std::string &funcName);`

- [ ] **Step 1: Bildirim** (`tryEmitDataBuiltin`)
- [ ] **Step 2: Aralıkları taşı**:
1. `// === Stdlib: TOML ===` → `// === Stdlib: TaskGroup ===` öncesi (2839–2976),
2. `// === JSON ===` → `// === Logging ===` öncesi (4057–4274; JSON+DOM+building bitişiktir).
- [ ] **Step 3: Dispatcher**: Concurrency'nin altına.
- [ ] **Step 4-7:** CMake → derle → test (2340/2340) → commit `refactor(irgen): TOML+JSON builtin'ler tryEmitDataBuiltin'e taşındı`

---

### Task 7: Net builtin'leri → IRGenCallNet.cpp

**Files:** Create: `src/IR/IRGenCallNet.cpp`; Modify: `src/IR/IRGenCall.cpp`, `include/liva/IR/IRGen.h`, `CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<llvm::Value *> tryEmitNetBuiltin(CallExpr *node, const std::string &funcName);`

- [ ] **Step 1: Bildirim** (`tryEmitNetBuiltin`)
- [ ] **Step 2: Aralığı taşı**: `// === Stdlib: Networking ===` → `// === Directory operations ===` öncesi (3151–3782, tek parça).
- [ ] **Step 3: Dispatcher**: Data'nın altına.
- [ ] **Step 4-7:** CMake → derle → test (2340/2340) → commit `refactor(irgen): networking builtin'ler tryEmitNetBuiltin'e taşındı`

---

### Task 8: UI builtin'leri → IRGenCallUI.cpp

**Files:** Create: `src/IR/IRGenCallUI.cpp`; Modify: `src/IR/IRGenCall.cpp`, `include/liva/IR/IRGen.h`, `CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<llvm::Value *> tryEmitUIBuiltin(CallExpr *node, const std::string &funcName);`

- [ ] **Step 1: Bildirim** (`tryEmitUIBuiltin`)
- [ ] **Step 2: Aralığı taşı**: `// === Stdlib: UI (wxWidgets wrapper) ===` → `// Handle print/println built-ins` öncesi (5347–6260; print Task 2'de taşındıysa sınır artık kuyruktaki `// Check for indirect call through function-typed variable` yorumudur).
- [ ] **Step 3: Dispatcher**: Net'in altına.
- [ ] **Step 4-7:** CMake → derle → test (2340/2340) → commit `refactor(irgen): UI builtin'ler tryEmitUIBuiltin'e taşındı`

---

### Task 9: Son doğrulama ve kapanış

**Files:** Modify: `roadmap.md` (1. madde durumu)

- [ ] **Step 1: Dispatcher'ın nihai biçimini doğrula**

`visitCallExpr`'ın "Nihai dispatcher hedef biçimi" ile eşleştiğini oku-karşılaştır. Sapma varsa düzelt (yalnızca dispatcher iskeletinde — taşınmış kodda değil).

- [ ] **Step 2: Satır sayılarını raporla**

```bash
wc -l src/IR/IRGenCall*.cpp
```

Beklenen: IRGenCall.cpp ~1.900 satır (dispatcher + visitAssignExpr/visitMemberExpr/visitMatchExpr vb.); hiçbir yeni dosya ~2.100 satırı geçmez (Method ~2.000).

- [ ] **Step 3: Tam seri test + toplam sayı doğrulaması**

Run: `ctest --test-dir build-clang --output-on-failure`
Beklenen: 2340/2340 PASS. Test SAYISININ değişmediğini açıkça doğrula.

- [ ] **Step 4: roadmap.md 1. maddesini güncelle**

Öncelik tablosundaki 1. satıra IRGenCall tarafının tamamlandığı, TypeChecker tarafının beklediği notu düş:

```markdown
| 1 | `visitCallExpr`'ı parçala — **IRGenCall tamamlandı (2026-07)**, TypeChecker bekliyor | Refactor | ... |
```

- [ ] **Step 5: Commit**

```bash
git add roadmap.md
git commit -m "refactor(irgen): visitCallExpr parçalama tamamlandı — dispatcher + 8 domain dosyası"
```

## Sapma protokolü

- Task 0 mükerrer ad bulursa: blokları aynı helper'da tut, raporla.
- Taşıma sırasında blok dışı başıboş statement bulunursa: dispatcher'da bırak, raporla.
- Herhangi bir task'ta test kırmızıysa: `git checkout -- <değişen dosyalar>` ile o task'ı geri al, nedeni raporla; SONRAKI task'a geçme.
- Taşınan kodun kullandığı dosya-yerel (static/anon-namespace) yardımcı bulunursa: yalnızca tek helper kullanıyorsa birlikte taşı; birden çok dosya kullanacaksa taşımayı durdur ve raporla (IRGen.h'ye taşıma kararı kullanıcıya sorulur).
