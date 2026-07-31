# Ömür Analizi Faz Kapsamı — Tasarım

**Tarih:** 2026-07-31
**Durum:** Onaylandı (kapsam bölünmesi, yaklaşım ve ölçüm-önce süreci kullanıcı tarafından onaylı)
**Dal:** `fix/lifetime-phase-coverage`
**İlgili kayıt:** `roadmap.md:134` AYRI İŞ maddeleri (7) ve (8)

## Problem

Sema üç fazdan oluşuyor ve üçüncüsü diğer ikisinden farklı davranıyor.
`src/Sema/Sema.cpp:31-35`:

```cpp
// Phase 3: Lifetime analysis (scope-based borrow checking)
for (auto &decl : tu.getDeclarations()) {
    if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
        lifetimeAnalysis_.analyzeFunction(static_cast<FuncDecl *>(decl.get()));
    }
}
```

Yalnız **top-level** `FuncDecl`'ler analiz ediliyor. `impl`, `class` ve protokol
metot gövdeleri ömür analizinin tamamen dışında.

Probe ile ölçüldü (`livac --check-only` @ `04ef33c`) — birebir aynı gövde, tek
fark nerede durduğu:

| Program | Sonuç |
|---|---|
| top-level `func run() { var p = ref r; { var inner = 99; p = ref inner } }` | ✅ `error: borrow of 'inner' outlives the value` |
| aynı gövde `impl H { func run(self) { … } }` içinde | ❌ sessizce derleniyor |

Bu, önceki dalın (`fix/ownership-traversal-gaps`) `OwnershipChecker` için
kapattığı boşluğun tam ikizi — bir faz ötede. O iş `impl` gövdelerini ownership
denetimine soktu; ömür analizi hâlâ dışarıda ve bu tutarsızlığı görünür kılan da
o iş oldu.

### İkinci, iç içe boşluk

`LifetimeAnalysis::visitNode` (`src/Sema/LifetimeAnalysis.cpp:21-51`) yalnız
`BlockStmt`, `VarDecl`, `ExprStmt` (yalnız `AssignExpr` alt-durumu), `IfStmt`,
`WhileStmt`, `ForStmt` ve `ReturnStmt`'i ele alıyor; geri kalan her şey
`default: break;`'e düşüyor. Yani blok İÇEREN ama listede olmayan deyimler —
`IfLetStmt`, `WhileLetStmt`, `MatchExpr` arm gövdeleri — hiç gezilmiyor ve
oralardaki `var p = ref x` bağlamaları ile yeniden atamaları görülmüyor.

Giriş boşluğu (a) ile iç gezinti boşluğu (b) aynı analizin aynı kusuru: analiz
ulaşamadığı yerde susuyor.

### Üçüncü, ilgili ama farklı: ölü API

