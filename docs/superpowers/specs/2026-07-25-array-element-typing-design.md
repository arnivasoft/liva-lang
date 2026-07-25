# Dizi Eleman Tiplemesi — Tasarım

**Tarih:** 2026-07-25
**Durum:** Onaylandı (kapsam, dönüşüm politikası ve katman kararları kullanıcı tarafından onaylı)

## Problem

Liva'da dizi eleman değerleri, hedef yuvanın tipine **hiç dönüştürülmeden**
depolanıyor; Sema ise dizi literalinin elemanlarını ne birbiriyle ne de
anotasyonla karşılaştırıyor. Sonuç, sessiz yanlış veri ve heap taşmaları.

Probe ile doğrulanan mevcut davranış (2026-07-25, `livac` main @ 8f643f0):

| Kaynak | Bugünkü davranış | Kanıt |
|---|---|---|
| `let a: [i32] = [1, "a"]` | Derleniyor. `liva_array_new(4, 2)` 8 bayt ayırıyor, index 1'e 8 baytlık `ptr` yazılıyor → **4 bayt tampon dışı yazım** | `ha_1.ll` |
| `let a: [f64] = [1, 2]` | Derleniyor. 8 baytlık yuvalara `store i32` → `a[1]` **`0.000000`** | `hc_2.ll`, koşum |
| `var a: [f64] = [1.5, 2.5]; a.push(3)` | `a[2]` **`0.000000`** | `hd_3` koşumu |
| `let a: [i64] = [1, 2, 3]` | 8 baytlık yuvalara `store i32` → her yuvanın üst 4 baytı ilklenmemiş (taze `malloc` sayfaları sıfır olduğu için şu an doğru okunuyor) | `hc_1.ll` |
| `let a: [u8] = [1, 2, 3]` | 1 baytlık yuvalara `store i32` → **3 bayt tampon dışı yazım** | `hc_3.ll` |
| `var a: [u8] = []` + döngüde `a.push(i)` (`i: i32`) | **Sonsuz döngü / heap bozulması** | `hv_2` koşumu |
| `let a: [string] = [1, 2]` | Derleniyor, hiçbir uyarı yok | `ha_3` koşumu |

Kök neden tek cümlede: **eleman store'ları dönüşüm yapmıyor, Sema da
dönüştürülemeyecek elemanları reddetmiyor.**

## Kapsam Dışı

- Sabit dizilerin (`[T; N]`) boyut kontrolü.
- Map/Set eleman tiplemesi.
- `[dyn Protocol]` boxing kuralları (mevcut davranış korunur).
- Dizi literali dışındaki bağlamlarda (fonksiyon argümanı, `return`) anotasyon
  yönlendirmeli uyarlama. Bu bağlamlarda elemanlar yalnız **birbiriyle**
  birleştirilir; hedef tipe uyarlama Katman 2'nin coercion'ına bırakılır.

## Mimari

İki katman. Katman 1 hatayı **kaynakta** yakalar, Katman 2 geçerli olan
her dönüşümü **doğru** üretir. Katman 2 tek başına da sessiz-yanlış veri
sınıfını kapatır; Katman 1 dönüştürülemeyecek olanı reddeder.

### Katman 1 — Sema (`src/Sema/TypeChecker.cpp`)

#### 1.1 Uyum kuralı

Bir eleman ifadesinin hedef eleman tipi `T`'ye yazılabilirliği:

1. **Aynı tip** → uygun.
2. **Değer-koruyan sayısal dönüşüm** → uygun, sessiz. Her ifade için geçerli
   (değişken, çağrı sonucu, literal fark etmez).
3. **Kayıplı sayısal dönüşüm** → yalnız eleman bir tamsayı/ondalık
   **literal** ise ve değeri `T`'ye sığıyorsa uygun. Sığmıyorsa
   `err_array_element_literal_range`. Literal değilse
   `err_array_element_type_mismatch`.
