# Ömür Analizi Faz Kapsamı — Uygulama Planı

> **Ajan çalışanlar için:** ZORUNLU ALT-SKILL: Bu planı görev-görev uygulamak için
> `superpowers:subagent-driven-development` (önerilen) veya
> `superpowers:executing-plans` kullanın. Adımlar takip için checkbox (`- [ ]`)
> sözdizimi kullanıyor.

**Hedef:** Ömür analizi, ownership denetiminin ulaştığı her fonksiyon gövdesine
ulaşsın; iç gezintisi blok içeren `if let`/`while let` deyimlerini atlamasın; ve
Sema'nın kullanılmayan ikinci giriş yüzeyi temizlensin.

**Mimari:** `LifetimeAnalysis`'e TU girişi (`check(TranslationUnit&)`) eklenir;
gezinti analizin içinde `walkSubtree` ile yapılır ve `Sema.cpp` faz 3'ü tek satıra
iner. `visitNode`'a iki hedefli `case` eklenir — genel `forEachChild` fallback'i
KULLANILMAZ çünkü analiz kapsam derinliğine duyarlı.

**Teknoloji:** C++20, LLVM 21 (Clang/clang-cl), CMake + Ninja, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-07-31-lifetime-phase-coverage-design.md`

## Global Kısıtlar

- **Dil standardı:** C++20. `-fno-exceptions` uyumlu kod yaz: `std::stoi` yerine
  `strtol`, exception fırlatan API kullanma.
- **Yapı:** artımlı `cmake --build build-clang`.
- **ctest adları gtest SINIF adıdır:** `-R OwnershipTest`, `-R SemaTest`,
  `-R ExamplesTest` doğru; CMake hedef adı (`ownership_test`) EŞLEŞMEZ.
- **Tam süit:** `ctest --test-dir build-clang --output-on-failure` — **`-j`
  KULLANMA.** Taban: **2794/2794 PASS, 0 FAIL** (1 DISABLED + 3 beklenen opt-in
  skip: PgRealRoundTrip, HttpLiveRoundTrip, WsLiveEchoRoundTrip). `ctest -N`
  2795 KAYITLI gösterir — fark DISABLED testtir, bu normaldir.
- **`-Werror`:** hedef-bazlı, CI'da açık. Uyarı bırakma.
- **Analiz KURALI değişmeyecek.** Bu iş yalnız ULAŞILABİLİRLİK açıyor;
  `checkScopeExit`/`visitVarDecl`/`visitAssignExpr`'in mantığına dokunulmaz.
- **`visitNode`'a genel `forEachChild` fallback'i EKLENMEYECEK.** `currentDepth_`
  yalnız `visitBlockStmt` içinde artıp azalıyor ve `checkScopeExit` bu sayıya göre
  karar veriyor; kör gezinti derinlik muhasebesini bozar.
- **`MatchExpr` dalı EKLENMEYECEK** — arm gövdeleri `Expr`, blok parse edilmiyor,
  orada `var p = ref x` bildirilemez (spec ②).
- **Dal:** `fix/lifetime-phase-coverage`. `main`'e commit YAPMA.
- **Commit mesajları Türkçe**, Conventional Commits öneki ve şu iki satırla
  bitmeli:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
  ```

  Bash aracıyla commit'lerken heredoc kullan (`git commit -F - <<'EOF' … EOF`);
  PowerShell here-string Bash'te parse hatası verir.

### Görev 1 ile Görev 2 arasında KARAR NOKTASI var

Ömür analizi bugüne kadar kodun büyük bölümüne hiç bakmadı ve `impl` gövdeleri
stdlib'in çoğunu oluşturuyor. Görev 1 gezintiyi açar, üç yüzeyde ölçer, her yeni
reti sınıflandırır ve **hiçbir fallout'u düzeltmez**. Tam süit Görev 1 sonunda
kırmızı olabilir — bu bir kusur değil, tasarlanmış ara durumdur ve commit mesajı
bunu açıkça yazar. Kontrolcü ölçüm tablosunu kullanıcıya götürmeli ve kapsam
kararını almadan Görev 2'yi dispatch etmemelidir.

## Dosya Yapısı

