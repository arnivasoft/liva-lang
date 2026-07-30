# OwnershipChecker Gezinti Boşlukları — Uygulama Planı

> **Ajan çalışanlar için:** ZORUNLU ALT-SKILL: Bu planı görev-görev uygulamak için
> `superpowers:subagent-driven-development` (önerilen) veya
> `superpowers:executing-plans` kullanın. Adımlar takip için checkbox (`- [ ]`)
> sözdizimi kullanıyor.

**Hedef:** `OwnershipChecker`'ın ziyaret zinciri AST'nin her yerine ulaşsın —
kullanım, taşıma ve ödünç denetimleri hangi sözdizimsel yoldan geçildiğine bağlı
olmaktan çıksın; ek olarak atama hedefi zincirlerinde (`w.id = 9`, `arr[i] = x`)
değişebilirlik ve ödünç denetimi uygulansın.

**Mimari:** `src/AST/ASTWalk.cpp`'deki çocuk tablosunun üstünde tek bir
`visitChildren` yardımcısı ve düğüm başına tek satırlık override. Yeni ownership
KURALI yazılmıyor — var olan `checkUse`/`markMoved`/`addBorrow` denetimlerinin
alt ağaca ulaşması sağlanıyor. Atama hedefi için zincirin kökünü soyan bir
yardımcı (`rootIdentifier`) ekleniyor.

**Teknoloji:** C++20, LLVM 21 (Clang/clang-cl, `C:\LLVM`), CMake + Ninja,
GoogleTest.

**Spec:** `docs/superpowers/specs/2026-07-30-ownership-traversal-gaps-design.md`

## Global Kısıtlar

- **Dil standardı:** C++20. `-fno-exceptions` uyumlu kod yaz: `std::stoi` yerine
  `strtol`, exception fırlatan API kullanma.
- **Yapı:** artımlı `cmake --build build-clang`.
- **ctest adları gtest SINIF adıdır:** `-R OwnershipTest`, `-R ExamplesTest`,
  `-R SemaTest` doğru; CMake hedef adı (`ownership_test`) EŞLEŞMEZ.
- **Tam süit:** `ctest --test-dir build-clang --output-on-failure` — **`-j`
  KULLANMA.** Paralel koşum SelfHostTest/BuildCacheTest/IncrementalBenchmarkTest'te
  önceden var olan yarışları tetikliyor. Taban: **2762/2762**, 3 beklenen opt-in
  skip (PgRealRoundTrip, HttpLiveRoundTrip, WsLiveEchoRoundTrip).
- **`-Werror`:** hedef-bazlı, CI'da açık. Uyarı bırakma.
- **Yeni ownership kuralı yazılmayacak.** `checkUse`/`markMoved`/`addBorrow`
  semantiği değişmiyor; yalnız ulaşılabilirlik açılıyor.
- **Mevcut 17 override'ın gövdelerine dokunulmayacak** — tek istisna
  `visitClassDecl`'e atlanan alan dalının eklenmesi (Görev 1, Adım 5).
- **Dal:** `fix/ownership-traversal-gaps`. `main`'e commit YAPMA.
- **Commit mesajları Türkçe**, Conventional Commits öneki ve şu iki satırla
  bitmeli:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
  ```

  Bash aracıyla commit'lerken heredoc kullan (`git commit -F - <<'EOF' … EOF`);
  PowerShell here-string Bash'te parse hatası verir.

### Bu planın en önemli kuralı: dal Görev 1 ile Görev 2 arasında KIRMIZI olabilir

Denetlenen yüzey bugüne kadar muaf olduğu için gezintiyi açmanın kaç yeni ret
üreteceği bilinmiyor. Görev 1 **ölçüm görevidir**: gezintiyi açar, üç yüzeyde
ölçer, her yeni reti sınıflandırır ve **hiçbir fallout'u düzeltmez**. Tam süit
Görev 1 sonunda kırmızı olabilir ve bu bir kusur değil, tasarlanmış bir durumdur
— Görev 1'in commit mesajı bunu açıkça yazar. Süitin yeşile dönmesi Görev 2'nin
işidir.

**Görev 1 ile Görev 2 arasında bir KARAR NOKTASI vardır:** ölçüm tablosu
kullanıcıya sunulur ve kapsamın korunup korunmayacağına orada karar verilir.
Görev 2'nin içeriği o karara bağlıdır. Kontrolcü bu noktada durmalı ve
kullanıcıya sormalıdır.

## Dosya Yapısı

| Dosya | Sorumluluk | Görev |
|---|---|---|
| `include/liva/Sema/OwnershipChecker.h` (değişir) | 23 yeni override bildirimi + `visitChildren` ve `rootIdentifier` private yardımcıları | 1, 3 |
| `src/Sema/OwnershipChecker.cpp` (değişir) | `visitChildren` gövdesi, 23 tek satırlık override, `visitClassDecl` alan dalı, `visitAssignExpr` kök-zincir denetimi | 1, 3 |
| `tests/unit/OwnershipTest.cpp` (değişir) | Ret pinleri + fazla-ret koruma pinleri | 1, 3 |
| `roadmap.md` (değişir) | Kayıt 134'ün "AYRI İŞ" notunun kapanışı + ölçüm sonucu | 4 |

Yeni dosya yok.

---

### Görev 1: Gezinti mekanizması + ölçüm turu

**Files:**
- Modify: `include/liva/Sema/OwnershipChecker.h` (public visit bildirimleri ~satır 90 sonrası; private bölüm ~satır 94 sonrası)
- Modify: `src/Sema/OwnershipChecker.cpp` (include bloğu; `visitClassDecl` satır 42-49; dosya sonuna yakın yeni override'lar)
- Test: `tests/unit/OwnershipTest.cpp` (dosya sonuna)

**Interfaces:**
- Consumes: `liva::forEachChild(const ASTNode *, const std::function<void(const ASTNode *)> &)` — `include/liva/AST/ASTWalk.h`. Çocukları KAYNAK SIRASINDA verir; dönüş `false` = tanınmayan düğüm türü (52 düğümün tamamı tabloda olduğu için pratikte olmaz).
- Produces: `void OwnershipChecker::visitChildren(ASTNode *node)` (private) ve 23 public `visit*` override'ı. Görev 3 bunların açtığı gezintiye dayanır.

- [ ] **Adım 1: Başarısız ret pinlerini yaz**

`tests/unit/OwnershipTest.cpp` dosyasının SONUNA ekle:

```cpp
// === Gezinti boşlukları (roadmap 134 "AYRI İŞ") ===
//
// ASTVisitor'ın tüm varsayılan visit* metotları no-op, dolayısıyla
// OwnershipChecker'ın override ETMEDİĞİ her düğüm türü ownership denetiminin
// tamamen dışındaydı: ziyaret zinciri orada kopuyordu. Aşağıdaki programların
// hepsi çıplak identifier yazımıyla reddediliyor ama bu yazımlarla sessizce
// derleniyordu.