4. **Diğer her şey** (string↔sayısal, bool↔sayısal, struct, dizi, optional
   uyuşmazlığı) → `err_array_element_type_mismatch`.

`bool` sayısal SAYILMAZ: `[i32] = [1, true]` hatadır.

**Değer-koruyan dönüşüm tablosu** (kaynak → hedef, hepsi sessiz):

| Kaynak | Değer-koruyan hedefler |
|---|---|
| `I8`  | `I8 I16 I32 I64 F32 F64` |
| `I16` | `I16 I32 I64 F32 F64` |
| `I32` | `I32 I64 F64` |
| `I64` | `I64` |
| `U8`  | `U8 U16 U32 U64 I16 I32 I64 F32 F64` |
| `U16` | `U16 U32 U64 I32 I64 F32 F64` |
| `U32` | `U32 U64 I64 F64` |
| `U64` | `U64` |
| `F32` | `F32 F64` |
| `F64` | `F64` |

Not: `I32 → F32` değer-koruyan DEĞİLDİR (24-bit mantis); `U32 → F32` de
değildir. Bunlar kayıplı sınıfa düşer, yani yalnız literalde geçerlidir.

**Literal aralık kontrolü**: `IntegerLiteralExpr` değeri hedef tamsayı
tipinin `[min, max]` aralığında olmalıdır. `FloatLiteralExpr` için aralık
kontrolü yapılmaz (yalnız `F32`/`F64` hedefleri kabul edilir; tamsayı
hedefine ondalık literal `err_array_element_type_mismatch`).

#### 1.2 `visitArrayLiteralExpr` — eleman birleştirme

Bugün gövde yalnız elemanları ziyaret ediyor. Yeni davranış:

