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
| `??` bazı bağlamlarda yanlış (çözüldü 2026-07) | Yalnızca bare identifier + MemberExpr chain doğru; diğer LHS'ler (çağrı sonucu, subscript) unwrap edilmeden aynen dönüyor | `IRGenExpr.cpp:302-352`, fallback `:350-351` — fix: `emitOptionalCoalesce` helper'a genel LHS + sağ-assoc + Sema `cloneTypeRepr` |
| `??` RHS tipi LHS'in unwrap edilmiş inner tipine karşı hiç kontrol edilmiyor (yeni bulundu, 2026-07, izlemede) | Sema `NilCoalesce` sonuç tipini yalnızca RHS'ten türetiyor, LHS inner tipiyle karşılaştırmıyor; tip uyuşmazlığı derleme-zamanı diagnostiğe değil opak LLVM PHI verifier çökmesine gidiyor — ör. `a ?? 5 == 5` öncelik nedeniyle RHS'i `bool`'a çeviriyor (LHS `i32?`) | `TypeChecker.cpp:2127` civarı (`visitBinaryExpr`, `NilCoalesce` dalı) — tüm `??` yolları için simetrik/önceden var olan açık |
| Generik monomorfize metodların `T?` döndürmesi hatalı codegen üretiyor (çözüldü 2026-07) | `monomorphize()`/`monomorphizeMethod()` visitFuncDecl'in kurduğu per-function dönüş/coroutine durumunu (`currentFuncOptionalInner_`, `currentIsAsync_`, `currentCoro*`) hiç kurmuyor/sıfırlamıyordu — `-> T?` sarma atlanıyor, üstelik Optional/async caller'ın bayat durumu mono gövdeye sızıyordu | `IRGenMono.cpp` — fix: her iki mono yolunda kaydet→sıfırla→(ikame sonrası) kur→geri yükle; 5 RuntimeExec testi |
| DynArray `pop()` void'du — eleman değeri hiç dönmüyordu (çözüldü 2026-07) | IRGen `.pop()`'u salt `len--` olarak üretiyordu (yerel `[T]` + struct-alan yolları); `return self.items.pop()` değer dönen fonksiyonda `ret void` üretiyordu. LSP (`pop() -> T`) ve DAP interpreter zaten değer-dönüşlü sayıyordu | `IRGenCallMethod.cpp` — fix: `emitDynArrayPopValue` (guard'lı son-eleman load + `len--` + boşta zeroinit PHI); Sema pop dönüş tipi = eleman tipi |
| Async generik fonksiyonlar (`async func foo<T>`) hiç desteklenmiyor ama temiz diagnostik yok (önceden var, 2026-07 izlemede) | Mono yolu her zaman düz sync fonksiyon üretir; `await work(x)` çağrısı Task handle beklerken sync imza bulup verifier hatasıyla düşüyor (final inceleme probe'u). Ucuz fix: Sema'da "async generic functions are not supported" diagnostiği | `IRGenMono.cpp` (bilinçli `currentIsAsync_ = false`), Sema diagnostik eksik |
| Argümansız generik statik metod `T`'yi çözemiyor, sessizce yanlış derleniyor (yeni bulundu, 2026-07, izlemede) | `Stack.new()` (parametresiz) — T çıkarımı yalnız argüman tiplerinden; argümansızda mono sessizce başarısız, program derleniyor ama exit 0 + hiç çıktı üretmeden bitiyor. `var s: Stack<i32> = ...` anotasyonu, `Stack<i32>.new()` ve `Stack.new<i32>()` de çıkarımı sürmüyor (hepsi sessiz yanlış) | COOKBOOK §6 bu yüzden `new(first: T)` tohumlu biçime alındı; kalıcı çözüm: dönüş-tipi/anotasyon tabanlı tip-argüman çıkarımı + çözülemezse diagnostik |
| `return self` from `mut self` | Codegen yolu yok | — |
| Integer widening kısıtlı | Yalnızca sabit-literal tarafında; genel coercion modeli yok | `IRGenExpr.cpp:234-243` |
| String `==` struct-wrapper'da | Primitive `string` için çalışıyor (`IRGenExpr.cpp:192-203`); sorun wrapper bağlamında tip tanıma. Kalıcı çözüm: protokol tabanlı `Equatable` | `IRGenExpr.cpp:167-181` |
| `Optional<Named>` yeniden atamada (`=`) bir sonraki okuma stale/kaymış değer verir (yeni bulundu, 2026-07, izlemede) | `while`/loop içinde aynı `Optional<NamedStruct>` değişkenine düz `=` ile yeniden atama; sıralı `let` ile üretilen ayrı değişkenlerde sorun yok | Task 2 raporu (`.superpowers/sdd/task-2-report.md`), minimal repro dahil |
| Fonksiyon çağrısında değer-tipi Drop struct'ı hem çağıran hem çağrılan taraf drop'luyor (önceden var, bu branch'ten bağımsız, izlemede) | Argüman geçişi move olarak işaretlenmiyor — `take(a)` sonrası hem `take` içindeki parametre hem de çağıran taraftaki `a` scope-exit'te drop çağırıyor (aynı payload iki kez) | `probe_r2.liva` (final inceleme), `IRGenCall.cpp` argüman geçiş kodegen'i — henüz kök neden dosya/satır olarak izole edilmedi |
| `??` unwrap kopyası Drop'lu tipte double-drop üretiyor (önceden var olan tek-sahip-kopya sınıfı, 2026-07 izlemede) | `let r = o ?? fb` (o: `Res?`, Drop'lu) — PHI sonucu izlenmeyen bir KOPYA; hem `fb`/kaynak hem `r` aynı payload'u drop'luyor (`DROP` ×2). Roadmap #4'ün "kalan" kısmıyla (kopya/move genellemesi) aynı fix sahası | Final inceleme probe'u: `mk(-1) ?? fb` → iki `DROP 99` |
| `a ?? nil` LLVM verifier çökmesi (önceden var, izlemede) | Literal `nil` RHS'in resolved tipi yok → yeni Optional-RHS Sema guard'ı atlanıyor; ucuz fix: `NilLiteralExpr` kind kontrolü | `TypeChecker.cpp` NilCoalesce dalı |
| Match expression'da bare binding-pattern + guard (`n if ... =>`) codegen hatası (önceden var, `??`'den bağımsız, izlemede) | "internal: undefined variable 'n' in code generation" — expression-pozisyonlu match'te guard'lı bare binding | Final inceleme probe'u (2026-07-24) |
| `\|\|` ve `&&` kısa-devre YAPMIYOR (çözüldü 2026-07) — CondBr+PHI kısa-devre lowering'i + parser'da çağrı-sonrası `\|\|` trailing-closure disambiguation'ı (`\|\|` yalnız `{` izliyorsa closure) | IRGen ikili mantıksal operatör (`\|\|`/`&&`) codegen'i short-circuit semantiği uygulamıyor, her iki operandı koşulsuz değerlendiriyor | IRGen binary logical op codegen; keşif: `cli::cli` Task 4, probe'la doğrulandı |
| Yalnız bir if/else dalında (veya bir döngünün yalnız bazı iterasyonlarında) bildirilen `[T]` DynArray local'i, alınmayan/atlanan koldaki scope-cleanup'ın ilklenmemiş/çöp storage'ı free etmesiyle heap corruption (`STATUS_HEAP_CORRUPTION`, sessiz çökme — stdout flush edilmeden abort) üretiyordu (çözüldü 2026-07 — zeroInitEntryAlloca: container alloca'sı entry block'ta {null,0,0} ile sıfırlanıyor, ulaşılmayan yollar free(NULL) no-op) | DynArray Drop/scope-exit codegen'i, çalışmayan koldaki (veya atlanan iterasyondaki) `let`/`var: [T]` local'i için de temizleme kodu üretip başlatılmamış pointer'ı "free" ediyor | Keşif: `cli::cli` Task 4; workaround: değişkeni fonksiyon başında bir kez bildir, dallarda/döngüde yalnız yeniden ata |
| Bool olmayan operandlarla `&&`/`\|\|` temiz diagnostik yerine LLVM verifier hatasına gidiyor (önceden var, 2026-07 izlemede) | Kısa-devre lowering'i yalnız i1 LHS'te devreye girer; bool-olmayan operandlar (ör. `if let` dışında Optional değişkenle `o && flag`) legacy eager-bitwise yola düşüp `and %Optional, i1` verifier hatası üretir | Ucuz fix: Sema'da mantıksal operatörlere bool-operand kontrolü; `IRGenExpr.cpp` non-i1 fallback |
| Top-level `var x = 0` (global değişken) livac'ı SEGFAULT ettiriyordu (çözüldü 2026-07) | Kök neden: IRGen visitVarDecl null insert-block dereferansı. Fix: Sema'da err_global_var_unsupported diagnostiği (const-olmayan top-level VarDecl reddedilir; `const` destekli kalır) + IRGen savunma katmanı | `TypeChecker.cpp` TU döngüsü, `IRGenDecl.cpp` visitVarDecl guard; 4 pinning testi |
| Çağrı sonucu üzerinde doğrudan member zinciri (`strSplit(s, ",").length`, `m.keys().length`) "cannot resolve member" hatası veriyor (önceden var, izlemede) | Member çözümü yalnız değişken-tabanlı kayıtlardan (varDynArrayTypes vb.) besleniyor; çağrı-sonucu geçici değerler için resolvedType tabanlı yol yok | Workaround: ara `let` binding'i (anotasyonlu); LANGUAGE-REFERENCE notu mevcut |
| Untyped `func main()` içinde bir if dalındaki çıplak `return`, LLVM verifier hatası veriyor (`ret void` vs `i32` — "Function return type does not match operand type") (yeni bulundu, 2026-07, izlemede — final inceleme probe'uyla doğrulandı) | main'in örtük `i32` dönüş tipi ile gövdedeki değersiz `return`'ün `ret void` üretmesi çelişiyor; ya çıplak `return` main'de `ret i32 0`'a çevrilmeli ya da Sema diagnostiği eklenmeli | Keşif: cli::cli Task 5 cookbook doğrulaması; minimal repro: `func main() { if x == 1 { return } }` |
| Struct alanlarında `pub` token'ı parse edilmiyor (`pub var x` alan düzeyinde sözdizimi hatası veriyor; alanlar zaten `pub struct` altında dışarıdan erişilebilir olduğu için pratik etki kozmetik) — ya parser'a alan-düzeyi `pub` desteği eklenmeli ya da bu davranış dokümante edilmeli (yeni bulundu, 2026-07, izlemede) | Struct alan-döngüsü yalnızca `var`/`let` token'ını tüketiyor, `pub`'ı hiç kontrol etmiyor | `ParseDecl.cpp:386-391` |
| Map fonksiyon parametresi/dönüşü/struct alanı desteklenmiyor (izlemede) | Doğrulanmadı — henüz kök neden izole edilmedi | — |
| Aynı-süreç "CompilerInstance state kirliliği" şüphesi ve tüm Cli flake'leri (çözüldü 2026-07 — kök neden state değil, [string] eleman-depolama UAF'ıydı) | Gerçek kök neden: dizi slotuna string depolarken sahiplik kopyası alınmaması (yerel element-atama + üye push) — serbest bırakılan blok geri dönüşünce eleman "true" okunuyordu; taze-süreçte ~%5, aynı-süreçte ~%10. Fix: dizi, string elemanlarının KOPYASINA sahip (liva_str_dup, tüm depolama yolları); ayrıca üye-alan element-ataması sessiz no-op'tu, gerçek store eklendi. `SelfHostTest.StrSplit*` flake'i ayrı izlenmeli (bağlantı kanıtlanmadı) | `IRGenCall.cpp` visitAssignExpr, `IRGenCallMethod.cpp` mpush; doğrulama: probe 0/150 taze süreç, ctest tek-test 0/100 (önce 6/100), examples tek-süreç 30/30 |

| `-Werror` kapsam boşlukları (2026-07 final incelemesi, izlemede) | (1) `liva_runtime`/`liva_ui`/fuzz hedefleri `liva_set_compiler_flags` çağırmıyor — LIVA_WERROR açıkken bile uyarı bayrağı/gate almıyorlar; (2) CI koşucularında LLVM olmadığından `liva_irgen`/`codegen`/`jit`/`livac` CI'da derlenmiyor — IRGen uyarı kapısı fiilen yalnız yerel clang-cl/MinGW; (3) GNU dalındaki `-Wno-unused-variable/parameter` bastırmaları CI'da unused sınıfını yakalamıyor (yalnız yerel clang-cl /W4 yakalar) | `CMakeLists.txt:134,174,182`, `tests/CMakeLists.txt:212`, `cmake/CompilerFlags.cmake` — takip işi: kapsamı genişlet |

| Modül-içi Sema hataları "module 'X' not found" olarak maskeleniyordu (çözüldü 2026-07) | Fix: parse + Sema maskeleme siteleri err_module_error ile modülün gerçek hatalarını (dosya:satır gömülü) import noktasına iletiyor; not-found yalnız gerçek dosya-yokluğunda | `ModuleLoader.cpp` loadModule; 2 SemaTest + e2e livac build doğrulaması |

| İç içe dizi (`[[i32]]`) push'u heap corruption ile çöküyordu (çözüldü 2026-07 — `[[T]]` çekirdek desteği: literal/length/index/push/for-in/index-atama; eleman = inline 24B DynArray struct; for-in değişkeni borrow) | İç dizi elemanı `ptr` olarak lower ediliyordu ama 24-baytlık `%DynArray` struct'ı 8-baytlık slota (elemSize 8) store ediliyordu — hem üye (`self.rows.push(r)`, 0xC0000374) hem yerel (`rows.push(r)`, 0xC0000005) yol | `dynArrayElemLLVMType`/`deriveNestedDynArrayInner` merkezi yardımcıları (`IRGen.cpp`), 6 kayıt sitesi (`IRGenDecl.cpp`, `IRGenCall.cpp` `resolveMemberDynArray`), for-in loop-var iç kaydı + `movedVars` borrow işareti (`IRGenStmt.cpp`); 9 RuntimeExec pinning testi |
| `[[T]]` kaynak-mutasyonu UAF: `rows.push(a)` sonrası `a.push(9)` — realloc iç buffer'ı taşırsa `rows[0]` dangling (izlemede) | Push/index-atama DUP YAPMAZ (tasarım gereği "değer kopyası") — pushlanan struct DEĞERİ kaynakla AYNI `data ptr`'ı paylaşır (shallow-alias); kaynak daha sonra büyüyüp realloc olursa yalnız kaynağın kendi struct alanı güncellenir, dizi slotundaki KOPYA eski (serbest bırakılmış) buffer'a işaret etmeye devam eder | Probe-doğrulanmış (görev-2 review bulgusu); use-after-move diagnostiği yok — kaynağı push/atama sonrası salt-okunur/tüketilmiş saymak gerekir |
| Aynı `[[T]]` slotunun çift okunması çift-serbest-bırakma üretir: `let p = rows[0]; let q = rows[0]` (izlemede) | Her iki binding de aynı `data ptr`'ı SAHİP olarak kaydediyor (`daIt->second`'dan gelen struct değeri dup'lanmıyor) — ikisi de kendi scope-exit'inde aynı bloğu `free()`'liyor | Tasarım adayı: binding-as-borrow (if-let DynArray kalıbındaki "Borrowed... skip cleanup" desenine benzer bir `movedVars`/borrow işaretleme, ikinci+ okumalar için) |
| Tek-seviye `[T]` argümanının bir struct-metoda geçişinde aşırı-geniş arg-move — çağıranın buffer'ı her çağrıda sessizce sızıyor (izlemede) | Görev 2'nin double-free düzeltmesi (`movedVars.insert`) yalnız TEK bir struct-metod dispatch noktasında uygulandı; genel "değere göre argüman geçişi = move" kuralı genelleştirilmedi — davranış DOĞRU (çökme yok) ama her çağrıda çağıranın kaynak değişkeni artık hiç serbest bırakılmıyor (bellek profili değişti, tek-seferlik sızıntıdan çağrı-başına sızıntıya) | `IRGenCallMethod.cpp` struct-instance metod çağrı-dispatch'i (~15 farklı dispatch döngüsünden yalnız 1'i düzeltildi: class metodları, generic impl, zincirli üye-çağrılar dokunulmadı) |
| Dolu `[[T]]` struct-literal ALAN init'i sessizce yanlış veri üretiyordu — `Grid { rows: [[1,2],[3,4]] }` sonrası `g.rows[1]` bozuk, `g.rows[0]` doğru (maskeliyor) (çözüldü 2026-07) | Kök neden: `cloneIfDynArrayField` klon eleman boyutunu `toLLVMType(element)` ile hesaplıyordu — iç içe dizide element dinamik Array olduğundan 8 baytlık `ptr` dönüyor, oysa okuyucular 24 baytlık inline `%DynArray` adımlıyor; klon len*8 bayt kopyalıyordu (satır 0 kısmen geçerli, satır 1+ tampon dışı). Fix: merkezi `dynArrayElemLLVMType()` (7. kayıt sitesi) | `IRGenStmt.cpp:346` `cloneIfDynArrayField`; 2 koşum testi (alan init + index okuma, iç içe for-in) |
| Çok-seviyeli üye dizi element-ataması (`o.inner.vals[0] = x`) hâlâ sessiz no-op (2026-07'de tek-seviye düzeltildi, izlemede) | Yeni MemberExpr dalı yalnız `ident.field[i]` çözüyor (resolveMemberDynArray Identifier obje istiyor); okuma tarafı temiz derleme hatası verdiğinden sessiz-yanlış-okuma riski yok | `IRGenCall.cpp` visitAssignExpr MemberExpr dalı — genişletme adayı |

| Variadic fonksiyon ÇALIŞTIRMASI heap corruption ile çöküyordu (çözüldü 2026-07) | Çağıran variadic argümanları STACK'e paketliyor; callee kaydında borrow işareti olmadığından scope temizliği stack pointer'ına free() çağırıyordu. Fix: [T]-param kuralıyla aynı movedVars borrow işareti (tek satır) | `IRGenDecl.cpp` variadic kayıt bloğu; 2 koşum testi (boş variadic dahil) |
| Bağımsız generik fonksiyonun `[T]` parametresi/`T` dönüşü sessizce yanlış (önceden var, izlemede) | `func head<T>(xs: [T]) -> T` — `println(head(v))` BOŞ satır basıyor; anotasyonlu binding "expected 'i32', found 'T'" Sema hatası; impl-içi generikler (Stream) sağlam | TypeChecker dönüş-tipi propagasyonu — generic-mono-pitfalls ailesi |
| Kullanıcı fonksiyonunun `-> [T]?` dönüşü LLVM verifier hatası (önceden var, izlemede) | `insertvalue %Optional {i1, ptr}` payload'ı %DynArray struct — Optional lowering'i DynArray-inner'ı ptr sanıyor; native builtin'ler ([u8]? dönenler) farklı yoldan çalışıyor | Optional-of-DynArray lowering; repo'da kapsam yok |

| Variadic İLETİMİ sessizce yanlış (yeni erişilebilir oldu, 2026-07 izlemede) | `outer(values: i32...) { inner(values) }` derleniyor ama paketleme döngüsü DynArray değişkenini TEK skaler eleman sanıyor — kesilmiş pointer değeri basılıyor (probe: 6 beklenirken 426768356, exit 0) | `IRGenCall.cpp:304` varargs paketleme — DynArray-tipli argümanı spread/geçir olarak ele almalı |
| Impl-metodlarda variadic parametre desteklenmiyor — IRGen internal hatası (önceden var, izlemede) | Parse+Sema geçiyor, gövdede her kullanım "internal: undefined variable" — metod yolunda variadic kaydı da (IRGenDecl.cpp:2035 dışlıyor) çağrı-tarafı paketleme de (IRGenCallMethod) yok | Metod yolu kaydı + paketleme + temiz diagnostik gerekli |

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

1. **Generic `Map<K,V>`** — stdlib'de yalnızca monomorfik HashSet (I64/Str) ve BTreeMap varyantları var — **built-in Map tamamlandı (2026-07: coercion fix, size/isEmpty/clear/keys/values; for (k,v) zaten vardı)**
2. **CLI argüman parser** — `os.getArgs()` ham liste; flag/subcommand/usage çatısı yok — **tamamlandı (2026-07, cli::cli)**
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

`examples/` içinde ~55 örnek var ama **http, websocket, json, db/sqlite/postgres, regex, crypto, jwt, csv, toml, time, stream, sync için tek örnek yok** — son aylarda en çok emek verilen stdlib yüzeyi örneksiz — **tamamlandı (2026-07: 11 yeni örnek + ExamplesTest kapısı; examples/README.md)**. Artifact temizliği maddesi başından beri gereksizmiş: `.exe`/`.dll` zaten kök `.gitignore`'da (`*.exe`, `*.dll`, ayrıca fazladan `examples/*.exe`) ve `examples/` altında hiçbir artifact git'e tracked değil (doğrulandı: `git ls-files examples/ | grep -E '\.(exe|dll)$'` boş döner) — yerel wx DLL'leri (`wxbase331u_vc_x64_custom.dll` vb.) UI demo'larının çalışması için gerekli ve olduğu gibi kalmalı. Docs en/tr çift bakım — drift riski; COOKBOOK/TUTORIAL yeni fluent API'lerin (json/http/ws yeniden yazımları) gerisinde kalmış olabilir.

---

## Önerilen Öncelik Sırası

| # | İş | Tür | Etki |
|---|-----|------|------|
| 1 | `visitCallExpr`'ı parçala — **IRGenCall + TypeChecker tamamlandı (2026-07)** | Refactor | Tüm gelecek işlerin hızını artırır |
| 2 | Runtime ABI'yi tek `.def` tablosuna indir — **tamamlandı (2026-07, RuntimeFunctions.def)** | Refactor | 3 yönlü senkron hatası sınıfını yok eder |
| 3 | Yapısal Pattern AST + eksik pattern türleri — **Faz A+B tamamlandı (2026-07)** — kalan: struct destructuring, if-let tam pattern, editör gramerleri (ayrı işler) | Dil | En büyük dil sağlamlığı açığı |
| 4 | Atama/if-let move takibi — **tamamlandı (2026-07, muhafazakâr kapsam: Drop'lu tipler)** — kalan: clone(), atamada eski-değer drop'u, koleksiyon/alan drop'ları | Bug/Dil | Bilinen 3 memory hatasını kökten çözer |
| 5 | `??` operatörünü genel LHS'lerde doğru üret — **tamamlandı (2026-07, + sağ-assoc + lazy RHS)** | Bug | Sessiz yanlış davranış |
| 6 | Generic `Map<K,V>` + CLI arg parser | Stdlib | En görünür kullanıcı boşlukları |
| 7 | Networking/db/json örnekleri + artifact temizliği | Docs | Düşük maliyet, yüksek getiri — **tamamlandı (2026-07)** |
| 8 | CI'da `-Werror` + fixture-skip'leri hard-fail yap — **tamamlandı (2026-07, LIVA_WERROR + fixture hard-fail)** | Altyapı | Regresyon sızıntısını kapatır |