`Sema::typeCheck` ve `Sema::ownershipCheck` (`src/Sema/Sema.cpp:39-47`) faz
kurulumunu atlıyor — `ownershipCheck`, `analyze`'ın yaptığı
`setClassNames`/`setDropTypeNames` çağrılarını yapmıyor, dolayısıyla class
muafiyetini ve Drop move-semantiğini hiç görmez. Ama **bu bir hata değil, ölü
kod**: tüm repoda hiçbir çağıranı yok (`grep` ile doğrulandı; tek giriş
`Sema::analyze`, `src/Driver/CompilerInstance.cpp`'de 5 yerde).

## Hedef

Ömür analizi, ownership denetiminin ulaştığı her fonksiyon gövdesine ulaşsın; iç
gezintisi blok içeren deyimleri atlamasın; ve Sema'nın kullanılmayan ikinci
giriş yüzeyi temizlensin.

**Bu iş bir ölçüm turu içerir.** Analiz edilen yüzey bugüne kadar muaf olduğu
için kaç yeni ret üreteceği bilinmiyor; kapsam kararı ölçümden sonra
kullanıcıya getirilir.

## Kapsam Dışı

- **`LifetimeAnalysis`'in kendisini güçlendirmek.** Analiz bugün basit (kapsam
  derinliği + `refTarget`); daha güçlü bir ömür modeli gerçek CFG işidir
  (roadmap 2.5 #2) ve ayrı bir projedir. Bu iş yalnız ULAŞILABİLİRLİK açıyor,
  kuralı değiştirmiyor.
- **Computed property'ler.** Kayıt 134'ün (1) numaralı maddesi
  (`TypeChecker::visitClassDecl` alan gövdelerini gezmiyor) bu işin dışında
  bırakıldı, çünkü keşifte özelliğin **uçtan uca kırık** olduğu ölçüldü — bir
  gezinti düzeltmesiyle çözülmez. Aşağıdaki "Yeni roadmap kaydı"na bakın.
- **`FieldDecl` getter/setter gövdeleri.** Bunlar `FuncDecl` değil `BlockStmt`;
  yukarıdaki gerekçeyle doğal olarak kapsam dışında kalıyorlar.
- **Genel `forEachChild` fallback'i `visitNode` içinde.** Gerekçe aşağıda.

## Yaklaşım

Faz 1 ve 2 TU'yu alıyor (`typeChecker_.check(tu)`, `ownershipChecker_.check(tu)`);
faz 3 ise `Sema.cpp`'de kendi döngüsünü yazıyor. Asimetri davranışta olduğu
kadar imzada da var.

Seçilen yol: **`LifetimeAnalysis`'e TU girişi vermek.** Gezinti analizin kendi
içinde yapılır, `Sema.cpp` üç satırlık bir faz listesine döner.

Değerlendirilip reddedilen iki alternatif: gezintiyi `Sema.cpp`'de yapmak (faz
listesi kalabalık kalır ve gezinti bilgisi orkestrasyon dosyasına sızar);
bildirim türü başına elle döngü yazmak (`src/AST/ASTWalk.cpp`'deki çocuk
tablosunun zaten bildiği şeyi tekrarlar).

## Bileşenler

Değişen dosyalar: `include/liva/Sema/LifetimeAnalysis.h`,
`src/Sema/LifetimeAnalysis.cpp`, `include/liva/Sema/Sema.h`,
`src/Sema/Sema.cpp`. Yeni dosya yok.

### ① `LifetimeAnalysis::check(TranslationUnit &tu)`

```cpp
/// TU'daki HER fonksiyon gövdesini analiz eder — top-level `func`'lar,
/// `impl`/`class` metotları ve protokol default gövdeleri dahil.
void check(TranslationUnit &tu);
```

`walkSubtree` (`liva/AST/ASTWalk.h`) ile TU bildirimleri gezilir; her
`FuncDecl` için `analyzeFunction` çağrılır. `analyzeFunction` girişte
`currentDepth_ = 0` ve `variables_.clear()` yaptığı için fonksiyonlar arası
durum sızıntısı yok — mevcut davranış korunuyor.

`walkSubtree` `FuncDecl`'i hem düğüm olarak verir hem gövdesine iner; iç içe
`FuncDecl` bulunmadığı için (blok içinde yerel `func` parse edilmiyor) çift
analiz riski yok. Yine de `analyzeFunction`'ın idempotent OLMADIĞI — aynı
fonksiyonu iki kez analiz etmek tanıları iki kez üretir — akılda tutulmalı:
gezinti her `FuncDecl`'i tam bir kez vermeli.

`walkSubtree` her fonksiyonun gövdesindeki deyimleri de gezer (bir `FuncDecl`
ararken) ve `analyzeFunction` sonra aynı gövdeyi kendi mantığıyla tekrar gezer.
Bu **kasıtlı ve zararsız**: dış gezinti yalnız düğüm türüne bakıp `FuncDecl`
olmayanları atlıyor, gerçek iş `analyzeFunction` içinde. Erken bir "optimizasyon"
uğruna dış gezintiyi `FuncDecl` görünce durduracak biçimde yazmak, iç içe
bildirim eklendiği gün sessizce kapsam kaybettirir.