1. Elemanları ziyaret et (mevcut).
2. Boş literal → `resolvedType` ATANMAZ (eleman tipi bilinmiyor; mevcut
   davranış korunur, IRGen'in boş-literal yolu değişmez).
3. İlk `resolvedType`'ı olan elemanı **aday** eleman tipi seç.
4. Kalan her eleman için: eleman adaya §1.1 kuralıyla yazılabiliyorsa
   devam; yazılamıyorsa ama **aday** o elemana yazılabiliyorsa adayı
   yükselt (ör. `[1, 2.5]` → aday `I32`, sonra `F64`'e yükselir — `1`
   literaldir ve `F64`'e sığar); ikisi de olmuyorsa
   `err_array_element_type_mismatch` bildir ve adayı değiştirme.
5. `node->setResolvedType(ArrayTypeRepr(clone(aday), dinamik))`.

Aday yükseltildiğinde daha önce kabul edilmiş elemanlar **yeniden
denetlenmez**: yükseltme yalnız eski aday yeni adaya yazılabiliyorsa
yapılır ve §1.1'in sayısal kafesi geçişlidir (eski adaya yazılabilen her
şey yeni adaya da yazılabilir).

#### 1.3 `visitVarDecl` — anotasyon yönlendirmeli kontrol

`node->getType()` bir `Array` **ve** init bir `ArrayLiteralExpr` ise, init
ziyaret edildikten hemen sonra:

1. Her elemanı anotasyonun eleman tipine göre §1.1 ile denetle, uyumsuzları
   bildir.
2. Literalin `resolvedType`'ını **anotasyonun tipinin klonu** yap.

(2) kritik: `TypeChecker.cpp:960-997`'deki mevcut genel anotasyon-uyum
kontrolü `typesCompatible(annType, initType)` çağırıyor. Literale §1.2'de
hesaplanan `[i32]` bırakılsaydı `let a: [i64] = [1,2,3]` için `[i64]` vs
`[i32]` karşılaştırması `err_type_mismatch` üretir ve **bugün çalışan kodu
kırardı**. Anotasyon tipini yazmak bu kontrolü etkisiz hale getirir ve
denetimin sahipliğini §1.3'e verir.

`[dyn Protocol]` anotasyonlu diziler bu kontrolden **muaftır** (eleman
boxing'i ayrı bir yol; mevcut davranış korunur).

#### 1.4 Yeni diagnostikler

`include/liva/Common/DiagnosticKinds.def`:

```
DIAG(err_array_element_type_mismatch, error, "array element of type '%0' cannot be stored in an array of '%1'")
DIAG(err_array_element_literal_range, error, "literal %0 does not fit in array element type '%1'")
DIAG(err_irgen_array_elem_coerce, error, "internal: cannot convert array element value to element type")
```

Üçüncüsü Katman 2'nin savunma diagnostiğidir (bkz. §2.1).

Konum **elemanın kendi** `getStartLoc()`'u olmalı, dizinin değil — bu
sayede eleman indeksini mesaja koymaya gerek kalmaz.

### Katman 2 — IRGen (`coerceToElemType`)

`IRGen`'e tek bir yardımcı:

```cpp
/// Bir değeri dizi eleman yuvasının LLVM tipine dönüştürür. Tipler zaten
/// eşitse değer aynen döner. Tamsayı genişletme/daraltma, tamsayı↔ondalık
/// ve ondalık genişletme/daraltma üretilir. Dönüşüm mümkün değilse
/// (ör. ptr ↔ i32) nullptr döner — Sema bunu zaten reddetmiş olmalıdır.
llvm::Value *coerceToElemType(llvm::Value *val, llvm::Type *slotTy);
```

Davranış:
- `val->getType() == slotTy` → `val`.
- ikisi de tamsayı: geniş→dar `CreateTrunc`, dar→geniş işaretliyse
  `CreateSExt` değilse `CreateZExt`. `i1` (bool) kaynak → `CreateZExt`.
- tamsayı → ondalık: `CreateSIToFP` (işaretsiz kaynakta `CreateUIToFP`).
- ondalık → tamsayı: `CreateFPToSI`.
- `float`↔`double`: `CreateFPExt` / `CreateFPTrunc`.
- diğer → `nullptr`.

İşaretlilik bilgisi LLVM tipinde yoktur; yardımcı ek bir `bool srcUnsigned`
parametresi alır (varsayılan `false`). Çağıranlar bu bilgiyi elde
edemedikleri yerlerde varsayılanı kullanır — mevcut davranışa göre
gerileme değildir, çünkü bugün hiç dönüşüm yok.

#### 2.1 Uygulama siteleri

| # | Dosya:satır | Bağlam | Yuva tipi kaynağı |
|---|---|---|---|
| 1 | `IRGenDecl.cpp:1448` | anotasyonlu `var/let` dizi literali init'i | `elemType` (anotasyondan) |
| 2 | `IRGenExpr.cpp:1149` | bağımsız dizi literali, ilk eleman | `elemType` (ilk elemandan) |
| 3 | `IRGenExpr.cpp:1155` | bağımsız dizi literali, kalan elemanlar | aynı `elemType` |
| 4 | `IRGenCallMethod.cpp:~489` | `arr.push(x)` (yerel dizi) | `daIt->second.elementType` |
| 5 | `IRGenCallMethod.cpp:~848` | `self.field.push(x)` (üye dizi) | `daInfo->elementType` |
| 6 | `IRGenCall.cpp:670` | `a[i] = x` (dinamik dizi) | `daIt->second.elementType` |
| 7 | `IRGenCall.cpp:702` | `a[i] = x` (sabit dizi) | sabit dizi eleman tipi |
| 8 | `IRGenCall.cpp:746` | `o.field[i] = x` (üye dizi) | `daInfo->elementType` |

Her sitede kural aynı: mevcut `CreateStore(val, ...)` çağrısından hemen
önce `val = coerceToElemType(val, slotTy)`; `nullptr` dönerse
`err_irgen_array_elem_coerce` (yeni, `internal:` önekli) bildirilip
`nullptr` döndürülür.

Site 2/3 özel durumu: yuva tipi ilk elemandan geldiği için, ilk eleman
`i32` ve ikincisi `f64` olan bir literal (`[1, 2.5]`) Sema'da adayı
`F64`'e yükseltir; ancak IRGen ilk elemanın LLVM tipini kullanır. Bu
nedenle site 2/3, yuva tipini **`node->getResolvedType()` varsa oradan**,
yoksa mevcut şekilde ilk elemandan alır.

`[dyn Protocol]` boxing yolu (site 1'in `isDynProtoElem` dalı)
dönüşümden **muaftır**: boxed değer zaten `%TraitObject` struct'ıdır.
`%DynArray` (iç içe dizi) elemanları da muaftır — struct tipleri için
`coerceToElemType` `val`'ı aynen döndürür (tipler eşit).

## Hata Yönetimi

- Sema hataları derlemeyi durdurur; IRGen'e ulaşmaz.
- IRGen'in savunma diagnostiği yalnız Sema'nın kaçırdığı bir yol için
  vardır; kullanıcıya `internal:` olarak görünür ve bir hata raporu
  işaretidir.
- Sema'nın rapor ettiği hatalar **eleman konumunu** gösterir, böylece uzun
  literallerde hangi elemanın sorunlu olduğu belli olur.

## Test Stratejisi

**SemaTest** (`hasDiag` ile DiagID kesin):
- `[i32] = [1, "a"]` → `err_array_element_type_mismatch`
- `[i32] = [1, true]` → `err_array_element_type_mismatch`
- `[string] = [1, 2]` → `err_array_element_type_mismatch`
- `[u8] = [1, 2, 300]` → `err_array_element_literal_range`
- `[u8] = [1, 2, 255]` → hata YOK
- `[i64] = [1, 2, 3]` → hata YOK
- `[f64] = [1, 2]` → hata YOK
- `[i64] = [n, 7]` (`n: i32` değişken) → hata YOK (değer-koruyan)
- `[u8] = [n]` (`n: i32` değişken) → `err_array_element_type_mismatch`
- `[1, 2.5]` (anotasyonsuz) → hata YOK, `resolvedType` `[f64]`
- `[]` (boş) → hata YOK

**RuntimeExecTest** (koşum, tam stdout):
- `[f64] = [1, 2]` → `a[1]` `2.000000`
- `[f64].push(3)` → `a[2]` `3.000000`
- `[i64] = [1,2,3]` → üç eleman da doğru
- `[u8] = [1,2,255]` → üç eleman da doğru, komşu dizi bozulmuyor
- `[u8]`e döngüde `push(i)` — **`i` bir i32 değişkeni olduğu için artık
  Sema hatası**; testin koşum varyantı `let b: u8 = 1` gibi doğru tipli bir
  kaynakla yazılır
- `[i32] = [1, 2]` ve `[string] = ["a","b"]` — gerileme yok
- İç içe `[[i32]] = [[1,2],[3]]` — gerileme yok

**Gerileme kapısı:** tam seri süit (`ctest --test-dir build-clang`), 2519
testin tamamı yeşil kalmalı. `[u8]` daraltma kuralı gzip/crypto stdlib
kullanımlarını etkileyebilir; süit bunu ortaya çıkarır.

## Riskler

1. **`resolvedType` atamak** en yüksek riskli adımdır: IRGen'de
   `getResolvedType()` okuyan çok sayıda yol var. Ayrı bir görev olarak
   uygulanır ve tam süitle doğrulanır.
2. **`[u8]` daraltma kuralı** mevcut stdlib kodunu kırabilir. Kırılırsa
   seçenek: `U8` hedefi için değişken kaynaklı `I32`'yi de değer-koruyan
   saymak yerine, ilgili stdlib çağrı yerlerini düzeltmek (doğru olan).
3. **İşaretlilik bilgisi kaybı**: `coerceToElemType` LLVM tipinden
   işaretliliği okuyamaz; `[u8]`den `[i32]`ye okuma yollarında zaten var
   olan işaret-genişletme davranışı (bkz. memory: gzip bayt işaretliliği)
   değişmemelidir. Bu nedenle yardımcı yalnız **yazma** yollarında kullanılır.