TEST_F(OwnershipTest, UseAfterMoveThroughMemberAccessRejected) {
    auto result = check(R"--(
        struct Packet {
            var size: i32
        }
        func send(p: Packet) {
            println(p.size)
        }
        func main() {
            let pkt = Packet { size: 10 }
            send(pkt)
            println(pkt.size)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_use_after_move));
}

TEST_F(OwnershipTest, UseAfterMoveThroughIndexExprRejected) {
    auto result = check(R"--(
        struct Packet {
            var size: i32
        }
        func send(p: Packet) {
            println(p.size)
        }
        func main() {
            let pkt = Packet { size: 1 }
            var arr: [i32] = [1, 2, 3]
            send(pkt)
            println(arr[pkt.size])
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_use_after_move));
}

TEST_F(OwnershipTest, UseAfterMoveThroughTernaryRejected) {
    auto result = check(R"--(
        struct Packet {
            var size: i32
        }
        func send(p: Packet) {
            println(p.size)
        }
        func main() {
            let pkt = Packet { size: 1 }
            let c: bool = true
            send(pkt)
            let q = c ? pkt : pkt
            println(q.size)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_use_after_move));
}

TEST_F(OwnershipTest, ImplMethodBodyIsOwnershipChecked) {
    // visitImplDecl override edilmemişti — impl metot gövdeleri HİÇ ownership
    // denetimi görmüyordu. Aynı çift taşıma top-level'da reddediliyor.
    auto result = check(R"--(
        struct Packet {
            var size: i32
        }
        struct Holder {
            var n: i32
        }
        func send(p: Packet) {
            println(p.size)
        }
        impl Holder {
            func run(self) {
                let pkt = Packet { size: 3 }
                send(pkt)
                send(pkt)
            }
        }
        func main() {
            let h = Holder { n: 1 }
            h.run()
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_use_after_move));
}

TEST_F(OwnershipTest, ProtocolDefaultBodyIsOwnershipChecked) {
    auto result = check(R"--(
        struct Packet {
            var size: i32
        }
        func send(p: Packet) {
            println(p.size)
        }
        protocol Runner {
            func name(self) -> string
            func run(self) {
                let pkt = Packet { size: 4 }
                send(pkt)
                send(pkt)
            }
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_use_after_move));
}

TEST_F(OwnershipTest, ComputedPropertyGetterIsOwnershipChecked) {
    // visitClassDecl üyeleri gezerken yalnız m.method'a bakıyordu; m.field
    // atlandığı için computed property gövdeleri denetim dışıydı.
    auto result = check(R"--(
        struct Packet {
            var size: i32
        }
        func send(p: Packet) {
            println(p.size)
        }
        class Box {
            var raw: i32
            var doubled: i32 {
                get {
                    let pkt = Packet { size: 5 }
                    send(pkt)
                    send(pkt)
                    return raw
                }
            }
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_use_after_move));
}

