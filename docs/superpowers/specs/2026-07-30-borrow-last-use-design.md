# Ödüncün Son Kullanımda Bırakılması — Tasarım

**Tarih:** 2026-07-30
**Durum:** Onaylandı (hedef seviyesi, kapsam, yaklaşım ve geri-çekilme kuralları kullanıcı tarafından onaylı)
**Dal:** `fix/borrow-last-use`
**İlgili kayıt:** `roadmap.md:134` (b) parçası — "ödünç hâlâ ifade düzeyinde değil"

## Problem

Bir `ref` bağlamasının tuttuğu ödünç, bağlamanın **kapsamı** bitene kadar canlı
kalıyor. Bu, önceki commit'te (`cbacac3`) kapsam ÇIKIŞINDA bırakma eklenerek
düzeltilen kusurun kalan yarısı: bırakma artık gerçekleşiyor, ama en erken
kapsam sonunda.

Bugünkü davranış (`livac` @ `cbacac3`):

```liva
func main() {
    var k: i32 = 10
    let r = ref k
    println(r)
    k = 42          // error: cannot move 'k' while it is borrowed
    println(k)
}
```

`r` bu noktadan sonra bir daha okunmuyor, dolayısıyla ödüncün canlı kalması için
hiçbir gerekçe yok. Rust bu programı kabul eder (NLL). Geçici çözüm `r`'yi iç bir
bloğa sarmak — dilin kullanıcıya ödettiği gereksiz bir bedel.

İkinci, komşu bir kusur aynı yüklemin içinde duruyor: `OwnershipChecker.cpp:260`
yalnız `BorrowedImmutable` durumunu reddediyor, bu yüzden **değişebilir** ödünç
canlıyken doğrudan atama sessizce kabul ediliyor:

```liva
var k: i32 = 10
let r = ref mut k
k = 42              // bugün: tanı YOK. Rust: E0506.
println(r)
```

## Hedef

`ref` bağlamalarının ödüncü, bağlamanın **son kullanımından** sonra bırakılsın;
sapmaların tamamı muhafazakâr ret (yanlış-pozitif) yönünde olsun, sağlamsız
kabul (yanlış-negatif) yönünde tek yol kalmasın. Ek olarak yukarıdaki `ref mut`
atama deliği kapatılsın.

## Kapsam Dışı

- **Gerçek NLL/CFG altyapısı.** Fonksiyon gövdeleri için CFG kurmak ve geriye
  doğru liveness dataflow'u çalıştırmak (roadmap 2.5 #2) bu işin dışında. Aşağıda
  "Kalan kesinlik boşlukları"nda listelenen üç desen bunu gerektiriyor.
- **`OwnershipChecker`'ın kendi gezinti boşlukları.** Checker bugün `MemberExpr`,
  `IndexExpr`, `UnaryExpr`, `ClosureExpr`, `MatchExpr`, `TernaryExpr` gibi
  düğümleri hiç gezmiyor (varsayılan no-op), yani bu yollardan geçen
  *kullanım-sonrası-taşıma* denetlenmiyor. Bu tasarımın eklediği `forEachChild`
  bunu kapatmayı mümkün kılıyor ama boşluk BU işte kapatılmıyor — ayrı iş, ayrı
  roadmap kaydı.
- **Çağrı-argümanı ödüncleri.** Zaten çağrı sonunda bırakılıyor (`releaseBorrow`,
  roadmap:134 (a) parçası). Değişiklik yok.
- **Ödünç zincirlerinin izlenmesi.** `let s = ref r` durumunda `s` → `r` → `k`
  zinciri modellenmiyor; bunun yerine kısaltma tamamen kapatılıyor (Geri-çekilme
  kuralı 4).
- **Yeni tanı mesajı / not eklemek.** Mevcut `err_move_while_borrowed`,
  `err_mut_borrow_conflict`, `err_immut_borrow_conflict` aynen kalıyor; yalnızca
  daha seyrek ateşleniyorlar.

## Yaklaşım

Seçilen yol: **deyim-granülerliğinde son-kullanım kısaltması, CFG'siz.** Ödünç,
bağlamanın *kendi deyim listesinde*, son kullanımını içeren deyim bittikten
sonra bırakılıyor.

