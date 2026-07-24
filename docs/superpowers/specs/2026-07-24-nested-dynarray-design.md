# İç İçe Dinamik Diziler ([[T]]) — Tasarım (Roadmap 2.3)

Tarih: 2026-07-24
Kapsam: `[[T]]` (dinamik dizi elemanlı dinamik dizi) çekirdek desteği — iç içe literal, push, index okuma, length, for-in. Bugün TAMAMEN kırık: literal `[[1,2],[3,4]]` OOB panik, `rows.push(r)` heap corruption (yerel 0xC0000005, üye 0xC0000374).

## Kök neden (doğrulanmış)

`toLLVMType(dinamik ArrayTypeRepr)` düz `ptr` (8B) döner (`IRGen.cpp:714-718`). Dış dizinin eleman tipi bu yolla `ptr`/elemSize 8 hesaplanırken, gerçek eleman DEĞERLERİ 24-baytlık `%DynArray {ptr,i64,i64}` struct'ları — her depolama/okuma yolu tutarsız.

## Temsil kararı

**İç içe dinamik dizinin elemanı, INLINE `%DynArray` struct'ıdır (24B).** Yani `[[i32]]` = DynArray< {ptr,i64,i64} >; dış buffer 24-baytlık struct'ları tutar. Gerekçe: fonksiyon parametreleri zaten `[T]`'yi struct-by-value taşıyor (mevcut kalıp); ekstra heap-box/indirection katmanı ve yeni runtime gerekmez; `liva_array_push/get` elemSize=24 ile değişmeden çalışır.

Sahiplik modeli (mevcut modelle tutarlı): dış dizinin scope-temizliği yalnız DIŞ buffer'ı serbest bırakır; İÇ buffer'lar bilinçli sızar (string elemanlarıyla aynı "arrays own copies, elements leak" profili). Push/index-read struct KOPYASI taşır — iç buffer pointer'ı paylaşılır (shallow copy; aliasing dokümante edilir: `let r: [i32] = rows[0]` iç buffer'ı paylaşır, `r.push` sonrası `rows[0].length` DEĞİŞMEyebilir — kapasite büyümesi realloc yaparsa kopyalar ayrışır; bu bilinen shallow semantiği LANGUAGE-REFERENCE'a yazılır).

## Kapsanan yüzey (çekirdek)

| İşlem | Davranış |
|-------|----------|
| `var rows: [[i32]] = []` / `= [[1,2],[3,4]]` | dış eleman tipi `getDynArrayStructTy()`, elemSize 24; iç literaller bağımsız DynArray struct değerleri olarak üretilip dış buffer'a yazılır |
| `rows.push(r)` (yerel + üye alan) | 24B struct kopyası push'lanır (r yerel `[T]` değişkeni veya literal) |
| `let first: [i32] = rows[i]` | struct değeri okunur; `first` normal DynArray olarak kaydedilir (length/index/push hepsi çalışır) |
| `rows.length`, `rows[i]` bounds | mevcut mekanizma elemSize 24 ile |
| `for r in rows` | eleman struct; döngü değişkeni DynArray olarak kayıtlı (iç length/index çalışır) |
| `rows[i] = r` (element-atama) | struct store (string-dup kuralı değil — struct kopyası zaten değer-kopyası) |

Sema: `[[T]]` tip anotasyonları zaten parse ediliyor; eleman tipi çözümü `visitIndexExpr`'da iç `[T]`'yi Array olarak vermeli (mevcut resolvedType akışı — Task 0 doğrular).

## Kapsam dışı (roadmap'te izlenir)

Üç+ seviye (`[[[T]]]`) — çekirdek mekanizma gereği çalışabilir ama test/garanti edilmez, probe sonucu dokümante edilir; iç dizilerin derin kopyası/deep-free; `[[string]]` (string elemanlı iç diziler — dup kuralı iç seviyede uygulanmaz, bilinçli sızıntı notu); `pop()` dönüşünün iç-dizi registrasyonu (çalışırsa bonus); nested `o.inner.rows[0] = x` çok-seviyeli atama (ayrı izleme satırı zaten var).

## Doğrulama

TDD; RuntimeExec testleri: iç içe literal + length + index okuma + iç eleman okuma; boş başlangıç + push (yerel VE üye alan) + geri okuma; for-in toplamı; index-atama; 200-iterasyonlu churn'lü corruption testi (UAF ailesi kalıbı — 0 beklenir); `[[string]]` ve `[[[T]]]` probe sonuçları rapora (test edilmez, davranış dokümante edilir). Tam seri suite taban 2498 + yeniler. LANGUAGE-REFERENCE (EN+TR) kısa "Nested arrays" notu (shallow-copy semantiği dahil).