`Sema.cpp` faz 3'ü tek satıra iner:

```cpp
// Phase 3: Lifetime analysis (scope-based borrow checking)
lifetimeAnalysis_.check(tu);
```

### ② `visitNode`'a hedefli `case`'ler

Eklenecekler: `IfLetStmt` (then gövdesi + varsa else gövdesi) ve `WhileLetStmt`
(gövde). İkisinin de gövdesi `BlockStmt` ve `VarDecl` tutabiliyor, yani
`var p = ref x` orada bildirilebiliyor.

**`MatchExpr` arm gövdeleri bilinçli olarak KAPSAM DIŞI** (2026-07-31'de plan
yazımı sırasında probe ile ölçüldü ve karar kullanıcı tarafından onaylandı):
`MatchArm::body` bir `Expr` (`include/liva/AST/Expr.h:340`) ve `NodeKind`'da
`BlockExpr` yok; `1 => { … }` yazımı parser'da `error: expected expression`
veriyor. Arm gövdeleri tek ifade olduğundan orada `var p = ref x` HİÇ
bildirilemez, dolayısıyla bir `MatchExpr` dalı eklemek asla tetiklenemeyecek kod
olurdu. Blok-gövdeli arm'lar ileride desteklenirse bu karar yeniden
değerlendirilmeli.

**Genel `forEachChild` fallback'i KULLANILMAYACAK.** Gerekçe: `LifetimeAnalysis`
kapsam derinliğine duyarlı — `currentDepth_` yalnız `visitBlockStmt` içinde
artıp azalıyor ve `checkScopeExit` bu sayıya göre karar veriyor. Kör bir çocuk
gezintisi blok olmayan düğümlerden de geçerek derinlik muhasebesini bozar. Bu,
`OwnershipChecker`'da genel fallback'in doğru olmasıyla arasındaki asıl fark:
orada ziyaretler durumsuzdu.

`default: break;` dalı yerinde kalır ama artık gerçekten "bu düğümde ömür
kaygısı yok" anlamına gelir.

### ③ Ölü API temizliği

`Sema::typeCheck` ve `Sema::ownershipCheck` bildirimleri ve tanımları silinir
(`include/liva/Sema/Sema.h:22,25`, `src/Sema/Sema.cpp:39-47`). Sıfır çağıranı
olan bir API'de simetri kurmak YAGNI ihlali; doğru düzeltme kaldırmak.

Silme derlemeyi kırarsa (beklenmiyor, `grep` temiz) bu bir bulgudur ve
raporlanmalı.

## Ölçüm turu

Analiz edilen yüzey bugüne kadar muaf olduğu için kaç yeni ret üreteceği
bilinmiyor. Üç yüzeyde, öncesi/sonrası:

| Yüzey | Taban (`04ef33c`) | Ne ölçülüyor |
|---|---|---|
| Tam süit | 2794/2794 | Düşen her test, gerekçesiyle |
| `ExamplesTest` kapısı (61 örnek) | hepsi derleniyor | Derlenmeyi bırakan örnekler |
| Derlenmeyen 14 örnek | zaten kırık | Hata *nedeni* değişti mi |

Her yeni ret üç kutudan birine: **(a) gerçek hata** (kod sahiden ödüncü değerden
uzun yaşatıyor) · **(b) analiz yanlış-pozitifi** (kural fazla geniş) · **(c)
önceden var olan bir sınırın açığa çıkması**.

Kapsam kararı ölçümden sonra kullanıcıya getirilir; uygulama planı ölçümü ilk
görev yapmalı ve sonraki görevleri sonuca bağlamalı.

## Test planı

**Ret pinleri** (hepsi spesifik DiagID iddia edecek —
`DiagID::err_borrow_outlives_value`):

| Pin | Gövde nerede |
|---|---|
| `impl` metot gövdesinde borrow-outlives | `impl H { func run(self) { … } }` |
| `class` metot gövdesinde borrow-outlives | `class C { func run() { … } }` |
| protokol default gövdesinde borrow-outlives | `protocol P { func run() { … } }` |
| `if let` gövdesinde borrow-outlives | `if let v = opt { … }` |