Değerlendirilip reddedilen iki alternatif:

- **Geciktirilmiş tanı** (çakışmayı biriktir, bağlama sonra gerçekten
  kullanılırsa ateşle). Daha kesin — dal-ayrımlı ölümü doğru çözer — ama hata
  yönü ters: görülmeyen bir kullanım sessiz kabule dönüşür ve checker'ın
  gezilmeyen 6+ düğüm türü bunu teorik değil gerçek bir risk yapıyor. Ayrıca
  döngü geri-kenarı için ek kural ve tanı sıralaması sorunu getiriyor.
- **Fonksiyon geneli kullanım sayacı** (metinsel kullanım sayısını say, sıfıra
  inince bırak). Döngü gövdesi metinde bir kez sayılır ama çalışma zamanında N
  kez yürür → ödünç erken bırakılır. **Sağlamsız**, reddedildi.

## Bileşenler

Üç parça. İlk ikisi yeni ve bağımsız test edilebilir; üçüncüsü mevcut koda dar
bir dokunuş.

### ① `forEachChild` — AST katmanı

Yeni dosyalar: `include/liva/AST/ASTWalk.h`, `src/AST/ASTWalk.cpp`

```cpp
/// node'un doğrudan çocuklarını fn'e verir.
/// Dönüş: false = bu düğüm türü tabloda yok; çağıran muhafazakâr davranmalı.
bool forEachChild(const ASTNode *node,
                  const std::function<void(const ASTNode *)> &fn);

/// Pre-order tüm alt ağaç. Herhangi bir düğümde tanınmama olursa false.
bool walkSubtree(const ASTNode *node,
                 const std::function<void(const ASTNode *)> &fn);
```

Kararlar:

- **`const` üzerinden çalışır.** `MatchExpr::getSubject()` ve `getArms()` yalnız
  const erişim veriyor; tarayıcı zaten sadece okuyor, böylece AST başlıklarına
  yeni mutable accessor eklemek gerekmiyor.
- **`switch`'te `default:` dalı YOK.** `NodeKind`'ın 52 değeri (13 Decl, 10 Stmt,
  29 Expr) açıkça ele
  alınıyor; yapraklar çocuksuz `true` dönüyor. Yeni bir düğüm türü eklendiğinde
  `-Wswitch` derleme hatası veriyor ve CI'da `-DLIVA_WERROR=ON` üç job'da açık.
  Yani gezinti eksiksizliğinin garantisi çalışma zamanı bayrağı değil derleyici;
  `false` dönüş yolu yalnızca yedek olarak API'de duruyor.
- **`Pattern` atlanıyor.** `Pattern` bir `ASTNode` değil (kendi `Kind` enum'u olan
  ayrı hiyerarşi), bu yüzden `MatchExpr`'in çocukları = subject + arm gövdeleri.
  Gerekçe: patternler ad **bağlar**, ad *kullanmaz* — `IdentifierPattern` ve
  `BindingPattern` yeni isim tanımlar, mevcut bir bağlamayı okumaz. Dolayısıyla
  bir patterni atlamak bir kullanımı kaçırmaz.
- Bağımlılığı yalnız AST başlıkları; Sema'yı tanımıyor.

Not: `"\(r)"` string interpolasyonu için özel işlem gerekmiyor — parser bunu
`BinaryExpr(Add, StringLiteralExpr, CallExpr(toString, r))`'a şekerden arındırıyor
(`ParseExpr.cpp:509`), yani interpolasyon içindeki ad sıradan bir alt ağaç.
Blok içinde yerel `func` bildirimi de parse edilmiyor (`ParseStmt.cpp`'de
`kw_func` yok), dolayısıyla `ClosureExpr` dışında gecikmeli-çalıştırma yüzeyi yok.

### ② `findLastUse` — Sema

Yeni dosyalar: `include/liva/Sema/BorrowLastUse.h`, `src/Sema/BorrowLastUse.cpp`

```cpp
struct LastUseResult {
    size_t stmtIndex;   // ödüncün bırakılacağı deyim indeksi
    bool shortenable;   // false → hiç kısaltma yok, kapsam çıkışı bırakır
};

LastUseResult findLastUse(const std::vector<std::unique_ptr<ASTNode>> &stmts,
                          size_t declIndex, const std::string &name);
```

