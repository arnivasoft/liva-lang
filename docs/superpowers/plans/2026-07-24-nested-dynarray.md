# İç İçe Dinamik Diziler ([[T]]) — Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `[[T]]` çekirdeği çalışır: iç içe literal, push (yerel+üye), index okuma, length, for-in, index-atama. Spec: `docs/superpowers/specs/2026-07-24-nested-dynarray-design.md`.

**Architecture:** Temsil kararı spec'te: iç içe dinamik dizinin elemanı INLINE `%DynArray {ptr,i64,i64}` struct'ı (24B). Mevcut `liva_array_push/get` elemSize=24 ile değişmeden kullanılır; yeni runtime GEREKMEZ. Dokunuşlar yalnız IRGen'in elemanType hesaplama/kayıt noktaları + Sema eleman-tipi çözümü.

**Tech Stack:** C++20, LLVM 21, GoogleTest RuntimeExec.

## Global Constraints

- Branch: `feat/nested-dynarray` (main'den).
- TDD zorunlu; her task sonunda tam derleme + TAM seri ctest (`ctest --test-dir build-clang --output-on-failure`, ASLA `-j`, FOREGROUND, 600000ms). Taban: **2498 yeşil** (3 opt-in skip).
- Commit trailer'ları zorunlu:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216`
- Sahiplik modeli spec'ten: dış temizlik yalnız dış buffer'ı free eder; iç buffer'lar bilinçli sızar; struct kopyaları SHALLOW (iç buffer paylaşılır) — bu semantik değiştirilemez, dokümante edilir.
- Kapsam dışı (spec): `[[[T]]]` ve `[[string]]` garanti edilmez (probe sonuçları rapora), deep-copy/deep-free yok, çok-seviyeli `o.inner.rows[i] =` yok.
- Mevcut TEK-seviye dizi davranışları DEĞİŞMEZ (string-dup kuralı dahil) — regresyonlar tam suite ile korunur.
- Liva test kuralları: yorum `//`, literal'ler katı i32, top-level var yok, çağrı-sonucu member zinciri yok (anotasyonlu ara binding).
- Sapma protokolü: bir dokunuş beklenmedik kapsamda büyürse (ör. Sema eleman-çözümü kırıksa) BLOCKED raporla; 3+ başarısız düzeltme = mimari soru, controller'a dön.

---

### Task 0: Dokunuş-noktası haritası (salt-okunur, commit yok)

Rapor → `.superpowers/sdd/task-0-report.md`, her iddia file:line + probe kanıtlı:

- [ ] (a) `elementType`/`elemSize` HESAPLANAN TÜM noktaların envanteri (dinamik dizi için): IRGenDecl var-decl dizi-literal yolu (~1374+ elemType=toLLVMType(element)), çağrı-sonucu DynArray yolu (~1625+), Map keys/values, push yolları (IRGenCallMethod yerel ~470 + üye ~805), visitIndexExpr okuma yolu (IRGenExpr ~1190+), element-atama yolları (IRGenCall visitAssignExpr — yeni dup'lu üç dal), for-in (IRGenStmt DynArray iterasyonu), mono param yolları. Her nokta için: iç eleman dinamik-dizi olduğunda bugün ne üretiyor, hedef davranış ne.
- [ ] (b) `visitArrayLiteralExpr` STANDALONE davranışı: var mı, iç literal `[1,2]` bir ifade olarak visit edilince ne dönüyor (probe + kod). `[[1,2],[3,4]]` literalinin bugünkü çöküş mekanizması (nest2 probe'unun "PANIC: index out of bounds"unun tam kaynağı).
- [ ] (c) Index-okuma kayıt zinciri: `let first: [i32] = rows[0]` — visitVarDecl'in anotasyonlu-[T] çağrı-sonucu yolu IndexExpr init'i için çalışıyor mu; `first`'ün varDynArrayTypes kaydı nereden gelmeli. Sema `visitIndexExpr`'ın `[[i32]]` base için eleman tipini `[i32]` (dinamik Array) olarak çözüp çözmediği (hafıza notu: NAMED-only kısıtı vardı — Array elemanlar için durum ne?).
- [ ] (d) for-in: `for r in rows` — DynArray iterasyon codegen'inin eleman tipini nereden aldığı; döngü değişkeninin iç-dizi olarak kayıt gereksinimi.
- [ ] (e) Probe seti (livac): nest1/nest2 mevcut çöküşleri + `[[string]]`, `[[[i32]]]` bugünkü davranış (rapor için taban çizgisi).

### Task 1: Çekirdek temsil — literal init + length + index okuma (TDD)

**Files:** Modify: `src/IR/IRGenDecl.cpp` (dizi-literal + çağrı-sonucu yolları), `src/IR/IRGenExpr.cpp` (visitIndexExpr), `src/Sema/TypeChecker.cpp` (visitIndexExpr eleman çözümü — Task 0 (c) bulgusuna göre), gerekiyorsa `src/IR/IRGen.cpp`; Test: `tests/unit/RuntimeExecTest.cpp`

**Interfaces (Task 2 bunlara güvenir):**
- Kural: dinamik dizinin eleman TypeRepr'ı dinamik Array ise → LLVM eleman tipi `getDynArrayStructTy()`, elemSize = DataLayout üzerinden (24). Bu kuralı uygulayan yardımcı: `llvm::Type *IRGen::dynArrayElemLLVMType(const TypeRepr *elemRepr)` (IRGen.h'e bildirim; içerde: dinamik Array → getDynArrayStructTy(), değilse toLLVMType) — TÜM eleman-tipi hesaplama noktaları bu yardımcıyı kullanacak.
- `let first: [i32] = rows[i]` sonrası `first` tam işlevli DynArray (length/index/push).

- [ ] **Step 1: RED testleri** (RuntimeExecTest, `#endif` öncesi):

```cpp
// ============================================================
// Nested dynamic arrays [[T]] — core (roadmap 2.3)
// ============================================================
// Elements of a nested dynamic array are INLINE %DynArray structs (24B).
// Outer cleanup frees only the outer buffer; inner buffers intentionally
// leak (same profile as string elements). Copies are SHALLOW.

TEST(RuntimeExecTest, NestedArrayLiteralAndIndexRead) {
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = [[1, 2], [30, 40, 50]]
            println(rows.length)
            let first: [i32] = rows[0]
            println(first.length)
            println(first[1])
            let second: [i32] = rows[1]
            println(second.length)
            println(second[2])
        }
    )--", "nested_literal_index");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "2\n2\n2\n3\n50\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayInnerReadViaBinding) {
    // Inner array read through a binding is fully functional (push works
    // on the SHALLOW-shared copy; documented semantics).
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = [[7]]
            let inner: [i32] = rows[0]
            var total = 0
            for x in inner {
                total = total + x
            }
            println(total)
        }
    )--", "nested_inner_via_binding");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "7\n") << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: RED çalıştır** (bugün: panik/çöküş — çıktı rapora).
- [ ] **Step 3: Implementasyon** — `dynArrayElemLLVMType` yardımcısı + Task 0 (a) envanterindeki literal-init/çağrı-sonucu/visitIndexExpr noktalarına uygulama; iç literal değer üretimi Task 0 (b) bulgusuna göre (standalone visitArrayLiteralExpr struct değeri dönecek şekilde ya da decl-yolunda özel işleme — hangisi mevcut mimariye uyuyorsa, raporla); Sema visitIndexExpr `[[T]]` elemanını dinamik `[T]` olarak çözer.
- [ ] **Step 4: GREEN + tam suite + commit** — `feat(irgen): [[T]] çekirdeği — literal, length, index okuma` + trailer'lar.

### Task 2: push (yerel+üye) + index-atama + for-in + churn (TDD)

**Files:** Modify: `src/IR/IRGenCallMethod.cpp` (iki push yolu), `src/IR/IRGenCall.cpp` (element-atama dalları), `src/IR/IRGenStmt.cpp` (for-in eleman tipi/kaydı); Test: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: RED testleri**:

```cpp
TEST(RuntimeExecTest, NestedArrayPushLocalAndMember) {
    auto r = compileAndRun(R"--(
        struct Grid {
            var rows: [[i32]]
        }
        impl Grid {
            func addRow(ref mut self, row: [i32]) {
                self.rows.push(row)
            }
        }
        func main() {
            var rows: [[i32]] = []
            let a: [i32] = [1, 2, 3]
            rows.push(a)
            println(rows.length)
            let back: [i32] = rows[0]
            println(back[2])

            var g = Grid { rows: [] }
            let b: [i32] = [9, 8]
            g.addRow(b)
            println(g.rows.length)
            let gr: [i32] = g.rows[0]
            println(gr[0])
        }
    )--", "nested_push_local_member");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "1\n3\n1\n9\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayForInAndElemAssign) {
    auto r = compileAndRun(R"--(
        func main() {
            var rows: [[i32]] = [[1, 2], [3, 4]]
            var total = 0
            for row in rows {
                for x in row {
                    total = total + x
                }
            }
            println(total)
            let repl: [i32] = [100]
            rows[0] = repl
            let got: [i32] = rows[0]
            println(got[0])
            println(got.length)
        }
    )--", "nested_forin_elemassign");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "10\n100\n1\n") << "stdout: " << r.stdout_output;
}

TEST(RuntimeExecTest, NestedArrayChurnNoCorruption) {
    // UAF-family pattern: 200 iterations with heap churn; any layout-
    // dependent corruption shows as a nonzero bad count.
    auto r = compileAndRun(R"--(
        func fill() -> [[i32]] {
            var rows: [[i32]] = []
            let a: [i32] = [1, 2]
            rows.push(a)
            let b: [i32] = [3]
            rows.push(b)
            return rows
        }
        func main() {
            var bad = 0
            var i = 0
            while i < 200 {
                let rows: [[i32]] = fill()
                let junk: string = strToUpper("recycle-me-please")
                let x: [i32] = rows[0]
                let y: [i32] = rows[1]
                if x.length != 2 { bad = bad + 1 }
                if y[0] != 3 { bad = bad + 1 }
                i = i + 1
            }
            println(bad)
        }
    )--", "nested_churn");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n") << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: RED çalıştır.**
- [ ] **Step 3: Implementasyon** — push yollarında eleman tipi `dynArrayElemLLVMType` (struct elemanlar dup DEĞİL değer-kopyası — string-dup dalına GİRMEZ, `isPointerTy()` kontrolü bunu doğal sağlar); element-atama dallarında aynı; for-in eleman tipi + döngü değişkeninin iç-dizi (varDynArrayTypes) kaydı; `return rows` fonksiyon-dönüşü çalışmalı (mevcut movedVars mekanizması — Task 0 (a) çağrı-sonucu yolu).
- [ ] **Step 4: GREEN + tam suite + commit** — `feat(irgen): [[T]] push/for-in/index-atama` + trailer'lar.

### Task 3: Dokümantasyon + kapanış

**Files:** Modify: `docs/en/LANGUAGE-REFERENCE.md`, `docs/tr/LANGUAGE-REFERENCE.md`, `roadmap.md`.

- [ ] **Step 1:** LANGUAGE-REFERENCE (EN+TR) dizi bölümüne "Nested arrays" alt-notu: `[[T]]` çekirdek desteği; SHALLOW kopya semantiği (iç buffer paylaşımı, realloc'ta ayrışma) ve iç-buffer sızıntı profili açıkça; örnekler livac-doğrulamalı.
- [ ] **Step 2:** `[[string]]` ve `[[[i32]]]` davranışını probe'la (Task 0 (e) taban çizgisiyle karşılaştır); çalışmayanları LANGUAGE-REFERENCE "not yet supported" notu + roadmap izleme satırıyla dokümante et.
- [ ] **Step 3:** roadmap.md 2.3: `[[i32]]` corruption satırı → çözüldü (2026-07, çekirdek destek; kapsam-dışılar notuyla).
- [ ] **Step 4: Tam suite + commit** — `docs: [[T]] iç içe dizi dokümantasyonu — çekirdek destek tamam` + trailer'lar.

---

## Plan öz-inceleme notları

- Spec kapsama: temsil+literal+index→T1, push/atama/for-in→T2, docs/roadmap/probe-dokümantasyonu→T3. Boşluk yok.
- Tip tutarlılığı: `dynArrayElemLLVMType(const TypeRepr*)` adı T1'de tanımlı, T2 aynen kullanır.
- Risk: (b) standalone iç-literal üretimi mimariye ters çıkabilir — Task 0 önden haritalar, sapma protokolü var; Sema eleman çözümü NAMED-only kısıtına takılırsa (hafıza: `[u8]` signedness gerekçesiyle kısıtlıydı) Array-elemanlar için dikkatli genişletme + gzip/`[u8]` regresyonlarını tam suite korur.