| Dosya | Sorumluluk | Görev |
|---|---|---|
| `include/liva/Sema/LifetimeAnalysis.h` (değişir) | `check(TranslationUnit&)` bildirimi | 1 |
| `src/Sema/LifetimeAnalysis.cpp` (değişir) | `check` gövdesi + `visitNode`'a iki case | 1 |
| `include/liva/Sema/Sema.h` (değişir) | Faz 3 çağrısı; ölü API bildirimlerinin silinmesi | 1, 3 |
| `src/Sema/Sema.cpp` (değişir) | Faz 3 tek satır; ölü API tanımlarının silinmesi | 1, 3 |
| `tests/unit/OwnershipTest.cpp` (değişir) | Ret pinleri + fazla-ret koruma pinleri | 1 |
| `roadmap.md` (değişir) | Kayıt 134 (7)/(8) kapanışı + computed property kaydı | 4 |

Yeni dosya yok.

---

### Görev 1: Faz kapsamı + iç gezinti + ölçüm turu

**Files:**
- Modify: `include/liva/Sema/LifetimeAnalysis.h` (public bölüm, `analyzeFunction` bildiriminin yanı)
- Modify: `src/Sema/LifetimeAnalysis.cpp` (include bloğu; `analyzeFunction`'dan önce yeni `check`; `visitNode`'un `switch`'i)
- Modify: `src/Sema/Sema.cpp:30-35` (faz 3 döngüsü)
- Test: `tests/unit/OwnershipTest.cpp` (dosya sonuna)

**Interfaces:**
- Consumes: `liva::walkSubtree(const ASTNode *, const std::function<void(const ASTNode *)> &)` — `include/liva/AST/ASTWalk.h`. Pre-order, düğümün KENDİSİNİ de verir, `NodeKind`'ın tamamını kapsar.
- Produces: `void LifetimeAnalysis::check(TranslationUnit &tu)` — faz 3'ün yeni giriş noktası. Görev 3 `Sema.h`'yi temizlerken bu imzaya dayanır.

- [ ] **Adım 1: Başarısız ret pinlerini yaz**

`tests/unit/OwnershipTest.cpp` dosyasının SONUNA ekle:

```cpp
// === Ömür analizi faz kapsamı (roadmap 134 madde (8)) ===
//
// Faz 3 (Sema.cpp) yalnız top-level FuncDecl'leri geziyordu, dolayısıyla
// impl/class/protokol metot gövdeleri ömür analizinin tamamen dışındaydı.
// Aşağıdaki gövdelerin hepsi top-level bir `func`'ta yazıldığında
// err_borrow_outlives_value alıyor.

TEST_F(OwnershipTest, ImplMethodBodyGetsLifetimeAnalysis) {
    auto result = check(R"--(
        struct H {
            var n: i32
        }
        impl H {
            func run(self) {
                var r: i32 = 0
                var p = ref r
                {
                    var inner: i32 = 99
                    p = ref inner
                }
                println(p)
            }
        }
        func main() {
            let h = H { n: 1 }
            h.run()
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_borrow_outlives_value));
}

TEST_F(OwnershipTest, ClassMethodBodyGetsLifetimeAnalysis) {
    auto result = check(R"--(
        class C {
            var n: i32
            func run() {
                var r: i32 = 0
                var p = ref r
                {
                    var inner: i32 = 99
                    p = ref inner
                }
                println(p)
            }
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_borrow_outlives_value));
}

TEST_F(OwnershipTest, ProtocolDefaultBodyGetsLifetimeAnalysis) {
    auto result = check(R"--(
        protocol Runner {
            func name(self) -> string
            func run(self) {
                var r: i32 = 0
                var p = ref r
                {
                    var inner: i32 = 99
                    p = ref inner
                }
                println(p)
            }
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_borrow_outlives_value));
}

TEST_F(OwnershipTest, IfLetBodyGetsLifetimeAnalysis) {
    // visitNode'un default: break dalı IfLetStmt'i atlıyordu, dolayısıyla
    // gövdesindeki ref bağlamaları hiç görülmüyordu.
    auto result = check(R"--(
        func main() {
            let opt: i32? = 7
            if let v = opt {
                var r: i32 = 0
                var p = ref r
                {
                    var inner: i32 = 99
                    p = ref inner
                }
                println(p)
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_borrow_outlives_value));
}

TEST_F(OwnershipTest, WhileLetBodyGetsLifetimeAnalysis) {
    auto result = check(R"--(
        func take(o: i32?) -> i32? {
            return o
        }
        func main() {
            var opt: i32? = 7
            while let v = take(opt) {
                var r: i32 = 0
                var p = ref r
                {
                    var inner: i32 = 99
                    p = ref inner
                }
                println(p)
                opt = nil
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_borrow_outlives_value));
}

// === Fazla-ret korumaları ===

TEST_F(OwnershipTest, LegitimateBorrowInImplBodyAccepted) {
    // Aynı kapsamdaki meşru ödünç, gezinti açıldıktan sonra da kabul edilmeli.
    auto result = check(R"--(
        struct H {
            var n: i32
        }
        impl H {
            func run(self) {
                var r: i32 = 0
                var p = ref r
                println(p)
            }
        }
        func main() {
            let h = H { n: 1 }
            h.run()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, LegitimateBorrowInIfLetBodyAccepted) {
    auto result = check(R"--(
        func main() {
            let opt: i32? = 7
            if let v = opt {
                var r: i32 = 0
                var p = ref r
                println(p)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}
```

- [ ] **Adım 2: Testlerin başarısız olduğunu doğrula (RED)**

```
cmake --build build-clang
ctest --test-dir build-clang -R OwnershipTest --output-on-failure
```

Beklenen: ilk BEŞ test FAIL (`result.passed` true geliyor, tanı yok), son İKİ
test şimdiden PASS. Hangilerinin gerçekten FAIL ettiğini raporuna yaz — RED
kanıtı beş ret pini.

Bir test kaynağı parse ya da tip denetimini geçemezse (özellikle `while let`
biçimi) testin ÖLÇTÜĞÜ şeyi koru, kaynağı uyarla ve hangi biçimi kullandığını
rapora yaz.

- [ ] **Adım 3: `check(TranslationUnit&)` bildirimini ekle**

`include/liva/Sema/LifetimeAnalysis.h`, `analyzeFunction` bildiriminin HEMEN
ÜSTÜNE:

```cpp
    /// TU'daki HER fonksiyon gövdesini analiz eder — top-level `func`'lar,
    /// `impl`/`class` metotları ve protokol default gövdeleri dahil.
    ///
    /// Faz 3 eskiden Sema.cpp'de kendi döngüsünü yazıyor ve yalnız top-level
    /// FuncDecl'leri geziyordu; faz 1 ve 2 ise TU'yu alıyordu. Bu giriş o
    /// asimetriyi hem davranışta hem imzada kapatıyor.
    void check(TranslationUnit &tu);
```

- [ ] **Adım 4: `check` gövdesini yaz**

`src/Sema/LifetimeAnalysis.cpp`'nin include bloğuna ekle:

```cpp
#include "liva/AST/ASTWalk.h"
```

`analyzeFunction` tanımının HEMEN ÜSTÜNE ekle:

```cpp
// walkSubtree her fonksiyonun gövdesindeki deyimleri de gezer (bir FuncDecl
// ararken) ve analyzeFunction sonra aynı gövdeyi kendi mantığıyla tekrar gezer.
// Bu KASITLI ve zararsız: dış gezinti yalnız düğüm türüne bakıyor, gerçek iş
// analyzeFunction içinde. Dış gezintiyi "optimizasyon" için FuncDecl görünce
// durduracak biçimde yazmak, iç içe bildirim eklendiği gün sessizce kapsam
// kaybettirir.
//
// analyzeFunction idempotent DEĞİL — aynı fonksiyonu iki kez analiz etmek
// tanıları iki kez üretir. Bugün blok içinde yerel `func` parse edilmediği için
// iç içe FuncDecl yok, dolayısıyla her gövde tam bir kez veriliyor.
void LifetimeAnalysis::check(TranslationUnit &tu) {
    for (auto &decl : tu.getDeclarations()) {
        walkSubtree(decl.get(), [&](const ASTNode *node) {
            if (node->getKind() == ASTNode::NodeKind::FuncDecl)
                analyzeFunction(
                    const_cast<FuncDecl *>(static_cast<const FuncDecl *>(node)));
        });
    }
}
```

- [ ] **Adım 5: `visitNode`'a iki hedefli case ekle**

`src/Sema/LifetimeAnalysis.cpp`, `visitNode`'un `switch`'inde `ReturnStmt`
case'inin ARDINA, `default:`ten ÖNCE:

```cpp
    // if-let/while-let gövdeleri BlockStmt ve VarDecl tutabiliyor, yani
    // `var p = ref x` orada bildirilebiliyor — default: break dalına düştükleri
    // için hiç görülmüyorlardı.
    //
    // MatchExpr BİLİNÇLİ olarak yok: MatchArm::body bir Expr ve NodeKind'da
    // BlockExpr olmadığı için `1 => { … }` parse edilmiyor; arm gövdesinde
    // `var p = ref x` bildirilemez, dolayısıyla bir MatchExpr dalı asla
    // tetiklenemezdi.
    case ASTNode::NodeKind::IfLetStmt: {
        auto *s = static_cast<IfLetStmt *>(node);
        visitNode(s->getThenBody());
        if (s->hasElse())
            visitNode(s->getElseBody());
        break;
    }
    case ASTNode::NodeKind::WhileLetStmt:
        visitNode(static_cast<WhileLetStmt *>(node)->getBody());
        break;
```

`default: break;` YERİNDE KALIR — artık gerçekten "bu düğümde ömür kaygısı yok"
anlamına geliyor.

- [ ] **Adım 6: `Sema.cpp` faz 3'ünü tek satıra indir**

`src/Sema/Sema.cpp` satır 30-35'teki bloğu:

```cpp
    // Phase 3: Lifetime analysis (scope-based borrow checking)
    for (auto &decl : tu.getDeclarations()) {
        if (decl->getKind() == ASTNode::NodeKind::FuncDecl) {
            lifetimeAnalysis_.analyzeFunction(static_cast<FuncDecl *>(decl.get()));
        }
    }
```

şununla değiştir:

```cpp
    // Phase 3: Lifetime analysis (scope-based borrow checking).
    // Gezinti artık analizin kendi içinde — faz 1 ve 2 gibi bu da TU alıyor.
    lifetimeAnalysis_.check(tu);
```

- [ ] **Adım 7: Ret pinlerinin geçtiğini doğrula (GREEN)**

```
cmake --build build-clang
ctest --test-dir build-clang -R OwnershipTest --output-on-failure
```

Beklenen: Adım 1'de eklenen YEDİ testin tamamı PASS.

`OwnershipTest`'in DİĞER testlerinden düşen olursa düzeltme — Adım 8'in
ölçümüne dahil et.

- [ ] **Adım 8: Ölçüm turu — üç yüzey**

**Hiçbir fallout'u düzeltme.** Bu adımın çıktısı bir tablo.

Yüzey 1 — tam süit (`-j` KULLANMA, taban 2794/2794):

```
ctest --test-dir build-clang --output-on-failure
```

Düşen her testin adını ve hata mesajının ilk satırını kaydet.

Yüzey 2 — örnek kapısı:

```
ctest --test-dir build-clang -R ExamplesTest --output-on-failure
```

`AllKnownGoodExamplesCompile` düşerse hangi örneklerin derlenmeyi bıraktığını
çıktıdan çıkar.

Yüzey 3 — derlenmeyen 14 örnek. `roadmap.md:117` kaydındaki listeyi oku, her
birini derle ve hata NEDENİNİN değişip değişmediğine bak:

```
./build-clang/livac.exe --check-only examples/<ad>.liva
```

- [ ] **Adım 9: Sınıflandırma tablosunu yaz**

Rapor dosyana şu biçimde bir tablo yaz — HER yeni ret bir satır:

| Test/örnek | Yeni tanı | Sınıf | Gerekçe |
|---|---|---|---|
| … | … | (a)/(b)/(c) | … |

Sınıflar:
- **(a) Gerçek hata** — kod sahiden ödüncü değerden uzun yaşatıyor. Düzeltilmeli.
- **(b) Analiz yanlış-pozitifi** — kod doğru ama analiz yanlış diyor. Analiz bugün
  çok basit (kapsam derinliği + `refTarget`), bu sınıf beklenebilir.
- **(c) Önceden var olan bir sınırın açığa çıkması.**

Sınıflandıramadığın reti "belirsiz" yaz ve neden emin olamadığını açıkla.
Tablonun sonuna toplam sayıları yaz.

- [ ] **Adım 10: Commit**

Ölçüm tablosu tamamlandıktan sonra commit'le. **Tam süit kırmızıysa yine
commit'le** — planın tasarlanmış ara durumu.

```bash
git add include/liva/Sema/LifetimeAnalysis.h src/Sema/LifetimeAnalysis.cpp src/Sema/Sema.cpp tests/unit/OwnershipTest.cpp
git commit -F - <<'EOF'
fix(sema): ömür analizi her fonksiyon gövdesine ulaşıyor

Sema'nın üçüncü fazı Sema.cpp'de kendi döngüsünü yazıyor ve yalnız top-level
FuncDecl'leri geziyordu, dolayısıyla impl/class/protokol metot gövdeleri ömür
analizinin tamamen dışındaydı:

  top-level func run() { var p = ref r; { var inner = 99; p = ref inner } }
    -> error: borrow of 'inner' outlives the value
  aynı gövde impl H { func run(self) { ... } } içinde
    -> sessizce derleniyordu

Bu, önceki dalın OwnershipChecker için kapattığı boşluğun tam ikizi, bir faz
ötede; o iş bu tutarsızlığı görünür kıldı.

İkinci, iç içe boşluk: visitNode'un default: break dalı IfLetStmt ve
WhileLetStmt gövdelerini atlıyordu, oralardaki ref bağlamaları hiç
görülmüyordu.

Çözüm: LifetimeAnalysis'e TU girişi (check(TranslationUnit&)) — gezinti
analizin içinde walkSubtree ile yapılıyor ve Sema.cpp faz 3'ü tek satıra indi;
faz 1 ve 2 zaten TU alıyordu, asimetri imzada da kapandı. visitNode'a iki
hedefli case eklendi. GENEL forEachChild fallback'i eklenmedi: analiz kapsam
derinliğine duyarlı (currentDepth_ yalnız visitBlockStmt'te değişiyor), kör
gezinti derinlik muhasebesini bozardı. MatchExpr dalı da eklenmedi — arm
gövdeleri Expr ve `1 => { … }` parse edilmiyor, orada ref bağlaması
bildirilemez.

Analiz KURALI değişmedi; yalnız ulaşılabilirlik açıldı.

5 ret pini + 2 fazla-ret koruma pini.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

Tam süit kırmızıysa commit mesajının sonuna, footer satırlarından ÖNCE şunu ekle
(gerçek sayılarla):

```
ÖLÇÜM: bu commit sonrası tam süitte <N> test düşüyor ve bu BEKLENEN. Analiz
edilen yüzey bugüne kadar muaftı; düşen testlerin sınıflandırması ve düzeltmesi
ayrı bir commit'te. Sınıflandırma: <X> gerçek hata, <Y> analiz yanlış-pozitifi,
<Z> önceden var olan sınırın açığa çıkması.
```

---

### Görev 2: Ölçüm fallout'unun düzeltilmesi

**ÖNKOŞUL — KARAR NOKTASI:** Bu görev, Görev 1'in ölçüm tablosu kullanıcıya
sunulup kapsam kararı alınmadan BAŞLAMAZ. Kontrolcü şunu sormalıdır: kapsam
korunsun mu (tüm fallout düzeltilsin), yoksa daraltılsın mı? Görev 2'nin içeriği
o karara bağlıdır.

**Files:**
- Modify: ölçüm tablosunun gösterdiği dosyalar (önceden bilinemez)
- Modify (yalnız daraltma kararı alınırsa): `src/Sema/LifetimeAnalysis.cpp`

**Interfaces:**
- Consumes: Görev 1'in `check(TranslationUnit&)`'i ve ölçüm tablosu.
- Produces: yeşil tam süit. Görev 3 yeşil bir tabandan başlamalı.

- [ ] **Adım 1: Kararı ve tabloyu al**

Kontrolcüden gelen kapsam kararını ve Görev 1'in sınıflandırma tablosunu oku
(`task-1-report.md`). Karar belirsizse BAŞLAMA — NEEDS_CONTEXT ile geri dön.

- [ ] **Adım 2: (a) sınıfı retleri düzelt**

Her (a) satırı için denetlenen KODU düzelt, testi değil: ödünç değerden uzun
yaşıyorsa ya ödünç iç kapsama alınır ya değer dış kapsama taşınır.

Bir mevcut testin GÖVDESİNİ değiştirmen gerekirse, testin ÖLÇTÜĞÜ kuralı koru ve
gerekçeyi test yorumuna yaz. Her düzenlemeyi raporunda ayrı ayrı gerekçelendir —
mevcut testi değiştirmek bir regresyonu gizlemenin en olağan yolu.

- [ ] **Adım 3: (b) sınıfı retleri düzelt**

Bunlar analizin fazla geniş davrandığı yerler. Düzeltme kodun değil analizin
tarafında. **Ama analiz KURALINI değiştirmek bu planın Global Kısıtları'na
aykırı** — kapsam kararı bunu açıkça izin vermediyse NEEDS_CONTEXT ile sor.
İzin verildiyse hangi kuralın nasıl daraltıldığını raporuna yaz ve koruma pini
ekle.

- [ ] **Adım 4: (c) sınıfı retleri düzelt**

Kapsam kararı bunlar için ya "kodu düzelt" ya "sınırı da düzelt" ya "gezintiyi
daralt" demiş olmalı. Karara uy; kendi başına genişletme.

- [ ] **Adım 5: Tam süiti yeşile getir**

```
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure
```

`-j` KULLANMA. Beklenen: 2794 + Görev 1'in 7 pini = **2801/2801**, 0 hata
(kapsam daraltılmadıysa).

Yeşile getiremediğin bir test kalırsa BLOCKED ile rapor et.

- [ ] **Adım 6: Commit**

```bash
git add -A
git commit -F - <<'EOF'
fix(sema): ömür analizinin ulaşmasının ortaya çıkardığı ihlaller düzeltildi

Görev 1 faz 3'ü her fonksiyon gövdesine ulaştırdı ve bugüne kadar analiz dışı
kalmış kod ilk kez yargılandı. Bu commit o fallout'u kapatıyor ve süiti yeşile
döndürüyor.

<Sınıflandırma tablosunun özeti: kaç (a) gerçek hata düzeltildi, kaç (b) analiz
yanlış-pozitifi ele alındı, kaç (c) önceden var olan sınır — her biri bir
cümleyle.>

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 3: Ölü Sema API'sinin kaldırılması

**ÖNKOŞUL:** Görev 2 tamamlanmış ve tam süit yeşil olmalı.

**Files:**
- Modify: `include/liva/Sema/Sema.h:21-25`
- Modify: `src/Sema/Sema.cpp:39-47`

**Interfaces:**
- Consumes: yok.
- Produces: yok (yüzey daraltma).

- [ ] **Adım 1: Çağıran olmadığını bizzat doğrula**

```
grep -rn "ownershipCheck\|typeCheck" --include=*.cpp --include=*.h src/ tests/ tools/ 2>/dev/null | grep -v "Sema.cpp\|Sema.h\|TypeChecker"
```

Beklenen: `src/Sema/OwnershipChecker.cpp` içindeki bir YORUM dışında eşleşme yok.
Bir çağıran bulursan **SİLME** — BLOCKED ile bildir, plan yanlış demektir.

- [ ] **Adım 2: Bildirimleri sil**

`include/liva/Sema/Sema.h`'den şu iki bloğu kaldır:

```cpp
    /// Run only type checking
    bool typeCheck(TranslationUnit &tu);

    /// Run only ownership checking
    bool ownershipCheck(TranslationUnit &tu);
```

- [ ] **Adım 3: Tanımları sil**

`src/Sema/Sema.cpp`'den şu iki fonksiyonu kaldır:

```cpp
bool Sema::typeCheck(TranslationUnit &tu) {
    typeChecker_.check(tu);
    return !diag_.hasErrors();
}

bool Sema::ownershipCheck(TranslationUnit &tu) {
    ownershipChecker_.check(tu);
    return !diag_.hasErrors();
}
```

`src/Sema/OwnershipChecker.cpp:97` civarındaki yorum `Sema::ownershipCheck`'e
atıf yapıyor. Yorumu, artık var olmayan bir API'ye atıf yapmayacak şekilde
güncelle — anlattığı şey (TU'ya özel durumun her girişte sıfırlanması) geçerli
kalmalı.

- [ ] **Adım 4: Derleme ve tam süit**

```
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure
```

`-j` KULLANMA. Beklenen: Görev 2 sonrası sayı, sıfır regresyon. Derleme kırılırsa
bir çağıran vardır — Adım 1'i tekrarla ve BLOCKED ile bildir.

- [ ] **Adım 5: Commit**

```bash
git add include/liva/Sema/Sema.h src/Sema/Sema.cpp src/Sema/OwnershipChecker.cpp
git commit -F - <<'EOF'
refactor(sema): kullanılmayan Sema::typeCheck/ownershipCheck kaldırıldı

İkisinin de tüm repoda sıfır çağıranı vardı; tek giriş Sema::analyze
(CompilerInstance.cpp'de 5 yerde). Ayrıca ownershipCheck, analyze'ın yaptığı
setClassNames/setDropTypeNames kurulumunu atlıyordu, yani o giriş üzerinden
koşan bir çağıran class muafiyetini ve Drop move-semantiğini hiç görmezdi.

roadmap kayıt 134 madde (7) bunu asimetri olarak not etmişti; ölçüldüğünde
gizli bir hata değil ölü kod olduğu görüldü, o yüzden simetri kurmak yerine
kaldırıldı.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 4: roadmap kapanışı

**Files:**
- Modify: `roadmap.md` (kayıt 134'ün AYRI İŞ listesi)

**Interfaces:**
- Consumes: Görev 1-3'ün ölçülmüş sonuçları.
- Produces: yok.

- [ ] **Adım 1: Son tam süit sayısını doğrula**

```
ctest --test-dir build-clang --output-on-failure
```

`-j` KULLANMA. Geçen sayıyı not al. `ctest -N`'in gösterdiği KAYITLI sayı bundan
1 fazladır (DISABLED test) — kayda koşum satırındaki sayıyı yaz.

- [ ] **Adım 2: (7) ve (8) maddelerini kapat**

`roadmap.md` kayıt 134'ün AYRI İŞ listesinde:

- **(8)** maddesini ÇÖZÜLDÜ olarak işaretle ve şunları yaz: faz 3 artık TU
  alıyor (`LifetimeAnalysis::check`), gezinti `walkSubtree` ile analizin içinde,
  `Sema.cpp` faz 3'ü tek satır; ayrıca `visitNode`'a `IfLetStmt`/`WhileLetStmt`
  case'leri eklendi. Genel `forEachChild` fallback'inin EKLENMEDİĞİNİ ve
  gerekçesini (analiz kapsam derinliğine duyarlı, `currentDepth_` yalnız
  `visitBlockStmt`'te değişiyor) yaz. `MatchExpr`'in kapsam dışı bırakıldığını ve
  gerekçesini (`MatchArm::body` bir `Expr`, `1 => { … }` parse edilmiyor, orada
  `var p = ref x` bildirilemez) yaz. Ölçüm turunun sonucunu gerçek sayılarla yaz.
- **(7)** maddesini ÇÖZÜLDÜ olarak işaretle: `Sema::typeCheck`/`ownershipCheck`
  ölü kod çıktı (sıfır çağıran) ve kaldırıldı.

- [ ] **Adım 3: (1) maddesini yeni kayıtla değiştir**

Kayıt 134'ün (1) numaralı maddesi bugün *"`TypeChecker::visitClassDecl` alan
gövdelerini gezmiyor"* diyor. Ölçülen durum bundan çok daha kötü — computed
property'ler ÜÇ KATMANDA birden kırık. Maddeyi şu dört probe sonucunu içerecek
şekilde genişlet (`livac` @ `04ef33c`):

| Deneme | Sonuç |
|---|---|
| `class` getter'ında tanımsız ad | sessizce derleniyor (aynı gövde `func`'ta `use of undeclared identifier` alıyor) |
| `class` getter'ında `raw * 2` | `error: internal: undefined variable 'raw' in code generation` |
| `class` getter'ında `self.raw * 2` | `error: LLVM module verification failed: Incorrect number of arguments passed to called function: %class_obj = call ptr @Box_init(i32 21)` |
| `struct`'ta computed property | parse edilmiyor — `error: expected 'identifier', found '{'` |

Maddenin sonucu şu olmalı: bu bir gezinti boşluğu DEĞİL, kendi spec'ini hak eden
ayrı bir iş — *"computed property'leri çalışır hale getir"* (parser: `struct`
biçimi; Sema: `TypeChecker::visitClassDecl` alan gövdeleri, `TypeChecker.cpp:1569,1585`;
IRGen: getter gövdesinde `self` bağlamı ve class `init` arity'si).
`OwnershipTest.DISABLED_ComputedPropertyGetterWithInferredTypeIsChecked` bunun
yalnız Sema ayağını pinliyor.

- [ ] **Adım 4: Commit**

```bash
git add roadmap.md
git commit -F - <<'EOF'
docs(roadmap): ömür analizi faz kapsamı kapandı, computed property kaydı düzeltildi

Kayıt 134'ün (8) maddesi çözüldü: faz 3 artık TU alıyor ve gezinti analizin
içinde; visitNode'a IfLetStmt/WhileLetStmt case'leri eklendi. Genel forEachChild
fallback'inin ve MatchExpr dalının neden EKLENMEDİĞİ gerekçeleriyle yazıldı.
(7) maddesi de çözüldü: Sema::typeCheck/ownershipCheck ölü kod çıktı ve
kaldırıldı.

(1) maddesi ölçülen gerçeğe göre genişletildi: computed property'ler bir gezinti
boşluğundan ibaret değil, parser/Sema/IRGen üç katmanda birden kırık — dört
probe sonucu kayda işlendi ve kendi spec'ini hak eden ayrı bir iş olarak
işaretlendi.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

## Kabul Ölçütleri (spec'ten)

1. ✅ Dört ret pini (impl/class/protokol/if-let) doğru tanıyla reddediliyor →
   Görev 1 Adım 1 (beş pin: while-let de eklendi).
2. ✅ `Sema.cpp` faz 3'ü tek satır → Görev 1 Adım 6.
3. ✅ `visitNode` `IfLetStmt`/`WhileLetStmt` geziyor; genel `forEachChild`
   fallback'i ve `MatchExpr` dalı EKLENMEDİ → Görev 1 Adım 5 + Global Kısıtlar.
4. ✅ `Sema::typeCheck`/`ownershipCheck` silindi, derleme temiz → Görev 3.
5. ✅ Ölçüm tablosu üretildi, her ret sınıflandırıldı → Görev 1 Adım 9.
6. ✅ Sıfır regresyon → Görev 2 Adım 5, Görev 3 Adım 4, Görev 4 Adım 1.
7. ✅ roadmap (7)/(8) kapatıldı + computed property kaydı → Görev 4.

## Notlar

- **Analiz kuralı değişmiyor.** Bu iş yalnız ulaşılabilirlik açıyor. Ölçümde (b)
  sınıfı retler çıkarsa — analiz bugün çok basit olduğu için çıkabilir — kuralı
  daraltmak Global Kısıtlar'a aykırıdır ve kapsam kararı gerektirir.
- **Görev 2'nin içeriği önceden yazılamaz** çünkü ölçüm sonucuna bağlı. Spec
  kapsam kararını ölçümden sonraya ve kullanıcıya bırakıyor.
- **`while let` test kaynağı riskli olabilir:** `while let v = take(opt)` biçimi
  sonsuz döngüye girmesin diye gövdede `opt = nil` var. Sema-only test olduğu için
  koşulmuyor, ama parse/tip denetiminden geçmesi gerekiyor. Geçmezse Görev 1
  Adım 2'nin talimatına göre biçimi uyarla ve raporla.