**Tarama aralığı:** `[declIndex + 1, stmts.size())` — bildirim deyiminden sonraki
her deyimin **tüm alt ağacı** `walkSubtree` ile gezilir. `name` adlı bir
`IdentifierExpr` geçen deyimlerin **en büyük** indeksi döner. Hiç geçmiyorsa
`stmtIndex = declIndex` — bildirim deyimi bittiği anda bırak.

**Geri-çekilme kuralları.** Dördü de yalnızca yukarıdaki tarama aralığına bakar,
yani başka bir bağlamanın aralığını etkilemez. Tetiklendiğinde `shortenable =
false` döner ve o bağlama için davranış bugünküyle aynı kalır (kapsam çıkışı
bırakır):

1. `walkSubtree` tanınmayan bir düğüm bildirdi.
2. Ad bir `ClosureExpr` alt ağacında geçiyor — closure saklanıp son kullanımdan
   sonra çağrılabilir.
3. Taranan aralıkta bir `MacroInvokeExpr` var — henüz genişletilmemiş, içeriği
   opak, adı kullanıp kullanmadığı bilinemez.
4. Ad bir `RefExpr`'in operandı olarak geçiyor (`let s = ref r`) — geçişli
   yeniden ödünç. Bu kural olmadan tasarımda **sağlamsız** bir delik kalıyor:

   ```liva
   var k: i32 = 10
   let r = ref k
   let s = ref r      // r'nin metinsel son kullanımı
   k = 42             // kural olmadan: r'nin ödüncü bırakılmış → KABUL
   println(s)         // s → r → k okuyor. Rust: E0506.
   ```

   `OwnershipChecker` `let s = ref r`'yi `r` üzerine bir ödünç olarak kaydeder,
   `k` üzerine değil; dolayısıyla `r`'nin `k` üzerindeki ödüncünü bırakmak `s`'in
   geçişli bağımlılığını görmezden gelir. Zinciri modellemek yerine kısaltmayı
   kapatıyoruz.

### ③ `OwnershipChecker::visitBlockStmt` — dar değişiklik

Değişen dosyalar: `src/Sema/OwnershipChecker.cpp`, `include/liva/Sema/OwnershipChecker.h`

Deyimler indeksle gezilir. Bir deyim `ref` bağlaması ürettiğinde — `visitVarDecl`
zaten `isRefBinding`, `borrowsName`, `borrowsMutable` alanlarını dolduruyor —
`findLastUse` çağrılır ve sonuç yerel bir listeye `{releaseAfterIndex,
bindingName}` olarak girer. `borrowsName` boşsa (referent izlenmiyor: global,
alan, ya da reddedilmiş ödünç) bırakılacak bir şey yok, `findLastUse` hiç
çağrılmaz. Deyim `i` bittiğinde `releaseAfterIndex == i` olan
kayıtlar için:

1. `releaseBorrow(info->borrowsName, info->borrowsMutable)`
2. `info->borrowsName.clear()` — **zorunlu**. İki nedeni var: kapsam çıkışındaki
   mevcut bırakmanın aynı ödüncü ikinci kez düşürmesini engeller, ve referent
   daha sonra başka bir ödünç aldıysa kapsam çıkışının o YENİ ödüncü yanlışlıkla
   silmesini engeller.

`dropScopeVariables` hiç değişmiyor: `borrowsName` boşsa zaten atlıyor.

Liste `visitBlockStmt`'in yerel değişkeni olarak tutulur (üye alan değil) —
iç içe bloklar doğal olarak kendi listelerini alır.

`visitFuncDecl`, `visitForStmt`, `visitIfLetStmt`, `visitWhileLetStmt` gövdelerini
`BlockStmt` üzerinden gezdiği için mekanizma tek noktadan her yere yayılıyor.

## Neden sağlam

Tek argüman, ve tasarımın tamamı buna dayanıyor:

> Bırakma noktası, bağlamanın bildirildiği **deyim listesinde** ve bildirimden
> **sonra**.

