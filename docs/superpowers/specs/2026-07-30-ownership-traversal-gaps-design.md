# OwnershipChecker Gezinti Boşlukları — Tasarım

**Tarih:** 2026-07-30
**Durum:** Onaylandı (hedef, kapsam, yaklaşım ve ölçüm-önce süreci kullanıcı tarafından onaylı)
**Dal:** `fix/ownership-traversal-gaps`
**İlgili kayıt:** `roadmap.md:134` "AYRI İŞ" notu — checker'ın gezinti boşlukları

## Problem

`ASTVisitor`'ın **tüm** varsayılan `visit*` metotları no-op
(`include/liva/AST/ASTVisitor.h:169-225`). `OwnershipChecker` yalnız 17 düğüm
türünü override ediyor, dolayısıyla geri kalan her düğüm türü ownership
denetiminin **tamamen dışında**: o düğümlerin altındaki hiçbir kullanım
görülmüyor, çünkü ziyaret zinciri orada kopuyor.

Sonuç, aynı programın iki yazımı arasında keyfi bir ayrım. Probe ile ölçüldü
(`livac` @ `ba73e01`):

| Program (aynı `struct Packet`, aynı `send(p: Packet)`) | Bugünkü sonuç |
|---|---|
| `send(pkt)` · `send(pkt)` | ✅ `error: use of moved value 'pkt'` |
| `send(pkt)` · `println(pkt.size)` | ❌ sessizce derleniyor |
| `send(pkt)` · `println(arr[pkt.size])` | ❌ sessizce derleniyor |
| `send(pkt)` · `let q = c ? pkt : pkt` | ❌ sessizce derleniyor |

Kusur teorik değil. `Drop` uygulayan bir tipte taşıma sonrası üye okuması
derleniyor ve program **çift drop** üretiyor (probe çıktısı:
`7 / dropped / 7 / dropped`).

Mevcut test süiti boşluğu zaten belgeliyor —
`tests/unit/OwnershipTest.cpp` `PassStructByValueMoves` testinin yorumu:
*"Note: MemberExpr (pkt.size) is not tracked; use direct IdentifierExpr"*.

### Daha büyük bulgu: `impl` gövdeleri hiç denetlenmiyor

`visitImplDecl` de override edilmemiş. Yani top-level'da hata veren aynı çift
taşıma, bir `impl` metodunun içinde sessizce derleniyor (probe ile doğrulandı):

```liva
impl Holder {
    func run(self) {
        let pkt = Packet { size: 3 }
        send(pkt)
        send(pkt)     // top-level'da hata; burada tanı YOK
    }
}
```

Aynı sınıftaki diğer kör alanlar:

- `ProtocolDecl` default metot gövdeleri (`visitProtocolDecl` yok).
- `StructDecl` → `FieldDecl` üzerinden computed property getter/setter'ları,
  `willSet`/`didSet` gözlemcileri ve lazy init ifadeleri (`visitStructDecl` yok).
- Sınıflarda aynı alan gövdeleri: `visitClassDecl` VAR ama üyeleri gezerken
  yalnız `m.method`'a bakıyor, `m.field`'i atlıyor.

Yani kör alan, adı sayılan birkaç ifade türünden ibaret değil: **19 ifade + 4
bildirim türü**.

## Hedef

`OwnershipChecker`'ın ziyaret zinciri AST'nin her yerine ulaşsın; kullanım
(`checkUse`), taşıma ve ödünç denetimleri hangi sözdizimsel yoldan geçildiğine
bağlı olmaktan çıksın. Ek olarak atama HEDEFİ zincirlerinde (`w.id = 9`,
`arr[i] = x`) değişebilirlik ve ödünç denetimi uygulansın.

**Bu iş bir ölçüm turuyla başlar.** Denetlenen yüzey bugüne kadar muaf olduğu
için değişikliğin kaç yeni ret üreteceği bilinmiyor; kapsam kararı ölçümden
sonra verilir (aşağıya bakın).

## Kapsam Dışı

