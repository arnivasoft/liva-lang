# Liva-Lang: Kod Kalitesi ve Yeni Özellik Yol Haritası

> Tarih: 2026-07-22
> Kapsam: `src/` (~48 dosya, ~47.9k satır), `stdlib/` (49 `.liva`, ~6.3k satır), araçlar (LSP/DAP/REPL/PM), testler, dokümantasyon.
> Yöntem: Üç paralel derin inceleme — kod kalitesi/teknik borç, dil özellikleri tamlık analizi, stdlib/ekosistem.

## Genel Değerlendirme

Proje genel olarak sağlıklı: `#if 0` ölü kod bloğu yok, TODO/FIXME birikimi yok, diagnostik sistemi X-macro tablosuyla (`DiagnosticKinds.def`, 149 kayıt) tutarlı, bellek yönetimi `unique_ptr` ağırlıklı ve temiz, CI matrisi (Windows/Linux/macOS + ASan/UBSan + coverage + benchmark regresyonu) güçlü.

Asıl riskler üç noktada toplanıyor:

1. **Birkaç devasa fonksiyon/dosya** (özellikle ~6.500 satırlık `IRGen::visitCallExpr`)
2. **Üç yerde elle senkronize edilen runtime ABI**
3. **"Parse ediliyor ama enforce edilmiyor" durumundaki sığ dil özellikleri** (pattern matching, lifetime'lar)

---

## 1. Kod Kalitesi / Teknik Borç

### 1.1 Kritik: Devasa fonksiyonlar

- `IRGen::visitCallExpr` (`src/IR/IRGenCall.cpp:71`) tek fonksiyon olarak **~6.500 satır** — dosyanın 8.026 satırının çoğu. İçinde 1.400+ `builder_->Create*` çağrısı ve ~66 satır-içi builtin isim karşılaştırması var. Kod tabanındaki en büyük bakım riski. Kategori bazlı handler'lara (builtin, method dispatch, static ctor, async, UI callback...) bölünmesi en yüksek getirili refactor.
- `TypeChecker::visitCallExpr` ~1.050 satır (`src/Sema/TypeChecker.cpp:2134-3188`).
- `IRGen::visitVarDecl` ~900 satır (`src/IR/IRGenDecl.cpp:835-1742`); `visitFuncDecl` ~580; `visitClassDecl` ~480.
- 2.000 satırı aşan 5 dosya: `IRGenCall.cpp` (8.026), `TypeChecker.cpp` (4.564), `LSPServer.cpp` (4.178), `IRGenDecl.cpp` (3.241), `IRGen.cpp` (2.125).

### 1.2 Kritik: Runtime ABI üç kopya halinde

Her native runtime fonksiyonu üç ayrı yerde elle tanımlanıyor:

1. LLVM imzaları — `src/IR/IRGen.cpp` (483 `getOrInsertFunction`)
2. JIT kayıtları — `src/JIT/JITEngine.cpp` (`REG(...)` makrosu, 244 adet)
3. C++ tanımları — `stdlib/runtime/runtime.cpp`

Senkronizasyon tamamen konvansiyona dayanıyor ("createRuntimeDecls unutulursa getOrPanic çöküyor" dersi bu yapının belirtisi). Diagnostiklerde zaten kullanılan `.def` X-macro deseni buraya uygulanırsa üçü tek kaynaktan üretilebilir.

### 1.3 Diğer bulgular

- **Kopya builtin isim listeleri** (ör. `logDebug/logInfo/...`) 4-5 yerde: `TypeChecker.cpp:149` ve `:2579`, `ModuleLoader.cpp:111`, `LSPServer.cpp:1238`, `IRGenCall.cpp:4276`.
- **Sessiz fallback'ler** tip çözümleme hatalarını diagnostik yerine yanlış kod üretimine dönüştürebiliyor (ör. `IRGenCall.cpp:6402` `elemTR = makeI32Type(); // fallback`, `IRGen.cpp:2067` "treat as opaque pointer"). `-fno-exceptions` + sıfır assert ortamında en olası "sessiz miscompile" kaynağı bunlar.
- **Uyarı ayarları**: `/W4` ve `-Wall -Wextra -Wpedantic` açık ama hiçbir toolchain'de warnings-as-errors yok (`cmake/CompilerFlags.cmake:10` açıkça `/WX-`); `-Wno-unused-parameter` / `-Wno-unused-variable` global kapalı. En azından CI'da tek toolchain'e `-Werror` önerilir.
- **Sessiz test atlamaları**: `tests/unit/IntegrationTest.cpp` içinde fixture dosyası bulunamazsa 10 test `GTEST_SKIP` oluyor — fixture kaybolursa entegrasyon kapsamı fark edilmeden sıfıra düşer; hard-fail yapılmalı.
- Bellek yönetimi temiz: yalnızca 27 `new`/`delete` metni, gerçek raw allocation'lar LLVM Module sahipliğinde; 297 smart-pointer kullanımı.

---

## 2. Dil Özellikleri

### 2.1 En büyük yapısal zayıflık: Pattern matching

`parseMatchExpr` yapısal pattern AST'si kurmuyor — token metnini `=>` görene kadar **string olarak birleştiriyor** (`src/Parser/ParseExpr.cpp:731-739`), IRGen bu string'i yeniden parse ediyor (`src/IR/IRGenCall.cpp:7820-7906`).

- **Çalışan**: `_` wildcard, `Enum.Case`, `Enum.Case(bindings)` (iç içe enum dahil), integer-literal pattern, subject binding + guard (`s if s >= 90`).
- **Eksik**: tuple/struct destructuring, string/float literal pattern, range pattern, or-pattern (`A | B`), `@` binding.

Yapısal bir Pattern AST'sine geçiş, dil sağlamlığı açısından en değerli tek yatırım.

### 2.2 Parse edilen ama enforce edilmeyen özellikler

- **Explicit lifetime'lar (`'a`)**: lexer/parser/elision var ama `LifetimeAnalysis.cpp` toplam 154 satır, sadece scope-derinliği kontrolü yapıyor; lifetime parametrelerini hiç okumuyor, `visitReturn` boş (`LifetimeAnalysis.cpp:47-49`). Şu an tamamen kozmetik.
- **GATs**: associated-type plumbing'i var ama lifetime enforce edilmediği için anlamlı well-formedness kontrolü yok.

### 2.3 Bilinen hatalar — kodda doğrulandı (kök nedenleriyle)

| Hata | Kök neden | Konum |
|------|-----------|-------|
| `let b = a` double-drop (çözüldü 2026-07) | `markMoved` yalnızca fonksiyon argümanlarında çağrılıyor; `VarDecl` init ve atamada move takibi yok | `OwnershipChecker.cpp:149-154`, `IRGenStmt.cpp:72-88` |
| Drop, Optional+if-let'te çalışmıyor (çözüldü 2026-07) | Koşullu temizlik sadece `heapOptionalStringVars` için var | `IRGenStmt.cpp:135-162` |
| `??` bazı bağlamlarda yanlış | Yalnızca bare identifier + MemberExpr chain doğru; diğer LHS'ler (çağrı sonucu, subscript) unwrap edilmeden aynen dönüyor | `IRGenExpr.cpp:302-352`, fallback `:350-351` |
| `return self` from `mut self` | Codegen yolu yok | — |
| Integer widening kısıtlı | Yalnızca sabit-literal tarafında; genel coercion modeli yok | `IRGenExpr.cpp:234-243` |
| String `==` struct-wrapper'da | Primitive `string` için çalışıyor (`IRGenExpr.cpp:192-203`); sorun wrapper bağlamında tip tanıma. Kalıcı çözüm: protokol tabanlı `Equatable` | `IRGenExpr.cpp:167-181` |
| `Optional<Named>` yeniden atamada (`=`) bir sonraki okuma stale/kaymış değer verir (yeni bulundu, 2026-07, izlemede) | `while`/loop içinde aynı `Optional<NamedStruct>` değişkenine düz `=` ile yeniden atama; sıralı `let` ile üretilen ayrı değişkenlerde sorun yok | Task 2 raporu (`.superpowers/sdd/task-2-report.md`), minimal repro dahil |

### 2.4 Sağlam olduğu doğrulanan alanlar

Generator/yield codegen'i gerçekten tam (LLVM `coro.*` lowering, for-in, break-early destroy, runtime testli), protokol default metodları (Sema + codegen), operator overloading (aritmetik + karşılaştırma seti, `!=`/`>=`/`>` sentezi), `?` error propagation, closures (capture + heap env), sınıf sistemi (vtable, override, static, computed properties, `final`, `is`/`as?`).

### 2.5 Doğal sonraki dil özellikleri

1. Yapısal pattern matching (bkz. 2.1)
2. Gerçek borrow checker: atama/if-let'te move takibi + `'a` anotasyonlarını anlamlı kılacak outlives kontrolü (double-drop sınıfı hataları kökten çözer)
3. Drop bütünlüğü: Optional, koleksiyon elemanları ve moved-out alanlar üzerinden drop
4. Kullanıcı türetilebilir `Copy`/`Clone` protokolleri
5. `defer` ifadesi (şu an yalnızca `deinit`/Drop var)
6. Operator overloading genişletmesi: `[]`, `()`, bitwise/shift, unary neg, compound assignment
7. Bağlamdan literal tip çıkarımıyla genel sayısal coercion

---

## 3. Stdlib ve Ekosistem

### 3.1 Stdlib boşlukları (etki sırasıyla)

1. **Generic `Map<K,V>`** — stdlib'de yalnızca monomorfik HashSet (I64/Str) ve BTreeMap varyantları var
2. **CLI argüman parser** — `os.getArgs()` ham liste; flag/subcommand/usage çatısı yok
3. **Kripto eksikleri** — sadece hash/HMAC/JWT var; AES, parola hash'leme (argon2/bcrypt/pbkdf2), anahtar üretimi için secure random yok
4. **Math/random tamamlama** — atan2/asin/acos, exp, sinh/cosh/tanh, gcd/lcm, NaN/Inf sınıflandırma; seed'lenebilir RNG, gaussian, shuffle/choice
5. **Timezone desteği** — DateTime aritmetiği sağlam ama saat dilimi/ofset yönetimi yok
6. YAML (TOML var), bağımsız sıkıştırma modülü API'si, logging dosya/rotasyon hedefleri

### 3.2 UI modülü

Geniş widget seti mevcut (23+ widget, menü/toolbar, data binding Faz 6.x). Eksikler: dialog helper'ları (file/color/message), rich-text/HTML view, chart, TreeView/DataGrid için data binding, DataGrid model/sıralama/düzenleme kancaları, liste içi drag-reorder, erişilebilirlik katmanı.

### 3.3 Araçlar

- **LSP** çok kapsamlı (rename, semanticTokens, inlayHint, callHierarchy dahil). Eksikler: `prepareRename`, `typeDefinition`/`implementation`, range formatting, pull diagnostics, `didChangeWatchedFiles`.
- **DAP** ayrı bir AST yorumlayıcısı üzerinde (`DAPInterpreter.cpp`, 1.758 satır) — derlenen kodla davranış sapması riski yapısal; en azından dokümante edilmeli. Function/data breakpoint ve set-variable yok.
- **Paket yöneticisi** tüketici tarafında tam (SemVer `^`/`~`, registry sorgusu, `liva.lock`, transitive çözümleme) ama **publish komutu yok**, indirme checksum doğrulaması yok, çalışan registry sunucusu yok.
- **Playground** statik HTML; WASM backend'i yok.

### 3.4 Örnek/dokümantasyon açığı (düşük maliyet, yüksek getiri)

`examples/` içinde ~55 örnek var ama **http, websocket, json, db/sqlite/postgres, regex, crypto, jwt, csv, toml, time, stream, sync için tek örnek yok** — son aylarda en çok emek verilen stdlib yüzeyi örneksiz. Ayrıca `examples/` içine commit edilmiş `.exe`/`.dll` artifact'ları gitignore'a alınmalı. Docs en/tr çift bakım — drift riski; COOKBOOK/TUTORIAL yeni fluent API'lerin (json/http/ws yeniden yazımları) gerisinde kalmış olabilir.

---

## Önerilen Öncelik Sırası

| # | İş | Tür | Etki |
|---|-----|------|------|
| 1 | `visitCallExpr`'ı parçala — **IRGenCall + TypeChecker tamamlandı (2026-07)** | Refactor | Tüm gelecek işlerin hızını artırır |
| 2 | Runtime ABI'yi tek `.def` tablosuna indir — **tamamlandı (2026-07, RuntimeFunctions.def)** | Refactor | 3 yönlü senkron hatası sınıfını yok eder |
| 3 | Yapısal Pattern AST + eksik pattern türleri — **Faz A+B tamamlandı (2026-07)** — kalan: struct destructuring, if-let tam pattern, editör gramerleri (ayrı işler) | Dil | En büyük dil sağlamlığı açığı |
| 4 | Atama/if-let move takibi — **tamamlandı (2026-07, muhafazakâr kapsam: Drop'lu tipler)** — kalan: clone(), atamada eski-değer drop'u, koleksiyon/alan drop'ları | Bug/Dil | Bilinen 3 memory hatasını kökten çözer |
| 5 | `??` operatörünü genel LHS'lerde doğru üret | Bug | Sessiz yanlış davranış |
| 6 | Generic `Map<K,V>` + CLI arg parser | Stdlib | En görünür kullanıcı boşlukları |
| 7 | Networking/db/json örnekleri + artifact temizliği | Docs | Düşük maliyet, yüksek getiri |
| 8 | CI'da `-Werror` + fixture-skip'leri hard-fail yap | Altyapı | Regresyon sızıntısını kapatır |