Deyim listeleri sırasaldır. Bir deyim listesine her giriş, bildirimi bırakmadan
önce çalıştırır ve listenin **içinde geri kenar yoktur** — döngüler tek bir
deyimdir ve biz bir döngü deyimini *tamamen bittikten sonra* bırakırız. Bir
döngü gövdesinde bildirilen bağlama ise o gövdenin kendi deyim listesinde
bildirilir, dolayısıyla her iterasyon taze bir ödünç alır ve aynı listede
bırakır.

Ad-tabanlı taramanın **fazla** saydığı durumlar (gölgeleyen iç bildirimler,
farklı dallardaki kullanımlar) bırakmayı yalnızca **geciktirir**, asla öne almaz
— yani muhafazakâr yönde sapar.

Erken çıkışlar (`return`, `break`, `continue`) bırakma noktasını atlar; kapsam
çıkışındaki mevcut bırakma yedek olarak yerinde duruyor.

Sonuç: her sapma yönü muhafazakâr ret tarafına düşüyor; geri-çekilme kuralı 4
eklendikten sonra yanlış-negatif üretebilecek bir yol kalmıyor.

## Rust karşılaştırması

| Desen | Rust | Bu tasarım | |
|---|---|---|---|
| `let r = &k; println(r); k = 42` | kabul | kabul | ✓ |
| `let r = &k; k = 42; println(r)` | E0506 ret | ret | ✓ |
| `let r = &mut k; *r = 1; k = 42` | kabul | kabul | ✓ |
| `let r = &mut k; k = 42; println(r)` | E0506 ret | **ret (yeni)** — bugün sessiz kabul | ✓ hizalanıyor |
| `for … { println(r) }` sonra `k = 42` | kabul | kabul | ✓ |
| `loop { println(r); k = 42 }` | ret (geri kenar) | ret | ✓ |
| `if c { println(r) } else { k = 42 }` | **kabul** (yol duyarlı) | ret | ⚠ daha muhafazakâr |
| closure `\|\| println(r)` sonra `k = 42` | kabul | ret (kural 2) | ⚠ daha muhafazakâr |
| iç blokta gölgeleyen ayrı `r` | kabul | mutasyon o bloktan önceyse ret | ⚠ daha muhafazakâr |
| `let s = ref r; k = 42; println(s)` | E0506 ret | ret (kural 4) | ✓ |

Üç yerde Rust'tan daha katı kalıyoruz; hepsi yol duyarlılığı gerektiriyor, yani
CFG'siz yaklaşımın bilinçli bedeli. Sağlamsız yönde sapma yok.

Bu işin dışında, önceden karara bağlanmış bilinçli bir Rust ayrılığı var ve
tasarım onu değiştirmiyor: Liva'da `r = 99` referente yazar (Rust'ta `*r = 99`),
`r = ref y` ise yeniden bağlar — deref operatörü olmadığından ayrımı değerin
biçimi veriyor (`b8cf76e`).

## `ref mut` atama deliği

`src/Sema/OwnershipChecker.cpp` `visitAssignExpr`, bugünkü koşul:

```cpp
if (info && info->state == OwnershipState::BorrowedImmutable) {
    diag_.report(node->getStartLoc(), DiagID::err_move_while_borrowed, ...);
}
```

`BorrowedMutable` da reddedilecek. Yeni DiagID yok — `err_move_while_borrowed`
bugünkü konvansiyon, atama yolunda da o kullanılıyor.

`writeThroughRef` dalı etkilenmiyor: `r = 99`'da atama hedefi `r`'dir ve `r`'nin
kendi durumu `Owned` (ödünç `k` üzerinde kayıtlı), dolayısıyla bu kontrole hiç
girmiyor.

**Commit sırası zorunlu:** önce son-kullanım kısaltması, sonra delik. Ters sırada
delik tek başına inerse, kısaltmanın zaten meşru kılacağı desenleri
(`let r = ref mut k; r = 1; k = 5`) geçici olarak reddedip mevcut testleri kırar.

## Test planı

**`ASTWalkTest` (yeni dosya, `tests/unit/`)** — parse edilmiş programlar üzerinden
davranış testi: çocuk sayımı ve alt ağaçtan ad toplama. `switch` eksiksizliği
derleyiciye (`-Wswitch`) bırakıldığı için burada tür sayımı test edilmiyor.
Kapsanacak biçimler: iç içe `BinaryExpr`, `CallExpr` argümanları, `MemberExpr`
zinciri, `IndexExpr`, `MatchExpr` arm gövdeleri, `ClosureExpr` gövdesi,
`StructLiteralExpr` alanları, string interpolasyonunun şekerden arındırılmış
biçimi.