- **Yeni ownership KURALI eklemek.** Bu iş var olan kuralların ulaşamadığı
  yerlere ulaşmasını sağlıyor; `checkUse`/`markMoved`/`addBorrow` semantiği
  değişmiyor.
- **Mevcut 17 override'ın gövdeleri.** Özellikle `visitCallExpr` (argüman-taşıma
  mantığı), `visitAssignExpr` (değer-önce-hedef sırası) ve `visitBlockStmt`
  (son-kullanım bırakma noktası) çocuklarını bilinçli bir sırada geziyor;
  `visitChildren`'a çevirmek sırayı ve semantiği bozardı. **Tek istisna
  `visitClassDecl`** — aşağıya bakın.
- **Closure'ların gecikmeli çalıştırma semantiği.** `ClosureExpr` gövdesi
  ziyaret edilecek, ama closure tanımlandıktan SONRA taşınan bir değişkenin
  closure çağrısında kullanılması yakalanmayacak. Bu bir kaçırma (muhafazakâr),
  yanlış-pozitif değil; gerçek çözümü CFG ister (roadmap 2.5 #2).
- **`[[T]]` ailesi bellek hataları** (roadmap 2.3): shallow-alias UAF,
  çift-okuma double-free, argüman-move'un 15 dispatch noktasından 14'ünde
  eksik olması. Ayrı iş.
- **Derlenmeyen 14 örneği düzeltmek.** Ölçümde hata *nedenleri* izlenecek ama
  düzeltilmeyecekler.

## Yaklaşım

Seçilen yol: **`forEachChild` üzerinden genel çocuk-ziyareti.**

`src/AST/ASTWalk.cpp`'deki çocuk tablosu (`ba73e01`'de eklendi, 52 düğümün
tamamını kapsıyor, `switch`'te `default:` yok) zaten doğru gezinti kaynağı. Tek
bir yardımcı ve düğüm başına tek satırlık override yeterli.

**Kilit nokta: doğru okuma semantiği bedavaya geliyor.** `pkt.size` →
`visitChildren` → `visitIdentifierExpr(pkt)` → `checkUse(pkt)`. Yani yeni kural
yazılmıyor, var olan denetim tetikleniyor. `forEachChild` çocukları kaynak
sırasında verdiği için `f(a, a)` gibi sıraya duyarlı taşımalar da bozulmuyor.

Değerlendirilip reddedilen alternatif: **düğüm başına elle yazılmış,
semantik-farkında visit metotları.** Teorik olarak daha ince ayarlanabilir, ama
pratikte aynı sonucu üretiyor ("kullanım say, taşıma sayma" zaten
`visitIdentifierExpr`'in davranışı) ve bedeli 19+ elle yazılmış metot, 19 ayrı
hata yapma fırsatı ve `forEachChild` ile ikinci bir gezinti kopyası. Yalnız
ölçüm, genel ziyaretle düzeltilemeyen bir yanlış-pozitif sınıfı gösterirse
anlamlı olur.

## Bileşenler

Değişen dosyalar: `include/liva/Sema/OwnershipChecker.h`,
`src/Sema/OwnershipChecker.cpp`. Yeni dosya yok.

### ① `visitChildren` yardımcısı

`OwnershipChecker.h`'de **private** bir yardımcı olarak bildiriliyor (mevcut
`trackVariable`/`checkUse` yardımcılarının yanına); public visit yüzeyine
girmiyor çünkü yalnız override'lar çağırıyor:

```cpp
// OwnershipChecker.h — private bölüm
/// Düğümün çocuklarını kaynak sırasında ziyaret eder.
void visitChildren(ASTNode *node);
```

```cpp
// OwnershipChecker.cpp
#include "liva/AST/ASTWalk.h"

/// Düğümün çocuklarını kaynak sırasında ziyaret eder. Ownership semantiği
/// eklemez — var olan kullanım/taşıma/ödünç denetimlerinin alt ağaca
/// ulaşmasını sağlar.
void OwnershipChecker::visitChildren(ASTNode *n) {
    forEachChild(n, [&](const ASTNode *c) { visit(const_cast<ASTNode *>(c)); });
}
```

`const_cast` gerekiyor: `forEachChild` `const` üzerinden çalışıyor, `visit`
değil. Bu kod tabanında yerleşik bir kalıp (`visitFuncDecl` zaten
`const_cast<BlockStmt *>` yapıyor).

### ② Tek satırlık override'lar

**19 ifade türü:** `UnaryExpr`, `MemberExpr`, `IndexExpr`, `StructLiteralExpr`,
`MatchExpr`, `ArrayLiteralExpr`, `TupleLiteralExpr`, `CastExpr`, `IsExpr`,
`GroupExpr`, `RangeExpr`, `UnwrapExpr`, `ClosureExpr`, `TryExpr`,
`TernaryExpr`, `AwaitExpr`, `YieldExpr`, `ComptimeExpr`, `MacroInvokeExpr`.

**4 bildirim türü:** `ImplDecl`, `ProtocolDecl`, `StructDecl`, `FieldDecl`.

Her biri aynı biçimde:

```cpp
void visitMemberExpr(MemberExpr *n) { visitChildren(n); }
```

`BreakStmt`/`ContinueStmt` ve `EnumDecl`/`EnumCaseDecl`/`ImportDecl`/
`TypeAliasDecl`/`MacroDecl` çocuk taşımıyor — override gerekmiyor.

### ③ `visitClassDecl`'e hedefli ekleme (mevcut override'a tek istisna)

`visitClassDecl` bugün üyeleri gezerken yalnız `m.method`'a bakıyor. Struct'lar
`StructDecl` → `FieldDecl` yoluyla computed property gövdelerini alacağı için,
sınıfların aynı kapsamı alması adına `m.field` de ziyaret edilecek:

```cpp
for (auto &m : node->getMembers()) {
    if (m.field)
        visit(m.field.get());
    if (m.method)
        visitFuncDecl(const_cast<FuncDecl *>(m.method.get()));
}
```

Bu, override'ı `visitChildren`'a çevirmek DEĞİL — mevcut gövde ve metot
sırası korunuyor, yalnız atlanan dal ekleniyor.

### ④ Atama hedefi zinciri

Gezinti açılınca `visitAssignExpr`'in sonundaki `visit(node->getTarget())`
sayesinde kullanım-sonrası-taşıma bedavaya geliyor (`w.id = 9` →
`checkUse(w)`). Eksik kalan iki denetim değişebilirlik ve canlı ödünç üzerine
yazma. Bunun için zincirin kökünü soyan bir yardımcı:

```cpp
/// w.a.b = x · arr[i] = x · (w).id = x  →  "w" / "arr"
/// MemberExpr::getObject(), IndexExpr::getBase() ve GroupExpr::getExpr()
/// katmanlarını soyar. Kök bir IdentifierExpr değilse (ör. `f().x = 1`)
/// nullptr döner ve denetim atlanır — izlenmeyen hedefte susmak mevcut
/// davranışla tutarlı.
static const IdentifierExpr *rootIdentifier(const Expr *target);
```

Kök bulunduğunda `IdentifierExpr` dalıyla **aynı** `checkMutation` + ödünç
kontrolü uygulanıyor. Yeni DiagID yok.

`writeThroughRef` istisnası bu yolda geçerli DEĞİL: o istisna, atamanın
referente gitmesi durumu içindi (`r = 99`), üye/indeks yazımı ise zaten kök
değişkenin kendisini hedefliyor.

## Ölçüm turu

Denetlenen yüzey bugüne kadar muaf olduğu için değişikliğin kaç yeni ret
üreteceği bilinmiyor. Bu yüzden **ilk iş ölçüm**, kapsam kararı ondan sonra.

Üç yüzeyde, öncesi/sonrası karşılaştırmalı:

| Yüzey | Taban (`ba73e01`) | Ne ölçülüyor |
|---|---|---|
| Tam süit | 2762/2762 | Düşen her test, gerekçesiyle |
| `ExamplesTest` kapısı (61 örnek) | hepsi derleniyor | Derlenmeyi bırakan örnekler |
| Derlenmeyen 14 örnek | zaten kırık | Hata *nedeni* değişti mi |

Her yeni ret üç kutudan birine sınıflandırılıyor:

- **(a) Gerçek hata** — denetlenen kod sahiden sağlamsız. Düzeltilir.
- **(b) Checker semantiği kaynaklı yanlış-pozitif** — kural fazla geniş.
  Daraltılır.
- **(c) Önceden var olan bir checker sınırının açığa çıkması** — ör.
  `visitCallExpr`'in argüman-taşıma kuralı `impl` gövdelerinde ilk kez çalışıyor
  ve orada fazla agresif.

Çıktı bir tablo ve bir karar noktası. "Hepsi (a)" çıkarsa iş neredeyse bitmiş
demektir; "çoğu (b)/(c)" çıkarsa kapsamı daraltmak ya da kademeye bölmek
gündeme gelir. Uygulama planı bu yüzden ölçümü ilk görev yapmalı ve sonraki
görevleri ölçüm sonucuna bağlamalı.

En olası (a) sınıfı, atama hedefi denetiminin getirdiği ret: `let arr = [...]`
sonrası `arr[0] = 1` artık reddedilir (Rust'ta da hata). Meşru bir ret, ama
mevcut kodda yaygınsa düzeltme işi büyür.

## Test planı

**Yeni `OwnershipTest` ret pinleri** — her biri bugünkü probe'lardan, hepsi
spesifik DiagID iddia edecek (yalnız `EXPECT_FALSE(passed)` yeterli değil):

| Pin | Beklenen tanı |
|---|---|
| Taşıma sonrası üye okuma (`pkt.size`) | `err_use_after_move` |
| Taşıma sonrası indeks okuma (`arr[pkt.size]`) | `err_use_after_move` |
| Taşıma sonrası ternary operandı | `err_use_after_move` |
| `impl` metot gövdesinde çift taşıma | `err_use_after_move` |
| Protokol default gövdesinde çift taşıma | `err_use_after_move` |
| Computed property getter'ında çift taşıma | `err_use_after_move` |
| Canlı ödünçle `w.id = 9` | `err_move_while_borrowed` |
| `let w` iken `w.id = 9` | `err_assign_to_immutable` |

**Fazla-ret koruması (kabul pinleri):** taşınmamış değerin üye okuması ·
`var arr` üzerinde `arr[0] = 1` · ödünç düştükten sonra üye ataması ·
`f().x = 1` (izlenmeyen kök, sessiz kalmalı).

**Tam süit** `-j` OLMADAN, taban 2762/2762. Düzeltmeler sonrası sıfır regresyon.

## Kabul ölçütleri

1. Dört probe'un tamamı (üye, indeks, ternary, `impl` gövdesi) artık doğru
   tanıyla reddediliyor.
2. `impl`, protokol default ve computed property gövdeleri ownership denetimi
   görüyor; sınıf alanları da (`visitClassDecl` eklemesi).
3. Atama hedefi zincirlerinde değişebilirlik ve ödünç denetimi uygulanıyor;
   izlenmeyen kök sessiz kalıyor.
4. Ölçüm tablosu üretilmiş ve her yeni ret (a)/(b)/(c) olarak sınıflandırılmış.
5. Düzeltmeler sonrası tam süit ve örnek kapısı sıfır regresyon.
6. `OwnershipChecker.cpp`'nin mevcut 17 override'ı — `visitClassDecl`'in
   hedefli eklemesi dışında — değişmemiş.
7. `roadmap.md:134`'ün "AYRI İŞ" notu güncellendi.

## Bilinen risk

Blast radius ölçülmeden bilinmiyor. Ölçüm turu (b)/(c) ağırlıklı çıkarsa bu
tasarımın kapsamı daralır — o karar kullanıcıya getirilecek, plan tarafından
tek taraflı verilmeyecek.