**Fazla-ret koruma pinleri:** top-level davranışı değişmedi (mevcut
`OwnershipTest`/`SemaTest` ömür testleri zaten bunu pinliyor — hangileri olduğu
uygulama sırasında tespit edilip raporlanacak) · meşru bir ödünç (`var p = ref r`
ve `r` ile aynı kapsamda) `impl` gövdesinde de kabul ediliyor · `if let`
gövdesinde meşru ödünç kabul ediliyor.

Testler `tests/unit/OwnershipTest.cpp`'ye eklenecek — ömür testlerinin bugün
yaşadığı yer orası (`RefToInnerScopeVarInit`, `BorrowOutlivesValueInnerScope`
vb. "Lifetime Analysis Tests" bölümü).

**Tam süit** `-j` OLMADAN, taban 2794/2794.

## Kabul ölçütleri

1. Yukarıdaki dört ret pini doğru tanıyla reddediliyor.
2. `Sema.cpp` faz 3'ü tek satır (`lifetimeAnalysis_.check(tu)`).
3. `visitNode` `IfLetStmt` ve `WhileLetStmt` gövdelerini geziyor; genel
   `forEachChild` fallback'i EKLENMEDİ; `MatchExpr` dalı EKLENMEDİ (gerekçe
   ②'de).
4. `Sema::typeCheck`/`Sema::ownershipCheck` silindi ve derleme temiz.
5. Ölçüm tablosu üretildi, her yeni ret sınıflandırıldı.
6. Düzeltmeler sonrası tam süit ve örnek kapısı sıfır regresyon.
7. `roadmap.md` kayıt 134'ün (7) ve (8) maddeleri ÇÖZÜLDÜ olarak işaretlendi ve
   computed property'ler için yeni kayıt açıldı.

## Yeni roadmap kaydı: computed property'ler uçtan uca kırık

Kayıt 134'ün (1) numaralı maddesi *"TypeChecker alan gövdelerini gezmiyor"*
diyor, ama keşifte ölçülen durum bundan çok daha kötü. Dört probe (`livac` @
`04ef33c`):

| Deneme | Sonuç |
|---|---|
| `class` getter'ında tanımsız ad | sessizce derleniyor (aynı gövde `func`'ta `use of undeclared identifier` alıyor) |
| `class` getter'ında `raw * 2` | `error: internal: undefined variable 'raw' in code generation` |
| `class` getter'ında `self.raw * 2` | `error: LLVM module verification failed: Incorrect number of arguments passed to called function: %class_obj = call ptr @Box_init(i32 21)` |
| `struct`'ta computed property | **parse edilmiyor** — `error: expected 'identifier', found '{'` |

Yani özellik üç katmanda birden kırık: parser (`struct` biçimini kabul etmiyor),
Sema (`TypeChecker::visitClassDecl` alan gövdelerini gezmiyor, dolayısıyla ad
çözümlemesi bile yapılmıyor) ve IRGen (getter gövdesinde `self` bağlamı yok,
class `init` arity'si yanlış). `OwnershipTest.DISABLED_ComputedPropertyGetter
WithInferredTypeIsChecked` bunun yalnız Sema ayağını pinliyor.

Bu, kayıt 134'ün (1) maddesinin yerine geçen ayrı bir iş: *"computed
property'leri çalışır hale getir"* — kendi spec'ini hak ediyor ve bir gezinti
düzeltmesiyle çözülmez.

## Bilinen risk

Blast radius ölçülmeden bilinmiyor. Ömür analizi bugüne kadar kodun büyük
bölümüne hiç bakmadı; `impl` gövdeleri stdlib'in çoğunu oluşturuyor. Ölçüm turu
(b)/(c) ağırlıklı çıkarsa kapsam daralır — o karar kullanıcıya getirilecek.