**`OwnershipTest` yeni pinler:**

| Test | Beklenti |
|---|---|
| `let r = ref k; println(r); k = 42` | kabul (hedef desen) |
| `for … { println(r) }` sonra `k = 42` | kabul |
| `let r = ref mut k; r = 1; k = 5` | kabul |
| hiç kullanılmayan bağlama, hemen sonra `k = 42` | kabul |
| `while c { println(r); k = 42 }` | ret |
| closure içinde kullanım, sonra `k = 42` | ret (kural 2) |
| `let s = ref r; k = 42; println(s)` | ret (kural 4) |
| iç blokta gölgeleyen `r`, mutasyon o bloktan önce | ret (muhafazakâr) |
| `let r = ref mut k; k = 42; println(r)` | ret (`ref mut` deliği) |

**Değişmeden geçmesi gereken mevcut pinler:** `BorrowStillLiveInSameScopeStillBlocksMutation`,
`BindingBorrowStillBlocksLaterArgBorrow`, `ArgBorrowReleaseDoesNotClearBindingBorrow`
— üçü de kullanımı mutasyondan sonraya koyduğu için kısaltmadan etkilenmiyor.
`RefBindingBorrowEndsWithItsScope` ve `SequentialScopedMutableBorrowsAccepted` de
kabul tarafında kalmalı.

**`RuntimeExecTest`:** hedef desen derlenip koşmalı ve `42` basmalı.

**Tam süit:** `ctest --test-dir build-clang --output-on-failure` **seri** koşum
(paralel koşum SelfHostTest/BuildCacheTest/IncrementalBenchmarkTest'te mevcut
yarışları tetikliyor). Taban 2732/2732; hedef 2732 + yeni testler, sıfır
regresyon.

**Bonus ölçüm:** `roadmap.md:117`'deki derlenmeyen 16 örnekten `ownership` ve
`ownership_demo` "borrow checker" gerekçesiyle listede. Bu iş onları açıyor mu
ölçülüp raporlanacak; açıyorsa ExamplesTest kapısına eklenecekler.

## Kabul ölçütleri

1. Hedef desen (`let r = ref k; println(r); k = 42`) hem Sema'dan geçiyor hem
   doğru değeri basıyor.
2. Dört geri-çekilme kuralının her biri pin testiyle sabitlenmiş.
3. `ref mut` atama deliği kapalı ve pinli.
4. Mevcut 105 OwnershipTest'in tamamı geçiyor; tam süitte sıfır regresyon.
5. `forEachChild`'ın `switch`'inde `default:` yok — yeni `NodeKind` derleme
   hatası veriyor.
6. `roadmap.md:134` kaydı güncellendi: (b) parçası çözüldü, kalan kesinlik
   boşlukları (dal-ayrımlı ölüm, closure sonrası ölüm, gölgeleme) tek kayıt
   olarak yazıldı.

## Kalan kesinlik boşlukları (roadmap'e)

Üçü de yol duyarlılığı, yani gerçek CFG + liveness dataflow istiyor
(roadmap 2.5 #2 ile aynı iş):

- Dal-ayrımlı ölüm: `if c { println(r) } else { k = 42 }`.
- Closure sonrası ölüm: closure çağrıldıktan sonra referentin mutasyonu.
- Gölgeleme: iç kapsamdaki aynı adlı ayrı bir bağlama, dış bağlamanın ödüncünü
  gereksiz uzatıyor (ad-tabanlı tarama nedeniyle).

Ayrıca ayrı bir iş olarak: `OwnershipChecker`'ın gezinti boşlukları
(`MemberExpr`, `IndexExpr`, `UnaryExpr`, `ClosureExpr`, `MatchExpr`,
`TernaryExpr` üzerinden kullanım-sonrası-taşıma denetlenmiyor) — bu tasarımın
eklediği `forEachChild` bunu kapatmanın altyapısını veriyor.
