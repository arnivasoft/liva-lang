# Move Takibi + Drop/Optional Düzeltmeleri — Tasarım (Roadmap #4)

Tarih: 2026-07-23
Kapsam: Bilinen 3 memory hatasının kök çözümü: (1) `let b = a` double-drop, (2) `b = a` atama double-drop, (3) `Optional<DropluStruct>` drop sızıntısı + if-let etkileşimi.
Onaylı karar (2026-07-23): **Muhafazakâr kapsam** — move takibi + drop bastırma YALNIZCA Drop protokolüne uyan struct'lar için. Düz struct'lar bugünkü kopya semantiğini aynen korur. `clone()` mekanizması ve tam Rust-style move kapsam DIŞI (roadmap'e ayrı madde).

## Tespitler

- `OwnershipChecker::markMoved` (`src/Sema/OwnershipChecker.cpp:204`) yalnızca çağrı argümanlarından besleniyor (`:150-156`); use-after-move/double-move diagnostikleri hazır ama `let b = a` / `b = a` hiç işaretlemiyor.
- IRGen'in ayrı `vars_.movedVars` seti scope temizliğini yönetiyor (`IRGenStmt.cpp:36-136` guard'ları); aynı boşluk → çift `*_drop`.
- Optional koşullu temizliği yalnızca `heapOptionalStringVars` şablonu (`IRGenStmt.cpp:128+`).
- **Task 0 DÜZELTMESİ (önceki varsayım yanlıştı):** `Optional<Named>` değişkenler `varStructTypes`'a kaydediliyor (`IRGenDecl.cpp:1091-1094`) ve `emitScopeCleanup`'ın koşulsuz döngüsü (`IRGenStmt.cpp:72`) bunlar için `T_drop`'u **yanlış pointer'la** (payload GEP'i değil Optional sarmalayıcı adresi) ve **nil olsa bile** çağırıyor — "hiç drop edilmiyor" değil, "yanlış drop ediliyor". Düzeltme bu yolu doğru koşullu-payload-drop'a yönlendirmeli.
- if-let'teki mevcut `movedVars.insert` (`IRGenStmt.cpp:506`) yalnızca `Optional<[T]>` (DynArray) için ve **binding'i** moved işaretliyor (hedef semantiğin tersi yönü); while-let'te hiçbir mekanizma yok; Drop'lu struct payload'ları if-let binding'inde `varStructTypes`'a kaydedilmiyor (binding hiç drop edilmiyor).
- **Sema/IRGen Drop-conformance asimetrisi:** Sema `protocolConformances_` yalnızca yerel impl'leri görüyor (`TypeChecker.cpp:1439`), import edilen modüllerin (json/websocket) Drop'ları görünmüyor; IRGen'in `dropImplementors_` seti (`IRGen.h:617`) her ikisini de kapsıyor (`IRGenDecl.cpp:1804-1808, 209-213`). Move semantiği iki katmanda aynı kümeyi görmeli — implementasyon Sema tarafına import-conformance propagasyonu ya da OwnershipChecker'a IRGen-eşdeğeri küme enjeksiyonu eklemeli (Sema.cpp:16-19 `classNames_` enjeksiyon deseni şablon).
- `isCopyType` TÜM named struct'lara false diyor — muhafazakâr kapsam için ayrı bir `isDropType` bayrağı şart (yoksa move semantiği düz struct'lara da taşar).

## Semantik (normatif)

**"Drop'lu tip"**: Drop protokolüne uyan (drop metodu üretilen — IRGen'de `<Struct>_drop` fonksiyonunun varlığı; Sema'da conformance kaydı) NAMED struct. Tespit mekanizmasının iki katmandaki kaynağı Task 0'da doğrulanır; iki katman AYNI kümeyi görmelidir.

1. **`let b = a`** (init, `a` identifier, tipi Drop'lu struct): move. Sema: `markMoved(a)` → sonraki kullanım `err_use_after_move`. IRGen: `vars_.movedVars.insert(a)` → scope çıkışında yalnız `b` drop edilir.
2. **`b = a`** (atama, aynı koşullar): move (aynı işaretlemeler). `b`'nin ÜZERİNE YAZILAN eski değerinin drop'u kapsam dışı — bilinen, dokümante sızıntı (sızıntı double-free'den güvenlidir); roadmap'e not düşülür.
3. **`Optional<T>` (T Drop'lu) scope çıkışı**: has-value bayrağı koşullu `T_drop(payload)` — `heapOptionalStringVars` şablonunun genellemesi. `movedVars` guard'ı aynen uygulanır.
4. **if-let/while-let**: binding payload'un sahipliğini ALIR: binding kendi scope'unda normal drop edilir; kaynak Optional değişkeni moved işaretlenir (payload için ikinci drop üretilmez). Mevcut 506 mekanizmasıyla ilişkisi Task 0 bulgusuna göre netleştirilir; davranış hedefi: değer yolu başına TAM 1 drop.
5. Drop'suz struct'lar, primitifler, string: davranış DEĞİŞMEZ (kopya + mevcut temizlik).
6. Diagnostik değişikliği: yeni diag YOK; mevcut err_use_after_move/err_double_move Drop'lu tipler için yeni noktalardan tetiklenir. Bu bilinçli bir davranış değişikliğidir (önceden sessiz kopyaydı) ve dokümante edilir.

## Doğrulama

TDD zorunlu. Drop sayacı deseni: drop metodu `println` ile iz bırakır; RuntimeExec testleri TAM drop sayısını assert eder (double-drop RED'de 2 görür, GREEN'de 1; Optional sızıntısı RED'de 0, GREEN'de 1; nil yolunda 0). SemaTest: move sonrası kullanım → err_use_after_move. Mevcut 2414 test yeşil kalır (Drop'lu tip kopyalayan mevcut test varsa Task 0 raporlar; davranış değişikliği onaylı olduğundan bu testler move semantiğine göre GÜNCELLENIR ve raporlanır).

## Kapsam dışı

`clone()`; tam Rust-style move; atamada eski değerin drop'u; koleksiyon elemanları/moved-out alanlar üzerinden drop; borrow-checker genişletmesi.