TEST_F(OwnershipTest, MemberReadOfLiveValueAccepted) {
    // Fazla-ret koruması: taşınmamış bir değerin üye okuması serbest kalmalı.
    auto result = check(R"--(
        struct Packet {
            var size: i32
        }
        func main() {
            let pkt = Packet { size: 10 }
            println(pkt.size)
            println(pkt.size)
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

Beklenen: ilk ALTI test FAIL (`result.passed` true geliyor, hiç tanı yok),
`MemberReadOfLiveValueAccepted` şimdiden PASS. Bu ayrım önemli — RED kanıtı
altı ret pini, yedincisi fazla-ret koruması.

Hangi testlerin FAIL ettiğini raporuna yaz.

- [ ] **Adım 3: Header'a bildirimleri ekle**

`include/liva/Sema/OwnershipChecker.h`, public visit bloğunun sonuna
(`void visitRefExpr(RefExpr *node);` satırından sonra):

```cpp
    // Gezinti boşlukları: aşağıdaki düğüm türleri override EDİLMEDİĞİ için
    // ASTVisitor'ın no-op varsayılanına düşüyordu ve altlarındaki hiçbir
    // kullanım görülmüyordu. Hepsi yalnızca çocuklarını ziyaret eder —
    // ownership semantiği eklemezler, var olan denetimlerin alt ağaca
    // ulaşmasını sağlarlar.
    void visitUnaryExpr(UnaryExpr *node);
    void visitMemberExpr(MemberExpr *node);
    void visitIndexExpr(IndexExpr *node);
    void visitStructLiteralExpr(StructLiteralExpr *node);
    void visitMatchExpr(MatchExpr *node);
    void visitArrayLiteralExpr(ArrayLiteralExpr *node);
    void visitTupleLiteralExpr(TupleLiteralExpr *node);
    void visitCastExpr(CastExpr *node);
    void visitIsExpr(IsExpr *node);
    void visitGroupExpr(GroupExpr *node);
    void visitRangeExpr(RangeExpr *node);
    void visitUnwrapExpr(UnwrapExpr *node);
    void visitClosureExpr(ClosureExpr *node);
    void visitTryExpr(TryExpr *node);
    void visitTernaryExpr(TernaryExpr *node);
    void visitAwaitExpr(AwaitExpr *node);
    void visitYieldExpr(YieldExpr *node);
    void visitComptimeExpr(ComptimeExpr *node);
    void visitMacroInvokeExpr(MacroInvokeExpr *node);

    void visitImplDecl(ImplDecl *node);
    void visitProtocolDecl(ProtocolDecl *node);
    void visitStructDecl(StructDecl *node);
    void visitFieldDecl(FieldDecl *node);
```

Private bölüme (`void trackVariable(...)` bildiriminden ÖNCE):

```cpp
    /// Düğümün çocuklarını kaynak sırasında ziyaret eder. Ownership semantiği
    /// eklemez — var olan kullanım/taşıma/ödünç denetimlerinin alt ağaca
    /// ulaşmasını sağlar.
    void visitChildren(ASTNode *node);
```

- [ ] **Adım 4: `visitChildren` ve 23 override'ı yaz**

`src/Sema/OwnershipChecker.cpp`'nin include bloğuna ekle:

```cpp
#include "liva/AST/ASTWalk.h"
```

Dosyanın `// === Private helpers ===` yorumundan ÖNCE (yani public visit
metotlarının sonuna) ekle:

```cpp
// === Gezinti boşlukları ===
//
// ASTVisitor'ın varsayılanları no-op olduğu için, override edilmeyen her düğüm
// türü ziyaret zincirini KESİYORDU: `send(pkt); println(pkt.size)` sessizce
// derleniyor, aynı programın `send(pkt); send(pkt)` yazımı ise reddediliyordu.
// Aşağıdakiler yeni bir ownership kuralı getirmez — yalnız var olan
// denetimlerin (checkUse/markMoved/addBorrow) alt ağaca ulaşmasını sağlar.
// Doğru okuma semantiği bedavaya gelir: pkt.size -> visitIdentifierExpr(pkt)
// -> checkUse(pkt).

void OwnershipChecker::visitChildren(ASTNode *node) {
    forEachChild(node, [&](const ASTNode *child) {
        visit(const_cast<ASTNode *>(child));
    });
}

void OwnershipChecker::visitUnaryExpr(UnaryExpr *node) { visitChildren(node); }
void OwnershipChecker::visitMemberExpr(MemberExpr *node) { visitChildren(node); }
void OwnershipChecker::visitIndexExpr(IndexExpr *node) { visitChildren(node); }
void OwnershipChecker::visitStructLiteralExpr(StructLiteralExpr *node) {
    visitChildren(node);
}
void OwnershipChecker::visitMatchExpr(MatchExpr *node) { visitChildren(node); }
void OwnershipChecker::visitArrayLiteralExpr(ArrayLiteralExpr *node) {
    visitChildren(node);
}
void OwnershipChecker::visitTupleLiteralExpr(TupleLiteralExpr *node) {
    visitChildren(node);
}
void OwnershipChecker::visitCastExpr(CastExpr *node) { visitChildren(node); }
void OwnershipChecker::visitIsExpr(IsExpr *node) { visitChildren(node); }
void OwnershipChecker::visitGroupExpr(GroupExpr *node) { visitChildren(node); }
void OwnershipChecker::visitRangeExpr(RangeExpr *node) { visitChildren(node); }
void OwnershipChecker::visitUnwrapExpr(UnwrapExpr *node) { visitChildren(node); }
// Closure GÖVDESİ ziyaret edilir: closure tanımlanmadan ÖNCE taşınmış bir
// değişkenin yakalanması yakalanır. Closure tanımlandıktan SONRA taşınan bir
// değişkenin closure ÇAĞRISINDA kullanılması yakalanmaz — bu bir kaçırma
// (muhafazakâr), yanlış-pozitif değil; gerçek çözümü CFG ister.
void OwnershipChecker::visitClosureExpr(ClosureExpr *node) { visitChildren(node); }
void OwnershipChecker::visitTryExpr(TryExpr *node) { visitChildren(node); }
void OwnershipChecker::visitTernaryExpr(TernaryExpr *node) { visitChildren(node); }
void OwnershipChecker::visitAwaitExpr(AwaitExpr *node) { visitChildren(node); }
void OwnershipChecker::visitYieldExpr(YieldExpr *node) { visitChildren(node); }
void OwnershipChecker::visitComptimeExpr(ComptimeExpr *node) { visitChildren(node); }
// Genişletilmemiş bir makronun argüman token'ları henüz AST değil, o yüzden
// forEachChild hiçbir çocuk vermez ve bu no-op'a düşer.
void OwnershipChecker::visitMacroInvokeExpr(MacroInvokeExpr *node) {
    visitChildren(node);
}

// impl metot gövdeleri HİÇ ownership denetimi görmüyordu — top-level'da
// reddedilen çift taşıma burada sessizce derleniyordu.
void OwnershipChecker::visitImplDecl(ImplDecl *node) { visitChildren(node); }
// Protokol DEFAULT metot gövdeleri için aynısı.
void OwnershipChecker::visitProtocolDecl(ProtocolDecl *node) { visitChildren(node); }
// StructDecl -> FieldDecl -> computed property getter/setter, willSet/didSet
// ve lazy init gövdeleri.
void OwnershipChecker::visitStructDecl(StructDecl *node) { visitChildren(node); }
void OwnershipChecker::visitFieldDecl(FieldDecl *node) { visitChildren(node); }
```

- [ ] **Adım 5: `visitClassDecl`'e atlanan alan dalını ekle**

`src/Sema/OwnershipChecker.cpp` satır 42-49'daki gövdeyi değiştir:

```cpp
void OwnershipChecker::visitClassDecl(ClassDecl *node) {
    // Check ownership for each method body
    for (auto &m : node->getMembers()) {
        if (m.method) {
            visitFuncDecl(const_cast<FuncDecl *>(m.method.get()));
        }
    }
}
```

şununla:

```cpp
void OwnershipChecker::visitClassDecl(ClassDecl *node) {
    for (auto &m : node->getMembers()) {
        // Alanlar atlanıyordu, dolayısıyla computed property getter/setter'ları,
        // willSet/didSet gözlemcileri ve lazy init'ler denetim dışı kalıyordu.
        // Struct'lar aynı kapsamı StructDecl -> FieldDecl yoluyla alıyor.
        if (m.field) {
            visit(m.field.get());
        }
        if (m.method) {
            visitFuncDecl(const_cast<FuncDecl *>(m.method.get()));
        }
    }
}
```

Bu, override'ı `visitChildren`'a çevirmek DEĞİL — mevcut metot dalı ve sırası
korunuyor, yalnız atlanan alan dalı ekleniyor.

- [ ] **Adım 6: Ret pinlerinin geçtiğini doğrula (GREEN)**

```
cmake --build build-clang
ctest --test-dir build-clang -R OwnershipTest --output-on-failure
```

Beklenen: Adım 1'de eklenen YEDİ testin tamamı PASS.

`OwnershipTest`'in DİĞER testlerinden düşen olursa bunları düzeltme — Adım 7'nin
ölçümüne dahil et.

- [ ] **Adım 7: Ölçüm turu — üç yüzey**

**Hiçbir fallout'u düzeltme.** Bu adımın çıktısı bir tablo.

Yüzey 1 — tam süit:

```
ctest --test-dir build-clang --output-on-failure
```

`-j` KULLANMA. Taban 2762/2762. Düşen her testin adını ve hata mesajının ilk
satırını kaydet.

Yüzey 2 — örnek kapısı:

```
ctest --test-dir build-clang -R ExamplesTest --output-on-failure
```

`AllKnownGoodExamplesCompile` düşerse hangi örneklerin derlenmeyi bıraktığını
çıktıdan çıkar.

Yüzey 3 — derlenmeyen 14 örnek. `roadmap.md:117` kaydındaki listeyi oku, her
birini tek tek derle ve hata NEDENİNİN değişip değişmediğine bak:

```
./build-clang/livac.exe examples/<ad>.liva -o build-clang/_m_<ad>.exe
```

(Geçici `.exe`'leri sonra sil; repoya bırakma.)

- [ ] **Adım 8: Sınıflandırma tablosunu yaz**

Rapor dosyana şu biçimde bir tablo yaz — HER yeni ret bir satır:

| Test/örnek | Yeni tanı | Sınıf | Gerekçe |
|---|---|---|---|
| … | … | (a)/(b)/(c) | … |

Sınıflar:
- **(a) Gerçek hata** — denetlenen kod sahiden sağlamsız (taşınmış değeri
  okuyor, ödünçlü değişkeni değiştiriyor). Düzeltilmeli.
- **(b) Checker semantiği kaynaklı yanlış-pozitif** — kural fazla geniş; kod
  doğru ama checker yanlış diyor.
- **(c) Önceden var olan bir checker sınırının açığa çıkması** — ör.
  `visitCallExpr`'in argüman-taşıma kuralı `impl` gövdelerinde İLK KEZ çalışıyor
  ve orada fazla agresif davranıyor.

Bir reti sınıflandıramıyorsan "belirsiz" yaz ve neden emin olamadığını açıkla —
tahmin etme.

Tablonun sonuna toplam sayıları yaz: kaç (a), kaç (b), kaç (c), kaç belirsiz.

- [ ] **Adım 9: Commit**

Ölçüm tablosu tamamlandıktan sonra commit'le. **Tam süit kırmızıysa yine
commit'le** — bu planın tasarlanmış bir ara durumu ve commit mesajı bunu
açıkça yazmalı.

```bash
git add include/liva/Sema/OwnershipChecker.h src/Sema/OwnershipChecker.cpp tests/unit/OwnershipTest.cpp
git commit -F - <<'EOF'
fix(sema): OwnershipChecker ziyaret zinciri AST'nin her yerine ulaşıyor

ASTVisitor'ın tüm varsayılan visit* metotları no-op ve OwnershipChecker yalnız
17 düğüm türünü override ediyordu, dolayısıyla geri kalan her düğüm ownership
denetiminin tamamen dışındaydı — ziyaret zinciri orada kopuyordu:

  send(pkt); send(pkt)            -> error: use of moved value
  send(pkt); println(pkt.size)    -> sessizce derleniyordu

Kör alan 19 ifade + 4 bildirim türüydü. En ağırı visitImplDecl'in de
olmamasıydı: impl metot gövdeleri HİÇ ownership denetimi görmüyordu. Aynı
sınıfta protokol default gövdeleri, struct computed property gövdeleri, ve
visitClassDecl'in üyeleri gezerken m.field'i atlaması vardı.

Çözüm forEachChild tablosunun üstünde tek bir visitChildren yardımcısı ve düğüm
başına tek satırlık override. Yeni ownership kuralı YOK — doğru okuma semantiği
bedavaya geliyor, çünkü pkt.size -> visitIdentifierExpr(pkt) -> checkUse(pkt).
Mevcut 17 override'ın gövdelerine dokunulmadı; tek istisna visitClassDecl'e
atlanan alan dalının eklenmesi.

Closure gövdesi ziyaret ediliyor ama gecikmeli çalıştırma modellenmiyor:
closure tanımlandıktan SONRA taşınan bir değişkenin closure çağrısında
kullanılması yakalanmaz — kaçırma, yanlış-pozitif değil.

6 ret pini + 1 fazla-ret koruma pini.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

Tam süit kırmızıysa commit mesajının sonuna, footer satırlarından ÖNCE şunu ekle
(gerçek sayıyla):

```
ÖLÇÜM: bu commit sonrası tam süitte <N> test düşüyor ve bu BEKLENEN. Denetlenen
yüzey bugüne kadar muaftı; düşen testlerin sınıflandırması ve düzeltmesi ayrı
bir commit'te. Sınıflandırma: <X> gerçek hata, <Y> checker yanlış-pozitifi,
<Z> önceden var olan sınırın açığa çıkması.
```

---

### Görev 2: Ölçüm fallout'unun düzeltilmesi

**ÖNKOŞUL — KARAR NOKTASI:** Bu görev, Görev 1'in ölçüm tablosu kullanıcıya
sunulup kapsam kararı alınmadan BAŞLAMAZ. Kontrolcü tabloyu kullanıcıya
götürmeli ve şunu sormalıdır: kapsam korunsun mu (tüm fallout düzeltilsin), yoksa
daraltılsın mı (bazı override'lar geri çekilsin)? Görev 2'nin içeriği o karara
bağlıdır.

**Files:**
- Modify: ölçüm tablosunun gösterdiği dosyalar (test, örnek, stdlib — önceden
  bilinemez)
- Modify (yalnız daraltma kararı alınırsa): `src/Sema/OwnershipChecker.cpp`,
  `include/liva/Sema/OwnershipChecker.h`

**Interfaces:**
- Consumes: Görev 1'in `visitChildren` + 23 override'ı ve ölçüm tablosu.
- Produces: yeşil tam süit. Görev 3 yeşil bir tabandan başlamalı.

- [ ] **Adım 1: Kararı ve tabloyu al**

Kontrolcüden gelen kapsam kararını ve Görev 1'in sınıflandırma tablosunu oku
(`task-1-report.md`). Karar belirsizse BAŞLAMA — NEEDS_CONTEXT ile geri dön.

- [ ] **Adım 2: (a) sınıfı retleri düzelt**

Her (a) satırı için: denetlenen KODU düzelt, testi değil. Taşınmış bir değer
okunuyorsa okuma kaldırılır ya da taşıma `ref`'e çevrilir; ödünçlü değişken
değiştiriliyorsa mutasyon ödüncün dışına taşınır.

Bir mevcut testin GÖVDESİNİ değiştirmen gerekirse, testin ÖLÇTÜĞÜ kuralı koru
ve değişikliğin gerekçesini test yorumuna yaz. Bu, bir regresyonu gizlemenin en
olağan yolu — her düzenlemeyi raporunda ayrı ayrı gerekçelendir.

- [ ] **Adım 3: (b) sınıfı retleri düzelt**

Bunlar checker'ın fazla geniş davrandığı yerler. Düzeltme kodun değil
checker'ın tarafında: ilgili override daraltılır ya da kaldırılır. Hangi
override'ın kaldırılacağı kapsam kararında belirtilmiş olmalı; belirtilmemişse
NEEDS_CONTEXT ile sor.

- [ ] **Adım 4: (c) sınıfı retleri düzelt**

Bunlar önceden var olan bir checker sınırının ilk kez görünür olması. Kapsam
kararı bunlar için ya "kodu düzelt" ya "sınırı da düzelt" ya "override'ı geri
çek" demiş olmalı. Karara uy; kendi başına genişletme.

- [ ] **Adım 5: Tam süiti yeşile getir**

```
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure
```

`-j` KULLANMA. Beklenen: 2762 + Görev 1'in 7 pini = **2769/2769**, 0 hata
(kapsam daraltılmadıysa; daraltıldıysa geri çekilen pinler kadar az).

Yeşile getiremediğin bir test kalırsa DURMA noktasıdır — BLOCKED ile rapor et,
hangi testin neden direndiğini yaz.

- [ ] **Adım 6: Commit**

```bash
git add -A
git commit -F - <<'EOF'
fix(sema): gezinti açılmasının ortaya çıkardığı ihlaller düzeltildi

Görev 1 ziyaret zincirini AST'nin her yerine ulaştırdı ve bugüne kadar denetim
dışı kalmış kod ilk kez yargılandı. Bu commit o fallout'u kapatıyor ve süiti
yeşile döndürüyor.

<Sınıflandırma tablosunun özeti: kaç (a) gerçek hata düzeltildi, kaç (b)
checker yanlış-pozitifi için override daraltıldı, kaç (c) önceden var olan
sınır ele alındı — her biri bir cümleyle.>

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 3: Atama hedefi zinciri

**ÖNKOŞUL:** Görev 2 tamamlanmış ve tam süit yeşil olmalı.

**Files:**
- Modify: `include/liva/Sema/OwnershipChecker.h` (private bölüm)
- Modify: `src/Sema/OwnershipChecker.cpp` (`visitAssignExpr`, satır ~297 civarı)
- Test: `tests/unit/OwnershipTest.cpp` (dosya sonuna)

**Interfaces:**
- Consumes: Görev 1'in açtığı gezinti (`visit(node->getTarget())` artık
  `MemberExpr`/`IndexExpr` zincirlerine iniyor, dolayısıyla kullanım-sonrası-
  taşıma zaten yakalanıyor).
- Produces: `static const IdentifierExpr *rootIdentifier(const Expr *target)`
  (dosya-yerel yardımcı) ve `visitAssignExpr`'in genişletilmiş hedef denetimi.

- [ ] **Adım 1: Başarısız testleri yaz**

`tests/unit/OwnershipTest.cpp` sonuna ekle:

```cpp
TEST_F(OwnershipTest, MemberAssignWhileBorrowedRejected) {
    // visitAssignExpr'in hedef denetimi yalnız IdentifierExpr hedeflerinde
    // çalışıyordu; `w.id = 9` canlı bir ödünç varken sessizce geçiyordu.
    auto result = check(R"--(
        struct W {
            var id: i32
        }
        func peek(x: ref W) -> i32 {
            return 0
        }
        func main() {
            var w = W { id: 1 }
            let r = ref w
            w.id = 9
            println(peek(ref r))
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_move_while_borrowed));
}

TEST_F(OwnershipTest, MemberAssignToImmutableRejected) {
    auto result = check(R"--(
        struct W {
            var id: i32
        }
        func main() {
            let w = W { id: 1 }
            w.id = 9
            println(w.id)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_to_immutable));
}

TEST_F(OwnershipTest, IndexAssignToImmutableRejected) {
    auto result = check(R"--(
        func main() {
            let arr: [i32] = [1, 2, 3]
            arr[0] = 9
            println(arr[0])
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_to_immutable));
}

TEST_F(OwnershipTest, MutableMemberAndIndexAssignAccepted) {
    // Fazla-ret koruması: var üzerinde üye ve indeks ataması serbest kalmalı.
    auto result = check(R"--(
        struct W {
            var id: i32
        }
        func main() {
            var w = W { id: 1 }
            var arr: [i32] = [1, 2, 3]
            w.id = 9
            arr[0] = 7
            println(w.id)
            println(arr[0])
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, UntrackedAssignRootStaysSilent) {
    // Fazla-ret koruması: kök bir IdentifierExpr'e inmiyorsa (çağrı sonucu)
    // denetim atlanmalı — izlenmeyen hedefte susmak getInfo'nun davranışıyla
    // tutarlı. Bu test bir ownership tanısı ÜRETİLMEDİĞİNİ pinliyor.
    auto result = check(R"--(
        struct W {
            var id: i32
        }
        func mk() -> W {
            return W { id: 1 }
        }
        func main() {
            mk().id = 9
        }
    )--");
    EXPECT_FALSE(hasDiag(result, DiagID::err_assign_to_immutable));
    EXPECT_FALSE(hasDiag(result, DiagID::err_move_while_borrowed));
}

TEST_F(OwnershipTest, MemberAssignAfterBorrowEndsAccepted) {
    // Fazla-ret koruması: ödünç son kullanımında düştükten sonra üye ataması
    // serbest (son-kullanım kısaltmasıyla birlikte çalışıyor).
    auto result = check(R"--(
        struct W {
            var id: i32
        }
        func peek(x: ref W) -> i32 {
            return 0
        }
        func main() {
            var w = W { id: 1 }
            let r = ref w
            println(peek(ref r))
            w.id = 9
            println(w.id)
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

Beklenen: ilk ÜÇ test FAIL (hedef denetimi `IdentifierExpr` dışına çıkmıyor),
son ÜÇ test (`MutableMemberAndIndexAssignAccepted`,
`UntrackedAssignRootStaysSilent`, `MemberAssignAfterBorrowEndsAccepted`)
şimdiden PASS.

`UntrackedAssignRootStaysSilent`'ın kaynağı parse ya da tip denetimini
geçemiyorsa (çağrı sonucuna atama Liva'da desteklenmiyor olabilir), testi
kaldırma — bunun yerine `f().x` yerine izlenmeyen başka bir kök dene ve hangi
biçimi kullandığını raporuna yaz.

- [ ] **Adım 3: `rootIdentifier` yardımcısını yaz**

`src/Sema/OwnershipChecker.cpp`, `visitAssignExpr`'in TANIMINDAN ÖNCE, anonim
namespace içinde:

```cpp
namespace {

/// Bir atama hedefi zincirinin kök değişkenini soyar:
///   w.a.b = x   ->  "w"
///   arr[i] = x  ->  "arr"
///   (w).id = x  ->  "w"
/// Kök bir IdentifierExpr değilse (ör. `f().x = 1`) nullptr döner ve çağıran
/// denetimi atlar — izlenmeyen hedefte susmak, getInfo'nun izlenmeyen
/// değişkenlerde sessizce geçmesiyle tutarlı.
const IdentifierExpr *rootIdentifier(const Expr *target) {
    const Expr *cur = target;
    while (cur) {
        switch (cur->getKind()) {
        case ASTNode::NodeKind::IdentifierExpr:
            return static_cast<const IdentifierExpr *>(cur);
        case ASTNode::NodeKind::MemberExpr:
            cur = static_cast<const MemberExpr *>(cur)->getObject();
            break;
        case ASTNode::NodeKind::IndexExpr:
            cur = static_cast<const IndexExpr *>(cur)->getBase();
            break;
        case ASTNode::NodeKind::GroupExpr:
            cur = static_cast<const GroupExpr *>(cur)->getExpr();
            break;
        default:
            return nullptr;
        }
    }
    return nullptr;
}

} // namespace
```

- [ ] **Adım 4: `visitAssignExpr`'in hedef denetimini genişlet**

`src/Sema/OwnershipChecker.cpp`, `visitAssignExpr` içindeki
`// Check target is mutable` bloğunun BAŞINDAKİ koşulu değiştir. Bugünkü hali:

```cpp
    // Check target is mutable
    if (node->getTarget()->getKind() == ASTNode::NodeKind::IdentifierExpr) {
        auto *ident = static_cast<IdentifierExpr *>(node->getTarget());
```

Şununla değiştir:

```cpp
    // Check target is mutable.
    //
    // Hedef çıplak bir ad olmak zorunda değil: `w.id = 9` ve `arr[i] = x` de
    // kök değişkeni YAZIYOR, dolayısıyla aynı değişebilirlik ve ödünç kuralına
    // tabidir. Bunlar önceden hiç denetlenmiyordu çünkü koşul yalnız
    // IdentifierExpr hedeflerini kabul ediyordu. Kök çözülemezse
    // (ör. `f().x = 1`) denetim atlanır.
    if (const IdentifierExpr *root = rootIdentifier(node->getTarget())) {
        auto *ident = const_cast<IdentifierExpr *>(root);
```

Bloğun geri kalanı (yorumlar dahil) DEĞİŞMEZ — `writeThroughRef` dalı,
`checkMutation` çağrısı ve ödünç kontrolü aynen kalır. `writeThroughRef`
istisnası çıplak-ad hedeflerinde eskisi gibi çalışmaya devam eder: üye/indeks
yazımında hedef zaten kök değişkenin kendisidir ve `isRefBinding` yalnız
`RefExpr` ilkleyicili bağlamalarda set edildiği için o dal tetiklenmez.

Bloğun SONUNDAKİ `visit(node->getTarget());` satırına dokunma — Görev 1'in
gezintisi sayesinde zaten üye/indeks zincirine inip kullanım denetimini
yapıyor.

- [ ] **Adım 5: Testlerin geçtiğini doğrula (GREEN)**

```
cmake --build build-clang
ctest --test-dir build-clang -R OwnershipTest --output-on-failure
```

Beklenen: Adım 1'deki ALTI testin tamamı PASS.

- [ ] **Adım 6: Tam süiti koş**

```
ctest --test-dir build-clang --output-on-failure
```

`-j` KULLANMA. Beklenen: Görev 2 sonrası taban + 6 = sıfır regresyon.

Bu adımın en olası fallout'u: `let arr = [...]` sonrası `arr[0] = 1` yazan
mevcut kod artık reddedilir. Bu MEŞRU bir rettir (Rust'ta da hata) — kodu
düzelt, testi zayıflatma. Düşen testin gerçekten bu sınıfta olduğundan emin
değilsen BLOCKED ile rapor et.

- [ ] **Adım 7: Commit**

```bash
git add include/liva/Sema/OwnershipChecker.h src/Sema/OwnershipChecker.cpp tests/unit/OwnershipTest.cpp
git commit -F - <<'EOF'
fix(sema): üye ve indeks atamaları da değişebilirlik/ödünç denetimi görüyor

visitAssignExpr'in hedef denetimi yalnız IdentifierExpr hedeflerinde
çalışıyordu, dolayısıyla `w.id = 9` ve `arr[i] = x` ne değişebilirlik ne ödünç
denetimi görüyordu: canlı bir ödünç varken üye yazımı ve `let` ile bildirilmiş
bir değişkenin üyesine atama sıfır tanıyla geçiyordu.

Zincirin kök değişkenini soyan rootIdentifier yardımcısı eklendi
(MemberExpr::getObject, IndexExpr::getBase, GroupExpr::getExpr katmanları);
kök bulunduğunda çıplak-ad dalıyla AYNI checkMutation + ödünç kontrolü
uygulanıyor. Kök bir IdentifierExpr değilse (`f().x = 1`) denetim atlanıyor —
izlenmeyen hedefte susmak getInfo'nun davranışıyla tutarlı.

Kullanım-sonrası-taşıma tarafı zaten önceki commit'in gezintisiyle kapanmıştı
(visit(target) artık zincire iniyor); bu commit değişebilirlik ve ödünç
eksenini ekliyor. Yeni DiagID yok.

3 ret pini + 3 fazla-ret koruma pini.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 4: roadmap kapanışı

**Files:**
- Modify: `roadmap.md` (kayıt 134'ün "AYRI İŞ" notu)

**Interfaces:**
- Consumes: Görev 1-3'ün ölçülmüş sonuçları.
- Produces: yok (dokümantasyon).

- [ ] **Adım 1: Son tam süit sayısını doğrula**

```
ctest --test-dir build-clang --output-on-failure
```

`-j` KULLANMA. Geçen test sayısını not al.

- [ ] **Adım 2: roadmap kaydını güncelle**

`roadmap.md`'de kayıt 134'ün sonundaki "AYRI İŞ" notunu bul. Bugünkü hali
`OwnershipChecker`'ın gezinti boşluklarını AÇIK bir iş olarak tarif ediyor
(`MemberExpr`/`IndexExpr`/`UnaryExpr`/`ClosureExpr`/`MatchExpr`/`TernaryExpr`
gezilmiyor). Bu notu, işin ÇÖZÜLDÜĞÜNÜ anlatan bir metinle değiştir. Metin şunları
içermeli, kaydın mevcut üslubuna (BÜYÜK HARF vurgu, backtick, `dosya.cpp` atıfları)
uyarak:

- Kör alanın gerçek boyutu: 19 ifade + 4 bildirim türü, ve en ağırının
  `visitImplDecl`'in olmaması olduğu (impl metot gövdeleri HİÇ denetlenmiyordu);
  yanında protokol default gövdeleri, struct/class computed property gövdeleri.
- Çözümün biçimi: `forEachChild` tablosunun üstünde `visitChildren` + düğüm
  başına tek satırlık override; yeni ownership kuralı YOK, doğru okuma semantiği
  `checkUse`'tan geliyor.
- `visitClassDecl`'e eklenen alan dalı.
- Atama hedefi ekseni: `rootIdentifier` ile üye/indeks zincirlerinde
  değişebilirlik + ödünç denetimi.
- Ölçüm turunun sonucu: kaç yeni ret çıktı ve sınıflandırması (Görev 1'in
  tablosundan; gerçek sayıları yaz, tahmin etme).
- KALAN: closure'ların gecikmeli çalıştırma semantiği modellenmiyor (closure
  tanımlandıktan sonra taşınan değişkenin çağrıda kullanılması yakalanmaz) —
  CFG ister, roadmap 2.5 #2 ile aynı iş.
- Dosya atıfları ve tam süit sayısı.

Ayrıca `roadmap.md:117` kaydındaki derlenmeyen örnekler listesinde, Görev 1'in
Adım 7 Yüzey 3 ölçümü bir örneğin hata NEDENİNİN değiştiğini gösterdiyse o
maddeyi güncelle. Değişmediyse dokunma.

- [ ] **Adım 3: Commit**

```bash
git add roadmap.md
git commit -F - <<'EOF'
docs(roadmap): checker gezinti boşlukları kapandı

Kayıt 134'ün "AYRI İŞ" notu çözüldü olarak güncellendi: kör alanın gerçek
boyutu (19 ifade + 4 bildirim türü), impl/protokol/computed gövdelerinin hiç
denetlenmiyor oluşu, çözümün biçimi (forEachChild üstünde visitChildren +
tek satırlık override'lar, yeni kural yok), atama hedefi ekseni ve ölçüm
turunun sonucu kayda işlendi.

KALAN olarak not düşüldü: closure'ların gecikmeli çalıştırma semantiği
modellenmiyor — CFG ister, 2.5 #2 ile aynı iş.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

## Kabul Ölçütleri (spec'ten)

1. ✅ Dört probe (üye, indeks, ternary, `impl` gövdesi) doğru tanıyla
   reddediliyor → Görev 1, Adım 1'in ilk dört pini.
2. ✅ `impl`, protokol default ve computed property gövdeleri denetim görüyor;
   sınıf alanları da → Görev 1, Adım 1'in 4-6. pinleri + Adım 5.
3. ✅ Atama hedefi zincirlerinde değişebilirlik ve ödünç denetimi; izlenmeyen
   kök sessiz → Görev 3.
4. ✅ Ölçüm tablosu üretilmiş ve her yeni ret sınıflandırılmış → Görev 1,
   Adım 8.
5. ✅ Düzeltmeler sonrası tam süit ve örnek kapısı sıfır regresyon → Görev 2
   Adım 5, Görev 3 Adım 6, Görev 4 Adım 1.
6. ✅ Mevcut 17 override — `visitClassDecl` istisnası dışında — değişmemiş →
   Global Kısıtlar + Görev 1 Adım 5.
7. ✅ `roadmap.md:134` "AYRI İŞ" notu güncellendi → Görev 4.

## Notlar

- **`visitChildren` neden mevcut override'ları değiştirmiyor:** `visitCallExpr`
  argümanları gezerken bare-identifier taşıması uyguluyor, `visitAssignExpr`
  değeri hedeften ÖNCE geziyor, `visitBlockStmt` deyimleri indeksleyip
  son-kullanım bırakma noktasını hesaplıyor. Bunları genel çocuk-ziyaretine
  çevirmek sırayı ve semantiği bozardı.
- **`ClosureExpr` bilinçli olarak yarım:** gövde ziyaret ediliyor (tanımdan önce
  taşınmış bir değişkenin yakalanması yakalanır) ama gecikmeli çalıştırma
  modellenmiyor. Sapma yönü kaçırma, yani muhafazakâr.
- **`MacroInvokeExpr`:** genişletilmemiş makronun argüman token'ları AST değil,
  `forEachChild` hiçbir çocuk vermiyor, override no-op'a düşüyor. Genişletilmiş
  makro varsa gövdesi geziliyor.
- **Görev 2'nin kapsamı önceden yazılamaz** çünkü ölçüm sonucuna bağlı. Bu bir
  planlama eksiği değil, tasarımın bilinçli bir sonucu: spec kapsam kararını
  ölçümden sonraya bırakıyor ve kararı kullanıcıya veriyor.
