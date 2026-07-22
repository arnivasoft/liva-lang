# IRGenCall `visitCallExpr` Parçalama — Tasarım

Tarih: 2026-07-23
Kapsam: Yalnızca `src/IR/IRGenCall.cpp` içindeki `IRGen::visitCallExpr` (satır 71–6598, ~6.500 satır). `TypeChecker::visitCallExpr` bu çalışmanın dışında, ayrı bir iş olarak planlanacak.
Tür: Davranış-koruyucu saf refactor. Yol üstünde görülen bug'lar (ör. `??` fallback'i, sessiz `makeI32Type()` fallback'leri) düzeltilmez, not edilir. Her adımdan sonra 2340 test yeşil kalmalı.

## Mevcut yapı (tespit)

`visitCallExpr` iki ana bölgeden oluşuyor:

- **MemberExpr yolu** (satır 74–2070, ~2.000 satır): UI event fast-path (closure literal → heap env), `Result.ok/err` ctor, enum case ctor, method dispatch, chained calls. Bloklar birbirine dolanık ve **sıra bağımlı**.
- **Düz builtin zinciri** (satır 2074–6598, ~4.500 satır): `funcName == "..."` karşılaştırmalı, `// === Stdlib: X ===` domain yorumlarıyla gruplu, ad bazında karşılıklı dışlayıcı bloklar. Bölmesi güvenli.

## Dosya düzeni (yeni 8 dosya)

| Dosya | İçerik | Kaynak (~satır) |
|---|---|---|
| `IRGenCallMethod.cpp` | MemberExpr yolu tek parça (UI fast-path, Result/enum ctor, method dispatch) | 74–2070 |
| `IRGenCallCore.cpp` | len, toString, charToString, parseInt/parseInt64/parseFloat, print/println, readLine, format, math builtin'leri | ~600 |
| `IRGenCallSys.cpp` | random, env/exit/args, clock/bench*, sleep, dizin/path işlemleri, subprocess, logging, testing, datetime, encoding/compression | ~1.100 |
| `IRGenCallConcurrency.cpp` | sync (Mutex/Atomic), rwlock, condvar, channel, taskgroup, task control, select/withTimeout, thread pool, async I/O | ~550 |
| `IRGenCallNet.cpp` | networking (Url, HTTP, WebSocket builtin'leri) | 3151–3783 |
| `IRGenCallData.cpp` | TOML, JSON, JSON DOM (parse-tree + mutation) | ~500 |
| `IRGenCallString.cpp` | regex, string utility'leri, bytes↔string/hex/base64url dönüştürücüler, UTF-8 helper'ları | ~580 |
| `IRGenCallUI.cpp` | UI (wxWidgets wrapper) builtin'leri | 5347–6260 |

`IRGenCall.cpp`'de kalanlar: kısa dispatcher `visitCallExpr` + dosyadaki diğer fonksiyonlar (`visitAssignExpr`, `visitMemberExpr`, `visitStructLiteralExpr`, `visitMatchExpr`, `emitEnumCaseConstruct`, `emitNestedPatternMatch`, bounds-check helper'ları; satır 6599–8026).

Sonuç: hiçbir dosya ~2.000 satırı geçmez.

## Helper sözleşmesi

```cpp
// Dolu optional  = çağrı bu domain'e ait; taşıdığı değer sonuçtur
//                  (hata durumunda dolu-ama-nullptr olabilir)
// std::nullopt   = bu domain'in işi değil, dispatcher sıradakini dener
std::optional<llvm::Value *> tryEmitSysBuiltin(CallExpr *node,
                                               const std::string &funcName);
```

- `std::optional<llvm::Value*>` seçiminin nedeni: taşınan blok içindeki `return X;` ve `return nullptr;` ifadeleri **hiç değiştirilmeden** kalır (`nullptr` örtük dönüşümle dolu-ama-null optional olur = "işlendi, hata"); yalnızca helper sonuna `return std::nullopt;` eklenir. Dispatcher ayrımı `if (auto r = tryEmitX(...)) return *r;` ile yapar (dolu-ama-null optional da `true` dallanır). Alternatif `bool + out&` imzası bloklardaki yüzlerce return'ün yeniden yazılmasını gerektirdiğinden verbatim ilkesiyle çelişirdi.
- `funcName` dispatcher'da bir kez çıkarılır (IdentifierExpr callee), free-function helper'larına parametre geçilir.
- `tryEmitMethodCall` (MemberExpr yolu) yalnızca `CallExpr*` alır; `funcName` kavramı o yolda yok.
- Helper bildirimleri `include/liva/IR/IRGen.h` içine private metod olarak eklenir.

## Dispatcher ve sıralama garantisi

- `visitCallExpr` ~60 satıra iner. Dispatcher sırası: (1) MemberExpr yolu (`tryEmitMethodCall`), (2) class-ctor kontrolü (mevcut 2080 bloğu, dispatcher'da kalır), (3) funcName zinciri helper'ları, (4) kullanıcı fonksiyonu fallback'i. (1) ve (2)'nin zincirden önce gelmesi orijinal yapıyla birebir aynıdır.
- **Helper içi** blokların göreli sırası orijinaldeki gibi korunur.
- **Helper'lar arası** sıra orijinalden farklılaşır (domain blokları kaynakta bitişik değil: ör. Sys domain'i 2211–2361 ve 3783–4698 aralıklarına dağılmış). Bu güvenlidir çünkü funcName zincirindeki her blok **tam ad eşitliğiyle** (`funcName == "..."`) eşleşir; farklı adlı bloklar karşılıklı dışlayıcıdır. Ek koşullu bloklar (`!args.empty()`, `currentIsAsync_` vb.) eşleşmezse orijinalde de aynı şekilde kullanıcı-fallback'ine düşer.
- **Güvenlik ağı**: taşımaya başlamadan önce zincirdeki tüm `funcName == "<ad>"` literal'leri script ile çıkarılır ve hiçbir adın birden fazla blokta geçmediği doğrulanır. Bir ad iki blokta geçiyorsa o bloklar aynı helper'da, orijinal göreli sırayla tutulur.
- Blok kodu **verbatim taşınır** — mantık, koşul, hata mesajı, fallback hiçbir şekilde değişmez.

## Uygulama ve doğrulama stratejisi

Her adım = tek domain çıkarımı:

1. Blokları yeni dosyaya taşı (verbatim), helper imzasını ekle.
2. `CMakeLists.txt`'e yeni dosyayı ekle.
3. `build_clang.bat` ile derle.
4. `ctest --test-dir build-clang --output-on-failure` (seri) çalıştır.
5. Yeşilse commit; kırmızıysa adımı izole geri al.

Toplam ~9 küçük commit. Riskli tek parça MemberExpr yolu olduğundan **tek birim halinde** taşınır, iç yapısına dokunulmaz.

## Kapsam dışı

- `TypeChecker::visitCallExpr` parçalama (ayrı iş).
- Dispatch tablosu / builtin kayıt mimarisi (gelecekte değerlendirilebilir; bu refactor onun önkoşulunu hazırlar).
- Runtime ABI `.def` tablosu birleştirmesi (roadmap #2).
- Herhangi bir bug düzeltmesi veya davranış değişikliği.
